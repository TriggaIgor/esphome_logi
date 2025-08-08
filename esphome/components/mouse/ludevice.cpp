/*
 Copyright (C) 2017 Ronan Gaillard <ronan.gaillard@live.fr>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
*/
#include "ludevice.h"

#ifdef EEPROM_SUPPORT
#include <EEPROM.h>
#endif

ludevice::ludevice() : ludevice(DEFAULT_CE_PIN, DEFAULT_CS_PIN)
{
}

ludevice::ludevice(uint8_t _cepin, uint8_t _cspin) : radio(_cepin, _cspin)
{
}

bool ludevice::startSniffing() {
    if (is_connected) return false;
    
    radio.stopListening();
    radio.openReadingPipe(1, (uint64_t)0); // Открываем pipe для прослушивания всех адресов
    radio.startListening();
    sniffingMode = true;
    return true;
}

void ludevice::stopSniffing() {
    sniffingMode = false;
    radio.stopListening();
}

bool ludevice::checkForOtherDevices(uint8_t* foundAddress) {
    if (!sniffingMode || !radio.available()) return false;
    
    uint8_t payload[PAYLOAD_SIZE];
    uint8_t len = radio.getDynamicPayloadSize();
    if (len < 1) return false;
    
    radio.read(payload, len);
    
    // Проверяем, что это пакет от мыши Logitech
    if (payload[1] == 0xC2 || payload[1] == 0xC3) { // Типичные типы пакетов мыши
        // Получаем адрес отправителя
        uint64_t address;
        radio.readRegister(RX_ADDR_P1, &address, 5);
        
        // Исключаем наш собственный адрес
        if (memcmp(&address, rf_address, 5) != 0) {
            memcpy(foundAddress, &address, 5);
            return true;
        }
    }
    return false;
}

void ludevice::saveDetectedDevice(const uint8_t* address) {
    uint64_t addr;
    memcpy(&addr, address, 5);
    
    // Проверяем, не сохраняли ли уже этот адрес
    for (auto& dev : detectedDevices) {
        if (dev == addr) return;
    }
    
    detectedDevices.push_back(addr);
    
    // Здесь можно добавить сохранение в EEPROM или вывод в лог
    printf( "Detected new device: %02X:%02X:%02X:%02X:%02X", 
             address[0], address[1], address[2], address[3], address[4]);
}

void ludevice::setAddress(uint64_t address)
{
    setAddress((uint8_t *)&address);
}

void ludevice::setAddress(uint8_t *address)
{
    uint8_t address_dongle[5];

    // printf("Setting address: %s\r\n", hexa(address, 5));

    memcpy(address_dongle, address, 4);
    address_dongle[0] = 0;

    radio.stopListening();
    radio.openReadingPipe(2, address_dongle);
    radio.openReadingPipe(1, address);
    radio.openWritingPipe(address);
}

bool ludevice::begin()
{
    uint8_t init_status = radio.begin();

    if (init_status == 0 || init_status == 0xff)
    {
        return false;
    }

    aes_base = random(0xfffffff + 1) << 4;

    EEPROM.begin(sizeof(current_channel) + sizeof(rf_address) + sizeof(device_key));
    EEPROM.get(MAC_ADDRESS_EEPROM_ADDRESS + 0, current_channel);
    EEPROM.get(MAC_ADDRESS_EEPROM_ADDRESS + 1, rf_address);
    EEPROM.get(MAC_ADDRESS_EEPROM_ADDRESS + 1 + 5, device_key);

    // Проверка валидности данных EEPROM
    bool eeprom_valid = true;
    for (int i = 0; i < 5; i++) {
        if (rf_address[i] == 0xFF || rf_address[i] == 0x00) {
            eeprom_valid = false;
            break;
        }
    }
    
    if (!eeprom_valid) {
        // Генерируем случайный адрес
        for (int i = 0; i < 5; i++) {
            rf_address[i] = random(256);
        }
        current_channel = channel_tx[0];
    }

    radio.stopListening();
    {
        const uint8_t retryCount = 3;
        const uint8_t retryDelay = 1;

        radio.setAutoAck(true);
        radio.setRetries(retryDelay, retryCount);
        radio.setChannel(current_channel);
        radio.setPayloadSize(PAYLOAD_SIZE);
        radio.enableDynamicPayloads();
        radio.enableAckPayload();
        radio.enableDynamicAck();
        radio.openWritingPipe(PAIRING_MAC_ADDRESS);
        radio.openReadingPipe(1, PAIRING_MAC_ADDRESS);
        changeChannel();
        radio.setDataRate(RF24_2MBPS);
        {
            digitalWrite(DEFAULT_CS_PIN, LOW);
            SPI.transfer(W_REGISTER | (REGISTER_MASK & 0x3));
            SPI.transfer(0x03);
            digitalWrite(DEFAULT_CS_PIN, HIGH);
        }
    }
    radio.stopListening();

    return true;
}

