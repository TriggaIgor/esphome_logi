#include "mouse.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"
#include <EEPROM.h>

namespace esphome {
namespace mouse {

const char *const Mouse::TAG = "mouse";

// Конфигурация RF24
static const uint64_t BASE_ADDRESS = 0xBB0ADCA575LL;
static const uint8_t CHANNELS[] = {5, 8, 11, 14, 17, 20, 23, 26, 29, 32, 35, 38, 41, 44, 47, 50, 53, 56, 59, 62, 65, 68, 71, 74, 77};
static const uint8_t CHANNEL_COUNT = sizeof(CHANNELS) / sizeof(CHANNELS[0]);

LogitechUnifying::LogitechUnifying(uint8_t ce_pin, uint8_t cs_pin) : radio(ce_pin, cs_pin) {}

bool LogitechUnifying::begin() {
    if (!radio.begin()) {
        ESP_LOGE(TAG, "RF24 hardware not responding!");
        return false;
    }
    
    // Настройка параметров радио
    radio.setDataRate(RF24_2MBPS);
    radio.setPALevel(RF24_PA_MAX);
    radio.setAutoAck(true);
    radio.enableDynamicPayloads();
    radio.setRetries(3, 5);  // Уменьшено время ретраев
    radio.setChannel(CHANNELS[0]);
    
    ESP_LOGI(TAG, "RF24 initialized: DataRate=2MBPS, PA=MAX");
    
    // Инициализация EEPROM
    EEPROM.begin(512);
    load_from_eeprom();
    
    return true;
}

void LogitechUnifying::save_to_eeprom() {
    EEPROM.put(0, rf_address);
    EEPROM.put(sizeof(rf_address), device_key);
    if (EEPROM.commit()) {
        ESP_LOGI(TAG, "Settings saved to EEPROM");
    } else {
        ESP_LOGE(TAG, "EEPROM commit failed");
    }
}

void LogitechUnifying::load_from_eeprom() {
    EEPROM.get(0, rf_address);
    EEPROM.get(sizeof(rf_address), device_key);
    
    // Проверка валидности данных
    bool valid = true;
    for (int i = 0; i < 5; i++) {
        if (rf_address[i] == 0xFF || rf_address[i] == 0x00) {
            valid = false;
            break;
        }
    }
    
    if (valid) {
        ESP_LOGI(TAG, "Loaded from EEPROM: RF_Address=%02X:%02X:%02X:%02X:%02X", 
                rf_address[4], rf_address[3], rf_address[2], rf_address[1], rf_address[0]);
    } else {
        ESP_LOGW(TAG, "No valid settings in EEPROM, need pairing");
        memset(rf_address, 0, sizeof(rf_address));
    }
}

bool LogitechUnifying::pair() {
    ESP_LOGI(TAG, "Starting pairing procedure...");
    
    radio.stopListening();
    radio.setChannel(CHANNELS[0]);
    
    // Генерация случайного адреса
    for (int i = 0; i < 5; i++) {
        rf_address[i] = random(256);
    }
    ESP_LOGD(TAG, "Generated new RF address: %02X:%02X:%02X:%02X:%02X",
            rf_address[4], rf_address[3], rf_address[2], rf_address[1], rf_address[0]);
    
    // Попытка сопряжения (3 попытки)
    for (int attempt = 1; attempt <= 3; attempt++) {
        ESP_LOGD(TAG, "Pairing attempt %d/3", attempt);
        
        if (send_pairing_packet()) {
            save_to_eeprom();
            is_paired = true;
            ESP_LOGI(TAG, "Pairing successful!");
            return true;
        }
        delay(100); // Пауза между попытками
    }
    
    ESP_LOGE(TAG, "Pairing failed after 3 attempts");
    return false;
}

bool LogitechUnifying::send_pairing_packet() {
    uint8_t packet[22] = {
        0xF0, 0x4F, 0x01,               // Заголовок
        rf_address[4], rf_address[3], rf_address[2], rf_address[1], rf_address[0], // Адрес
        0x14, 0x17, 0x10,               // WPID и протокол
        0x02, 0x0F,                     // Тип устройства (мышь) и возможности
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1A, // Доп. данные
        0x00 // Контрольная сумма (временная)
    };
    
    // Расчет контрольной суммы
    uint8_t sum = 0;
    for (int i = 0; i < 21; i++) sum += packet[i];
    packet[21] = ~sum + 1;
    
    radio.openWritingPipe(BASE_ADDRESS);
    radio.stopListening();
    
    ESP_LOGD(TAG, "Sending pairing packet...");
    bool result = radio.write(packet, sizeof(packet));
    
    if (result) {
        ESP_LOGD(TAG, "Pairing packet sent successfully");
    } else {
        ESP_LOGD(TAG, "Failed to send pairing packet");
    }
    
    return result;
}

bool LogitechUnifying::reconnect() {
    if (!is_paired) {
        ESP_LOGW(TAG, "Not paired, cannot reconnect");
        return false;
    }
    
    // Установка адреса
    uint64_t address = 0;
    for (int i = 0; i < 5; i++) {
        address |= static_cast<uint64_t>(rf_address[i]) << (i * 8);
    }
    
    radio.openWritingPipe(address);
    radio.setChannel(CHANNELS[random(CHANNEL_COUNT)]);
    radio.stopListening();
    
    ESP_LOGI(TAG, "Reconnected to %02X:%02X:%02X:%02X:%02X",
            rf_address[4], rf_address[3], rf_address[2], rf_address[1], rf_address[0]);
            
    return true;
}

void LogitechUnifying::move(int16_t x, int16_t y) {
    // Ограничение значений (-2048 до 2047)
    x = std::max(std::min(x, 2047), -2048);
    y = std::max(std::min(y, 2047), -2048);
    
    uint8_t packet[10] = {
        rf_address[0], 
        0xC2, 
        0x00,  // Кнопки
        static_cast<uint8_t>(x & 0xFF), 
        static_cast<uint8_t>((x >> 8) & 0x0F) | ((x < 0) ? 0x40 : 0),
        static_cast<uint8_t>(y & 0xFF), 
        static_cast<uint8_t>((y >> 8) & 0x0F) | ((y < 0) ? 0x40 : 0),
        0x00,  // Вертикальное колесо
        0x00   // Горизонтальное колесо
    };
    
    // Расчет контрольной суммы
    uint8_t sum = 0;
    for (int i = 0; i < 9; i++) sum += packet[i];
    packet[9] = ~sum + 1;
    
    if (!radio.write(packet, sizeof(packet))) {
        ESP_LOGW(TAG, "Failed to send move packet");
    }
}

bool LogitechUnifying::is_other_device_active() {
    // Упрощенная реализация для тестирования
    return false;
}

void LogitechUnifying::loop() {
    static uint32_t last_channel_change = 0;
    if (millis() - last_channel_change > 5000) {
        current_channel = CHANNELS[random(CHANNEL_COUNT)];
        radio.setChannel(current_channel);
        last_channel_change = millis();
        ESP_LOGD(TAG, "Changed channel to %d", current_channel);
    }
}

void Mouse::setup() {
    ESP_LOGI(TAG, "Setting up Logitech Unifying Mouse");
    
    unifying_ = make_unique<LogitechUnifying>(ce_pin_, cs_pin_);
    
    if (!unifying_->begin()) {
        ESP_LOGE(TAG, "RF24 initialization failed!");
        return;
    }
    
    // Попытка подключения (3 попытки)
    bool connected = false;
    for (int i = 0; i < 3; i++) {
        if (unifying_->reconnect()) {
            connected = true;
            break;
        }
        delay(100);
    }
    
    if (!connected) {
        ESP_LOGW(TAG, "Reconnect failed, trying to pair...");
        if (!unifying_->pair()) {
            ESP_LOGE(TAG, "Pairing failed!");
            return;
        }
    }
    
    publish_state(true);
    ESP_LOGI(TAG, "Mouse initialized successfully");
}

void Mouse::update() {
    if (!enabled_) return;
    
    unifying_->loop();
    
    if (millis() - last_move_ > 20) {
        move_in_circle();
        last_move_ = millis();
    }
}

void Mouse::move_in_circle() {
    const float radius = 20.0f;
    const float speed = 0.2f;
    
    angle_ += speed;
    if (angle_ > 2 * M_PI) angle_ -= 2 * M_PI;
    
    int16_t x = static_cast<int16_t>(radius * cos(angle_));
    int16_t y = static_cast<int16_t>(radius * sin(angle_));
    
    ESP_LOGD(TAG, "Moving: X=%d, Y=%d", x, y);
    unifying_->move(x, y);
}

void Mouse::write_state(bool state) {
    enabled_ = state;
    publish_state(state);
    ESP_LOGI(TAG, "Mouse %s", state ? "enabled" : "disabled");
}

void Mouse::dump_config() {
    ESP_LOGCONFIG(TAG, "Logitech Unifying Mouse:");
    ESP_LOGCONFIG(TAG, "  CE Pin: %d", ce_pin_);
    ESP_LOGCONFIG(TAG, "  CS Pin: %d", cs_pin_);
    ESP_LOGCONFIG(TAG, "  Base Speed: %.1f", base_speed_);
    ESP_LOGCONFIG(TAG, "  Jitter Amount: %.2f", jitter_amount_);
    ESP_LOGCONFIG(TAG, "  Movement Speed: %.2f px/ms", movement_speed_);
    ESP_LOGCONFIG(TAG, "  Max Speed: %.2f px/ms", max_speed_);
    ESP_LOGCONFIG(TAG, "  Acceleration: %.4f", acceleration_rate_);
    ESP_LOGCONFIG(TAG, "  Deceleration: %.4f", deceleration_rate_);
    ESP_LOGCONFIG(TAG, "  Random Delay: %d ms", random_delay_);
}

}  // namespace mouse
}  // namespace esphome
