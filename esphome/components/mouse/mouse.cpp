#include "mouse.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"
#include <EEPROM.h>

namespace esphome {
namespace mouse {

// Конфигурация RF24
static const uint64_t BASE_ADDRESS = 0xBB0ADCA575LL;
static const uint8_t CHANNELS[] = {5, 8, 11, 14, 17, 20, 23, 26, 29, 32, 35, 38, 41, 44, 47, 50, 53, 56, 59, 62, 65, 68, 71, 74, 77};
static const uint8_t CHANNEL_COUNT = sizeof(CHANNELS) / sizeof(CHANNELS[0]);

LogitechUnifying::LogitechUnifying(uint8_t ce_pin, uint8_t cs_pin) : radio(ce_pin, cs_pin) {}

bool LogitechUnifying::begin() {
    if (!radio.begin()) return false;
    
    radio.setDataRate(RF24_2MBPS);
    radio.setPALevel(RF24_PA_MAX);
    radio.setAutoAck(true);
    radio.enableDynamicPayloads();
    radio.setRetries(5, 15);
    radio.setChannel(CHANNELS[0]);
    
    load_from_eeprom();
    return true;
}

void LogitechUnifying::save_to_eeprom() {
    EEPROM.put(0, rf_address);
    EEPROM.put(sizeof(rf_address), device_key);
    EEPROM.commit();
}

void LogitechUnifying::load_from_eeprom() {
    EEPROM.get(0, rf_address);
    EEPROM.get(sizeof(rf_address), device_key);
}

bool LogitechUnifying::pair() {
    radio.stopListening();
    radio.setChannel(CHANNELS[0]);
    
    // Генерация случайного адреса
    for (int i = 0; i < 5; i++) {
        rf_address[i] = random(256);
    }
    
    if (send_pairing_packet()) {
        save_to_eeprom();
        is_paired = true;
        return true;
    }
    return false;
}

bool LogitechUnifying::send_pairing_packet() {
    uint8_t packet[22] = {
        0xF0, 0x4F, 0x01,               // Заголовок
        rf_address[4], rf_address[3], rf_address[2], rf_address[1], rf_address[0], // Адрес
        0x14, 0x17, 0x10,               // WPID и протокол
        0x02, 0x0F,                     // Тип устройства (мышь) и возможности
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1A, // Доп. данные
        0xEC                            // Контрольная сумма
    };
    
    // Расчет контрольной суммы
    uint8_t sum = 0;
    for (int i = 0; i < 21; i++) sum += packet[i];
    packet[21] = ~sum + 1;
    
    radio.openWritingPipe(BASE_ADDRESS);
    return radio.write(packet, sizeof(packet));
}

bool LogitechUnifying::reconnect() {
    if (!is_paired) return false;
    
    radio.openWritingPipe(*(uint64_t*)rf_address);
    radio.setChannel(current_channel);
    return true;
}

void LogitechUnifying::move(int16_t x, int16_t y) {
    uint8_t packet[10] = {
        rf_address[0], 0xC2, 0x00,
        static_cast<uint8_t>(x & 0xFF), static_cast<uint8_t>((x >> 8) & 0x0F),
        static_cast<uint8_t>(y & 0xFF), static_cast<uint8_t>((y >> 8) & 0x0F),
        0x00, 0x00, 0x00, 0x00
    };
    
    // Расчет контрольной суммы
    uint8_t sum = 0;
    for (int i = 0; i < 9; i++) sum += packet[i];
    packet[9] = ~sum + 1;
    
    radio.write(packet, sizeof(packet));
}

bool LogitechUnifying::is_other_device_active() {
    if (millis() - last_channel_scan < 5000) 
        return false;
    
    last_channel_scan = millis();
    uint8_t original_channel = current_channel;
    bool activity_detected = false;
    
    for (uint8_t i = 0; i < CHANNEL_COUNT; i++) {
        radio.setChannel(CHANNELS[i]);
        radio.startListening();
        delay(5);
        
        if (radio.testRPD()) {
            activity_detected = true;
            break;
        }
    }
    
    radio.setChannel(original_channel);
    radio.stopListening();
    return activity_detected;
}

void LogitechUnifying::loop() {
    static uint32_t last_channel_change = 0;
    if (millis() - last_channel_change > 5000) {
        current_channel = CHANNELS[random(CHANNEL_COUNT)];
        radio.setChannel(current_channel);
        last_channel_change = millis();
    }
}

void Mouse::setup() {
    EEPROM.begin(512);
    unifying_ = make_unique<LogitechUnifying>(ce_pin_, cs_pin_);
    
    if (!unifying_->begin()) {
        ESP_LOGE(TAG, "RF24 initialization failed!");
        return;
    }
    
    if (!unifying_->reconnect() && !unifying_->pair()) {
        ESP_LOGE(TAG, "Pairing failed!");
    }
    
    publish_state(true);
}

void Mouse::update() {
    if (!enabled_) return;
    
    unifying_->loop();
    
    if (unifying_->is_other_device_active()) {
        ESP_LOGW(TAG, "Real mouse detected! Suspending emulation");
        return;
    }
    
    if (millis() - last_move_ > 20) {
        move_in_circle();
        last_move_ = millis();
    }
}

void Mouse::move_in_circle() {
    const float radius = 10.0f;
    const float speed = 0.1f;
    
    angle_ += speed;
    if (angle_ > 2 * M_PI) angle_ -= 2 * M_PI;
    
    int16_t x = static_cast<int16_t>(radius * cos(angle_));
    int16_t y = static_cast<int16_t>(radius * sin(angle_));
    
    unifying_->move(x, y);
}

void Mouse::write_state(bool state) {
    enabled_ = state;
    publish_state(state);
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