void ludevice::setChecksum(uint8_t *payload, uint8_t len)
{
    uint8_t checksum = 0;

    for (uint8_t i = 0; i < (len - 1); i++)
        checksum += payload[i];

    payload[len - 1] = -checksum;
}

void ludevice::hidpp10(uint8_t *rf_payload, uint8_t payload_size) {
    uint8_t rf_response[22] = {0};
    uint8_t reply = 0;
    const char *name = "default reply";

    // Базовые параметры ответа
    rf_response[0] = rf_payload[0];
    rf_response[1] = 0x40 | rf_payload[1]; // Сохраняем тип отчета
    rf_response[2] = rf_payload[2];
    rf_response[3] = rf_payload[3];
    rf_response[4] = rf_payload[4];

    // Обработка критичных запросов
    if (rf_payload[3] == 0x81) {
        uint32_t addr_param = (rf_payload[4] << 8) + (rf_payload[5]);
        
        if (addr_param == 0xd00) { // HIDPP_REG_BATTERY_MILEAGE
            name = "Battery";
            reply = 10;
            rf_response[5] = 50;  // capacity
            rf_response[6] = 0;
            rf_response[7] = 0;
        }
        else if (addr_param == 0xf101 || addr_param == 0xf102) {
            name = "Firmware";
            reply = 10;
            rf_response[5] = rf_payload[5];
            rf_response[6] = (firmware_version >> (addr_param == 0xf101 ? 24 : 8)) & 0xff;
            rf_response[7] = (firmware_version >> (addr_param == 0xf101 ? 16 : 0)) & 0xff;
        }
    }

    // Обработка по умолчанию для неизвестных пакетов
    if (reply == 0 && (rf_payload[1] == 0x10 || rf_payload[1] == 0x11)) {
        if (rf_payload[1] == 0x10) { // short report
            reply = 10;
            memset(rf_response+5, 0, 4); // Нулевые данные
        } else if (rf_payload[1] == 0x11) { // long report
            reply = 22;
            memset(rf_response+5, 0, 16); // Нулевые данные
        }
    }

    if (reply && (rf_payload[1] == 0x10 || rf_payload[1] == 0x11)) {
        rf_response[1] = (reply == 10) ? 0x50 : 0x51;
        radiowrite(rf_response, reply, name, 1);
    }
}

void ludevice::hidpp20(uint8_t *rf_payload, uint8_t payload_size)
{
    // https://initrd.net/stuff/mousejack/doc/pdf/DEFCON-24-Marc-Newlin-MouseJack-Injecting-Keystrokes-Into-Wireless-Mice.slides.pdf
    // https://drive.google.com/file/d/0B4Pb6jGAmjoKQ3hlZDFxUHVqRkU/view

    // [16.922] 9D:65:CB:58:4D 0040006E52 // keepalive, 110ms interval
    // [16.923] 9D:65:CB:58:4D // ACK
    // [17.015] 9D:65:CB:58:4D 0040006E52 // keepalive, 110ms interval
    // [17.015] 9D:65:CB:58:4D // ACK
    // [17.108] 9D:65:CB:58:4D 0040006E52 // keepalive, 110ms interval
    // [17.108] 9D:65:CB:58:4D // ACK
    // [17.201] 9D:65:CB:58:4D 0040006E52 // keepalive, 110ms interval
    // [17.201] 9D:65:CB:58:4D // ACK
    // [17.294] 9D:65:CB:58:4D 0040006E52 // keepalive, 110ms interval
    // [17.294] 9D:65:CB:58:4D 00:10:4D:00:14:00:00:00:00:8F // ACK payload; requesting HID++ version
    //                         00:10:ce:00:12:3f:13:00:00:be
    //                         00:10:ce:00:12:00:00:00:11:ff
    // [17.302] 9D:65:CB:58:4D 00:51:4D:00:14:04:05:0000000000000000000000000000:45 // response (HID++ 4.5)
    // [17.302] 9D:65:CB:58:4D // ACK
    // [17.387] 9D:65:CB:58:4D 0040006E52 // keepalive, 110ms interval
    // [17.387] 9D:65:CB:58:4D // ACK
    // https://lekensteyn.nl/files/logitech/logitech_hidpp_2.0_specification_draft_2012-06-04.pdf
    // https://github.com/mame82/UnifyingVulnsDisclosureRepo/blob/master/talk/phishbot_2019_redacted3.pdf
    // https://raw.githubusercontent.com/torvalds/linux/master/drivers/hid/hid-logitech-hidpp.c

    // [0] 00 - device index
    // [1] 10 - Report ID
    //          0x10, 7 bytes UNIFYING_RF_REPORT_HIDPP_SHORT, 0x0E = UNIFYING_RF_REPORT_LED
    //          0x11, 20 bytes REPORT_ID_HIDPP_VERY_LONG
    // [2] CE - Device Index / RF prefix
    // [3] 00 - Sub ID
    // ---
    // [4] 12 - Address
    // [5] xx - value 0
    // [6] xx - value 1
    // [7] xx - value 2
    // [8] 00 -
    // [9] xx - checksum

    uint8_t rf_response[22] = {0};
    uint8_t reply = 22;
    const char *name = ""; // исправлено

    rf_response[0] = rf_payload[0];
    rf_response[1] = 0x51;
    rf_response[2] = rf_payload[2];
    rf_response[3] = rf_payload[3];
    rf_response[4] = rf_payload[4];

    uint32_t feature_id = (rf_payload[5] << 8) + (rf_payload[6]);
    // https://lekensteyn.nl/files/logitech/logitech_hidpp_2.0_specification_draft_2012-06-04.pdf
    switch (feature_id)
    {
    default:
        reply = 0;
        break;
    case 0x0000: // root
        name = "root 0x0000";
        reply = 10;
        // HID++ 2.0, 4.5
        // rf_response[3] = 0x0;
        // rf_response[4] = 0x10 + (ack_payload[4] & 0xf);
        // RF rf_response Results start from [4 + 1]
        rf_response[5] = 0x2;
        rf_response[6] = 0x0;
        rf_response[7] = rf_payload[8]; // ping
        rf_response[8] = 0;
        break;
    case 0x0003: //device info

        // request parms starts from ack_payload[4]
        name = "firmware 0x0003";
        // RF rf_response Results start from [4 + 1]
        rf_response[5] = 0x0;
        rf_response[6] = 'a';
        rf_response[7] = 'b';
        rf_response[8] = 'c';
        rf_response[9] = 0x33;
        rf_response[10] = 0x44;
        rf_response[11] = 0x1;
        rf_response[12] = 0x1;
        rf_response[13] = 0x0; //xx
        rf_response[14] = 'K';
        rf_response[15] = 'S';
        rf_response[16] = 'B';

        break;
    case 0x1000: // battery
        name = "battery 0x1000";
        // RF rf_response Results start from [4 + 1]
        rf_response[5] = 80; // BatteryDischargeLevel
        rf_response[6] = 70; // BatteryDischargeNextLevel
        rf_response[7] = 2;  // 0 - charging
        break;
    case 0x1d4b: //wireless device status
        name = "wireless 0x1d4b";
        rf_response[5] = 0;
        rf_response[6] = 0;
        rf_response[7] = 0;
        break;
    }
    if (reply && rf_payload[1] == 0x10)
    {
        if (reply == 10)
            rf_response[1] = 0x50;
        if (reply == 22)
            rf_response[1] = 0x51;
        radiowrite(rf_response, reply, name, 1);
    }
}

void ludevice::loop(void)
{
    if (!is_connected)
        return;

    // Ограничиваем количество обработки за один вызов
    uint8_t max_packets = 1;
    uint8_t processed = 0;
    
    while (radio.available() && processed < max_packets)
    {
        uint8_t *rf_payload;
        uint8_t response_size = read(rf_payload);
        hidpp10(rf_payload, response_size);
        processed++;
    }

    stay_alive_keyboard();
}

void ludevice::stay_alive_keyboard(void)
{
    static uint32_t last_check = 0;
    static uint16_t send_interval = 0;
    const uint32_t now = millis();

    // Проверяем условия обновления не чаще 1 раза в секунду
    if (now - last_check > 1000) {
        last_check = now;
        
        const unsigned long idle_time = idle_timer;
        uint16_t new_keep_alive = keep_alive;

        if (idle_time > 60000) {
            new_keep_alive = 1200;
        } else if (idle_time > 30000) {
            new_keep_alive = 278;
        }

        if (new_keep_alive != keep_alive) {
            update_keep_alive(new_keep_alive, 3, true);
        }

        // Вычисляем интервал отправки
        send_interval = (keep_alive == 278) ? 250 : 
                       (keep_alive == 1200) ? 1100 : keep_alive;
    }

    // Отправка keep-alive
    if (send_alive_timer > send_interval)
    {
        radiowrite_ex(keep_alive_packet, sizeof(keep_alive_packet), "keep-alive", 1, true);
        send_alive_timer = 0;
    }
}

// ludevice.cpp (строка ~505)
void ludevice::stay_alive_mouse(void)
{
    uint8_t retry = 5;
    bool silent = false;
    char buffer[30];

    // Упрощенная логика обновления интервалов
    if (idle_timer > 5000 && keep_alive != 1200) {
        update_keep_alive(1200, retry, silent);
    } else if (idle_timer > 80 && keep_alive != 110) {
        update_keep_alive(110, retry, silent);
    }

    // Вычисление интервала отправки
    uint16_t send_interval = keep_alive;
    if (keep_alive == 110) send_interval = 100;
    else if (keep_alive == 1200) send_interval = 1100;

    if (send_alive_timer > send_interval)
    {
        sprintf(buffer, "%dms keep alive", keep_alive);
        radiowrite_ex(keep_alive_packet, sizeof(keep_alive_packet), buffer, retry, silent);
        send_alive_timer = 0;
    }
}


bool ludevice::update_keep_alive(uint16_t timeout, uint8_t retry, bool silent)
{
    char buffer[30];
    keep_alive_packet[2] = ((timeout & 0xff00) >> 8); // timeout
    keep_alive_packet[3] = ((timeout & 0x00ff));      // timeout
    setChecksum(keep_alive_packet, 5);

    keep_alive_change_packet[3] = ((timeout & 0xff00) >> 8); // timeout
    keep_alive_change_packet[4] = ((timeout & 0x00ff));      // timeout
    setChecksum(keep_alive_change_packet, 10);

    retry = 3;
    sprintf(buffer, "set keep alive to %d ms", timeout);
    if (radiowrite_ex(keep_alive_change_packet, sizeof(keep_alive_change_packet), buffer, retry, silent))
    {
        // uint8_t *response;
        // read(response);
        keep_alive = timeout;
        return true;
    }
    return false;
}

bool ludevice::pair_response(uint8_t *packet, const char *name, uint8_t retry)
{
    while (retry)
    {
        if (!radiowrite(packet, 5, name, 1))
        {
            retry--;
            if (retry == 0)
                return false;
        }
        else
        {
            if (radio.available())
                break;
        }
    }
    return true;
}

int ludevice::pair()
{
    bool passed;
    uint8_t retry = 15;  // Увеличено количество попыток
    uint8_t bis_retry;
    uint8_t response_size;
    uint8_t *response;
    uint8_t prefix;

    is_pairing = true;
    setAddress(PAIRING_MAC_ADDRESS);

    {
        // Добавлена задержка перед началом сопряжения
        delay(50);
        
        // Send REQ1
        prefix = PAIRING_MARKER_PHASE_1;
        pairing_packet_1[0] = prefix;
        pairing_packet_1[3] = rf_address[4];
        pairing_packet_1[4] = rf_address[3];
        pairing_packet_1[5] = rf_address[2];
        pairing_packet_1[6] = rf_address[1];
        pairing_packet_1[7] = rf_address[0];

        if (!radiowrite(pairing_packet_1, 22, "REQ1", retry))
            return -10;

        lock_channel = true;

        memcpy(device_raw_key_material, pairing_packet_1 + LOGITACKER_UNIFYING_PAIRING_RSP1_OFFSET_BASE_ADDR, 4);
        memcpy(device_raw_key_material + 4, pairing_packet_1 + LOGITACKER_UNIFYING_PAIRING_REQ1_OFFSET_DEVICE_WPID, 2);

        // Увеличено количество попыток для BIS1
        pairing_packet_1_bis[0] = prefix;
        pairing_packet_1_bis[3] = pairing_packet_1[3];
        bis_retry = 15;
        while (bis_retry)
        {
            if (radiowrite(pairing_packet_1_bis, sizeof(pairing_packet_1_bis), "BIS1", 1))
            {
                response_size = read(response);
                if (response_size > 0)
                {
                    if (response[0] != prefix)
                    {
                        printf("Wrong prefix\r\n");
                    }
                    else
                        break;
                }
                else
                    printf("Empty response\r\n");
            }
            else
            {
                delay(10);  // Добавлена задержка между попытками
            }
            bis_retry--;
        }
        if (bis_retry == 0)
            return false;

        {
            memcpy(device_raw_key_material + 6, response + LOGITACKER_UNIFYING_PAIRING_RSP1_OFFSET_DONGLE_WPID, 2);
            for (int i = 0; i < 5; i++)
                rf_address[i] = response[(3 + (4 - i))];
            setAddress(rf_address);
        }
    }

    {
        // Добавлена задержка перед REQ2
        delay(20);
        
        // Send REQ2
        prefix = PAIRING_MARKER_PHASE_2;
        pairing_packet_2[0] = prefix;

        nonce = random(0xffffffff);
        pairing_packet_2[3] = ((nonce & 0xff000000) >> 24);
        pairing_packet_2[4] = ((nonce & 0x00ff0000) >> 16);
        pairing_packet_2[5] = ((nonce & 0x0000ff00) >> 8);
        pairing_packet_2[6] = ((nonce & 0x000000ff) >> 0);

        serial = random(0xffffffff);
        pairing_packet_2[7] = ((serial & 0xff000000) >> 24);
        pairing_packet_2[8] = ((serial & 0x00ff0000) >> 16);
        pairing_packet_2[9] = ((serial & 0x0000ff00) >> 8);
        pairing_packet_2[10] = ((serial & 0x000000ff) >> 0);
        if (!radiowrite(pairing_packet_2, 22, "REQ2", retry))
            return false;

        memcpy(device_raw_key_material + 8, pairing_packet_2 + LOGITACKER_UNIFYING_PAIRING_REQ2_OFFSET_DEVICE_NONCE, 4);

        // Увеличено количество попыток для BIS2
        pairing_packet_2_bis[0] = prefix;
        pairing_packet_2_bis[3] = pairing_packet_2[3];
        bis_retry = 15;
        while (bis_retry)
        {
            if (radiowrite(pairing_packet_2_bis, sizeof(pairing_packet_2_bis), "BIS2", 1))
            {
                response_size = read(response);
                if (response_size > 0)
                {
                    if (response[0] != prefix)
                    {
                        printf("Wrong prefix\r\n");
                    }
                    else
                        break;
                }
                else
                    printf("Empty response\r\n");
            }
            else
            {
                delay(10);  // Добавлена задержка между попытками
            }
            bis_retry--;
        }
        if (bis_retry == 0)
            return false;

        memcpy(device_raw_key_material + 12, response + LOGITACKER_UNIFYING_PAIRING_RSP2_OFFSET_DONGLE_NONCE, 4);
    }

    {
        // Добавлена задержка перед REQ3
        delay(20);
        
        prefix = PAIRING_MARKER_PHASE_3;
        pairing_packet_3[0] = prefix;
        pairing_packet_3[4] = strlen(device_name);
        memcpy(pairing_packet_3 + 5, device_name, pairing_packet_3[4]);

        if (!radiowrite(pairing_packet_3, 22, "REQ3", retry))
            return false;

        pairing_packet_3_bis[0] = prefix;
        if (!pair_response(pairing_packet_3_bis, "BIS3", retry))
        {
            printf("BIS3 failed");
        }

        response_size = read(response);
        if (response_size == 0)
        {
            printf("No response\r\n");
            return false;
        }
    }

    {
        // Добавлена задержка перед финальным пакетом
        delay(20);
        
        if (!radiowrite(pairing_packet_4, 10, "Final", retry))
            return false;
    }

#ifdef EEPROM_SUPPORT
    /* Save address to eeprom */
    device_key[2] = device_raw_key_material[0];
    device_key[1] = device_raw_key_material[1] ^ 0xFF;
    device_key[5] = device_raw_key_material[2] ^ 0xFF;
    device_key[3] = device_raw_key_material[3];
    device_key[14] = device_raw_key_material[4];
    device_key[11] = device_raw_key_material[5];
    device_key[9] = device_raw_key_material[6];
    device_key[0] = device_raw_key_material[7];
    device_key[8] = device_raw_key_material[8];
    device_key[6] = device_raw_key_material[9] ^ 0x55;
    device_key[4] = device_raw_key_material[10];
    device_key[15] = device_raw_key_material[11];
    device_key[10] = device_raw_key_material[12] ^ 0xFF;
    device_key[12] = device_raw_key_material[13];
    device_key[7] = device_raw_key_material[14];
    device_key[13] = device_raw_key_material[15] ^ 0x55;

    printf("- Given RF Address:   %s\r\n", hexa(rf_address, 5));
    printf("- Device Key Raw:     %s\r\n", hexs(device_raw_key_material, 16));
    printf("- Device Key Derived: %s\r\n", hexs(device_key, 16));
    printf("- CHANNEL:            %d\r\n", current_channel);

    EEPROM.put(MAC_ADDRESS_EEPROM_ADDRESS + 0, current_channel);
    EEPROM.put(MAC_ADDRESS_EEPROM_ADDRESS + 1, rf_address);
    EEPROM.put(MAC_ADDRESS_EEPROM_ADDRESS + 1 + 5, device_key);
    EEPROM.commit();
#endif

    lock_channel = false;
    AES_init_ctx(&ctx, device_key);
    return true;
}

uint8_t ludevice::read(uint8_t *&packet)
{
    uint8_t packet_size = 22;

    if (radio.available())
    {
        packet = _read_buffer;
        radio.read(packet, packet_size);
        if (1)
        {
            if ((packet[19] == packet[20]) && (packet[20] == packet[21]))
                packet_size = 10;
            if (packet_size == 10 && ((packet[7] == packet[8]) && (packet[8] == packet[9])))
                packet_size = 5;
        }
        if (packet[1] != 0xe)
        {
            // printf("IN [%2d]: %2d                   ", packet_size, current_channel);
            printf("IN [%2d]:                  %2d   ", packet_size, current_channel);
            printf("%s\r\n", hexs(packet, packet_size));
        }
        return packet_size;
    }
    return 0;
}

bool ludevice::radiowrite(uint8_t *packet, uint8_t packet_size, const char *name, uint8_t retry)
{
    return radiowrite_ex(packet, packet_size, name, retry, false);
}

bool ludevice::radiowrite_ex(uint8_t *packet, uint8_t packet_size, const char *name, uint8_t retry, bool silent)
{
    bool success = false;
    uint8_t attempts = retry;

    // Предварительный расчет контрольной суммы
    setChecksum(packet, packet_size);

    for (uint8_t i = 0; i < attempts; i++) {
        if (radio.write(packet, packet_size)) {
            success = true;
            break;
        }
        
        if (!lock_channel) {
            changeChannel();
            delay(1);  // Минимальная задержка
        }
    }

    // Логирование только при ошибках или явном запросе
    if (!silent || !success) {
        printf("OUT[%2d]: %s %2d %c %s", 
               packet_size, 
               hexa(rf_address, 5), 
               current_channel,
               success ? ' ' : '!',
               hexs(packet, packet_size));
        if (name) printf(" - %s", name);
        printf("\r\n");
    }

    return success;
}

void ludevice::changeChannel()
{
    if (is_pairing) {
        channel_pairing_id = (channel_pairing_id + 1) % CHANNEL_PAIRING_COUNT;
        current_channel = channel_pairing[channel_pairing_id];
    } else {        
        channel_tx_id = (channel_tx_id + 1) % CHANNEL_TX_COUNT;
        current_channel = channel_tx[channel_tx_id];
    }
    
    radio.setChannel(current_channel);
}

bool ludevice::reconnect()
{
    return register_device();
}

bool ludevice::register_device()
{
#ifndef EEPROM_SUPPORT
#warning "EEPROM support is not enabled"
    return false;
#else
    uint8_t prefix;
    uint8_t *response;
    bool failed;
    uint8_t packet_size;

    is_pairing = false;

    EEPROM.get(MAC_ADDRESS_EEPROM_ADDRESS + 1, rf_address);

    prefix = rf_address[0];

    rf_address[0] = 0;
    setAddress(rf_address);

    register1[0] = prefix;
    register1[2] = prefix;
    packet_size = sizeof(register1);
    if (!radiowrite(register1, packet_size, "register1", 5))
        return false;

    register2[0] = prefix;
    packet_size = sizeof(register2);
    if (!radiowrite(register2, packet_size, "register2", 5))
        return false;

    rf_address[0] = prefix;
    setAddress(rf_address);

    hello[2] = prefix;
    packet_size = sizeof(hello);
    if (!radiowrite(hello, packet_size, "hello", 5))
        return false;

    if (!update_keep_alive(110, 5, false))
        return false;

    is_connected = true;
    AES_init_ctx(&ctx, device_key);
    return true;
#endif
}

void ludevice::move(uint16_t x_move, uint16_t y_move)
{
    move(x_move, y_move, false, false);
}

void ludevice::move(uint16_t x_move, uint16_t y_move, bool leftClick, bool rightClick)
{
    move(x_move, y_move, 0, 0, leftClick, rightClick);
}

void ludevice::move(uint16_t x_move, uint16_t y_move, uint8_t scroll_v, uint8_t scroll_h)
{
    move(x_move, y_move, scroll_v, scroll_h, false, false);
}

void ludevice::move(uint16_t x_move, uint16_t y_move, uint8_t scroll_v, uint8_t scroll_h, bool leftClick, bool rightClick)
{
    idle_timer = 0;

    uint8_t mouse_payload[] = {0x00, 0xC2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    uint32_t cursor_velocity;

    cursor_velocity = ((uint32_t)y_move & 0xFFF) << 12 | (x_move & 0xFFF);

    memcpy(mouse_payload + 4, &cursor_velocity, 3);

    if (leftClick)
        mouse_payload[2] = 1;

    if (rightClick)
        mouse_payload[2] |= 2; //1 << 1;

    mouse_payload[7] = scroll_v;
    mouse_payload[8] = scroll_h;

    setChecksum(mouse_payload, 10);
    radio.write(mouse_payload, 10, 0);

    radio.flush_rx();
}

void ludevice::click(bool leftClick, bool rightClick)
{
    move(0, 0, leftClick, rightClick);
}

void ludevice::scroll(uint8_t scroll_v, uint8_t scroll_h)
{
    move(0, 0, scroll_v, scroll_h, false, false);
}

void ludevice::scroll(uint8_t scroll_v)
{
    scroll(scroll_v, 0);
}

void ludevice::wipe_pairing(void)
{
    uint8_t erase[15 + 6] = {0};
    EEPROM.put(MAC_ADDRESS_EEPROM_ADDRESS, erase);
    EEPROM.commit();
}

char *ludevice::hexs_ex(uint8_t *x, uint8_t length, bool reverse, char separator)
{
    char *ptr = _hexs;
    for (int i = 0; i < length; i++)
    {
        uint8_t index = reverse ? (length - 1 - i) : i;
        ptr += sprintf(ptr, "%02X", x[index]);
        
        if (i < length - 1) {
            *ptr++ = separator;
        }
    }
    *ptr = '\0';
    return _hexs;
}

char *ludevice::hexa(uint8_t *x, uint8_t length)
{
    return hexs_ex(x, length, true, ':');
}

char *ludevice::hexs(uint8_t *x, uint8_t length)
{
    return hexs_ex(x, length, false, ' ');
}

void ludevice::typep(uint8_t scan1, uint8_t scan2, uint8_t scan3, uint8_t scan4, uint8_t scan5, uint8_t scan6)
{
    idle_timer = 0;

    uint8_t key_payload[] = {
        0x00,
        LOGITACKER_DEVICE_REPORT_TYPES_KEYBOARD | LOGITACKER_DEVICE_REPORT_TYPES_KEEP_ALIVE | 0x80,
        0x00, // [2] modifier
        0x00,
        0x00, // [4] scancode
        0x00, 0x00, 0x00, 0x00,
        0x00};

    key_payload[3] = 0x4;  // send 'a'
    key_payload[4] = 0x37; // send '.'
    key_payload[3] = scan1;
    key_payload[4] = scan2;
    key_payload[5] = scan3;
    key_payload[6] = scan4;
    key_payload[7] = scan5;
    key_payload[8] = scan6;
    setChecksum(key_payload, 10);
    radiowrite(key_payload, 10, "plain key", 5);
    return;
    while (1)
    {
        status = failed;
        if (radio.write(key_payload, 10, 0))
            status = success;

        printf("- plain keyboard: %s, %s\r\n", hexs(key_payload, 10), status);
        break;
    }
}

void ludevice::typem(uint16_t scan1, uint16_t scan2)
{
    idle_timer = 0;

    uint8_t key_payload[] = {
        0x00, 0xC3,
        0x00, // [2] scancode
        0x00,
        0x00, // [4] scancode
        0x00,
        0x00, 0x00, 0x00, // unused
        0x00};

    // PAGE UP (0x4B)
    // PAGE DOWN (0x4E)
    // ESC (0x29)
    // F5 (0x3E)
    // PERIOD (0x37)
    // B (0x05)

    // 00 C3 E2 00 00 00 00 00 00 5B (10 bytes) // toggle mute

    key_payload[2] = ((scan1 & 0x00ff) >> 0);
    key_payload[3] = ((scan1 & 0xff00) >> 8);
    key_payload[4] = ((scan2 & 0x00ff) >> 0);
    key_payload[5] = ((scan2 & 0xff00) >> 8);

    radiowrite(key_payload, 10, "media key", 5);
}

void ludevice::typee(uint8_t scan1, uint8_t scan2, uint8_t scan3, uint8_t scan4, uint8_t scan5, uint8_t scan6)
{
    uint32_t temp_counter;
    bool ret;

    uint8_t rf_frame[22] = {0};
    uint8_t plain_payload[8] = {0};

    idle_timer = 0;

    plain_payload[1] = scan1;
    plain_payload[2] = scan2;
    plain_payload[3] = scan3;
    plain_payload[4] = scan4;
    plain_payload[5] = scan5;
    plain_payload[6] = scan6;

    temp_counter = (aes_counter & 0xf);
    temp_counter = aes_base + (aes_counter & 0xf);
    logitacker_unifying_crypto_encrypt_keyboard_frame(rf_frame, plain_payload, temp_counter);

    if (scan1 == 0 && scan2 == 0 && scan3 == 0 && scan4 == 0 && scan5 == 0 && scan6 == 0)
        ret = radiowrite(rf_frame, 22, "encrypted key up", 1);
    else
        ret = radiowrite(rf_frame, 22, "encrypted key down", 1);

    if (ret)
        // aes_counter++;
        aes_base++;
}

void ludevice::update_little_known_secret_counter(uint8_t *counter_bytes)
{
    memcpy(little_known_secret + 7, counter_bytes, 4);
}

void ludevice::logitacker_unifying_crypto_calculate_frame_key(uint8_t *ciphertext, uint8_t *counter_bytes, bool silent)
{
    if (!silent)
        printf("1. last plain l_k_s:                 %s\r\n", hexs(little_known_secret, 16));
    update_little_known_secret_counter(counter_bytes); // copy counter_bytes into little_known_secret
    if (!silent)
        printf("2. plain l_k_s+counter:              %s\r\n", hexs(little_known_secret, 16));

    if (!silent)
        printf("3. device_key:                       %s\r\n", hexs(device_key, 16));
    memcpy(ciphertext, little_known_secret, 16); // copy little_known_secret into ciphertext
    AES_ECB_encrypt(&ctx, ciphertext);           // encrypt ciphertext

    if (!silent)
        printf("4. frame_key:                        %s\r\n", hexs(ciphertext, 16));
    return;
}

void ludevice::logitacker_unifying_crypto_encrypt_keyboard_frame(uint8_t *rf_frame, uint8_t *plain_payload, uint32_t counter)
{
    rf_frame[1] = LOGITACKER_DEVICE_REPORT_TYPES_ENCRYPTED_KEYBOARD | LOGITACKER_DEVICE_REPORT_TYPES_KEEP_ALIVE | 0x80;
    bool silent = false;

    uint8_t counter_bytes[4] = {0};
    // K800
    // counter_bytes[3] = (uint8_t)((counter & 0xff000000) >> 24);
    // counter_bytes[2] = (uint8_t)((counter & 0x00ff0000) >> 16);
    // counter_bytes[1] = (uint8_t)((counter & 0x0000ff00) >> 8);
    // counter_bytes[0] = (uint8_t)((counter & 0x000000ff) >> 0);

    // K270
    counter_bytes[0] = (uint8_t)((counter & 0xff000000) >> 24);
    counter_bytes[1] = (uint8_t)((counter & 0x00ff0000) >> 16);
    counter_bytes[2] = (uint8_t)((counter & 0x0000ff00) >> 8);
    counter_bytes[3] = (uint8_t)((counter & 0x000000ff) >> 0);
    memcpy(rf_frame + 10, counter_bytes, 4);

    uint8_t frame_key[16] = {0};
    logitacker_unifying_crypto_calculate_frame_key(frame_key, counter_bytes, silent);

    plain_payload[7] = 0xC9;
    memcpy(rf_frame + 2, plain_payload, 8);

    if (!silent)
        printf("5. plain rf_frame:             %s\r\n", hexs(rf_frame, 22));

    for (int i = 0; i < 8; i++)
        rf_frame[2 + i] ^= frame_key[i];

    setChecksum(rf_frame, 22);

    if (!silent)
        printf("6. encrypted rf_frame:         %s\r\n", hexs(rf_frame, 22));
}
