#include "mouse.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"
#include <EEPROM.h>

namespace esphome {
namespace mouse {

const char *const LogitechUnifying::TAG = "unifying";
const char *const Mouse::TAG = "mouse";

static const uint64_t BASE_ADDRESS = 0xBB0ADCA575LL;
static const uint8_t CHANNELS[] = {5, 8, 11, 14, 17, 20, 23, 26, 29, 32, 35, 38, 41, 44, 47, 50, 53, 56, 59, 62, 65, 68, 71, 74, 77};
static const uint8_t CHANNEL_COUNT = sizeof(CHANNELS) / sizeof(CHANNELS[0]);

LogitechUnifying::LogitechUnifying(uint8_t ce_pin, uint8_t cs_pin) : radio(ce_pin, cs_pin) {}

bool LogitechUnifying::begin() {
    if (!radio.begin()) {
        ESP_LOGE(TAG, "RF24 hardware not responding!");
        return false;
    }
    
    radio.setDataRate(RF24_2MBPS);
    radio.setPALevel(RF24_PA_MAX);
    radio.setAutoAck(true);
    radio.enableDynamicPayloads();
    radio.setRetries(3, 5);
    radio.setChannel(CHANNELS[0]);
    
    ESP_LOGI(TAG, "RF24 initialized");
    
    EEPROM.begin(512);
    load_from_eeprom();
    
    return true;
}

void LogitechUnifying::save_to_eeprom() {
    EEPROM.put(0, rf_address);
    // device_key больше не используется, но сохраняем для совместимости
    EEPROM.put(sizeof(rf_address), device_key);
    if (EEPROM.commit()) {
        ESP_LOGI(TAG, "Settings saved");
    } else {
        ESP_LOGE(TAG, "EEPROM commit failed");
    }
}

void LogitechUnifying::load_from_eeprom() {
    EEPROM.get(0, rf_address);
    // device_key больше не используется, но загружаем для совместимости
    EEPROM.get(sizeof(rf_address), device_key);
    
    bool valid = true;
    for (int i = 0; i < 5; i++) {
        if (rf_address[i] == 0xFF || rf_address[i] == 0x00) {
            valid = false;
            break;
        }
    }
    
    if (valid) {
        ESP_LOGI(TAG, "Loaded from EEPROM");
    } else {
        ESP_LOGW(TAG, "No valid settings");
        memset(rf_address, 0, sizeof(rf_address));
    }
}

bool LogitechUnifying::pair() {
    ESP_LOGI(TAG, "Starting pairing...");
    
    radio.stopListening();
    radio.setChannel(CHANNELS[0]);
    
    // Генерация случайного MAC-адреса
    uint32_t seed = micros();
    for (int i = 0; i < 5; i++) {
        seed = seed * 1103515245 + 12345;
        rf_address[i] = (seed >> 16) & 0xFF;
    }
    
    for (int attempt = 1; attempt <= 3; attempt++) {
        ESP_LOGD(TAG, "Attempt %d/3", attempt);
        
        if (send_pairing_packet()) {
            save_to_eeprom();
            is_paired = true;
            ESP_LOGI(TAG, "Pairing successful");
            return true;
        }
        delay(100);
    }
    
    ESP_LOGE(TAG, "Pairing failed");
    return false;
}

bool LogitechUnifying::send_pairing_packet() {
    uint8_t packet[22] = {
        0xF0, 0x4F, 0x01,
        rf_address[4], rf_address[3], rf_address[2], rf_address[1], rf_address[0],
        0x14, 0x17, 0x10,
        0x02, 0x0F,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1A,
        0x00
    };
    
    // Расчет контрольной суммы
    uint8_t sum = 0;
    for (int i = 0; i < 21; i++) sum += packet[i];
    packet[21] = ~sum + 1;
    
    radio.openWritingPipe(BASE_ADDRESS);
    return radio.write(packet, sizeof(packet));
}

bool LogitechUnifying::reconnect() {
    if (!is_paired) {
        ESP_LOGW(TAG, "Not paired");
        return false;
    }
    
    uint64_t address = 0;
    for (int i = 0; i < 5; i++) {
        address |= static_cast<uint64_t>(rf_address[i]) << (i * 8);
    }
    
    // Выбор случайного канала
    uint8_t random_index = (micros() >> 4) % CHANNEL_COUNT;
    radio.openWritingPipe(address);
    radio.setChannel(CHANNELS[random_index]);
    radio.stopListening();
    
    ESP_LOGI(TAG, "Reconnected");
    return true;
}

void LogitechUnifying::move(int16_t x, int16_t y) {
    // Ограничение значений
    if (x > 2047) x = 2047;
    else if (x < -2048) x = -2048;
    if (y > 2047) y = 2047;
    else if (y < -2048) y = -2048;
    
    uint8_t x_sign = (x < 0) ? 0x40 : 0;
    uint8_t y_sign = (y < 0) ? 0x40 : 0;
    
    uint8_t packet[10] = {
        rf_address[0], 
        0xC2, 
        0x00,
        static_cast<uint8_t>(x & 0xFF), 
        static_cast<uint8_t>((x >> 8) & 0x0F) | x_sign,
        static_cast<uint8_t>(y & 0xFF), 
        static_cast<uint8_t>((y >> 8) & 0x0F) | y_sign,
        0x00,
        0x00
    };
    
    // Расчет контрольной суммы
    uint8_t sum = 0;
    for (int i = 0; i < 9; i++) sum += packet[i];
    packet[9] = ~sum + 1;
    
    if (!radio.write(packet, sizeof(packet))) {
        ESP_LOGW(TAG, "Move failed");
    }
}

bool LogitechUnifying::is_other_device_active() {
    return false; // Упрощенная реализация
}

void LogitechUnifying::loop() {
    static uint32_t last_channel_change = 0;
    if (millis() - last_channel_change > 5000) {
        uint8_t random_index = (micros() >> 4) % CHANNEL_COUNT;
        current_channel = CHANNELS[random_index];
        radio.setChannel(current_channel);
        last_channel_change = millis();
        ESP_LOGD(TAG, "Channel changed");
    }
}

void Mouse::setup() {
    ESP_LOGI(TAG, "Setup started");
    
    unifying_ = make_unique<LogitechUnifying>(ce_pin_, cs_pin_);
    
    if (!unifying_->begin()) {
        ESP_LOGE(TAG, "RF24 init failed");
        return;
    }
    
    bool connected = false;
    for (int i = 0; i < 3; i++) {
        if (unifying_->reconnect()) {
            connected = true;
            break;
        }
        delay(100);
    }
    
    if (!connected && !unifying_->pair()) {
        ESP_LOGE(TAG, "Connection failed");
        return;
    }
    
    publish_state(true);
    ESP_LOGI(TAG, "Setup complete");
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
    const float pi = 3.14159265358979323846f;
    
    angle_ += speed;
    if (angle_ > 2 * pi) angle_ -= 2 * pi;
    
    int16_t x = static_cast<int16_t>(radius * cos(angle_));
    int16_t y = static_cast<int16_t>(radius * sin(angle_));
    
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
    ESP_LOGCONFIG(TAG, "  Movement Speed: %.2f", movement_speed_);
    ESP_LOGCONFIG(TAG, "  Max Speed: %.2f", max_speed_);
    ESP_LOGCONFIG(TAG, "  Acceleration: %.4f", acceleration_rate_);
    ESP_LOGCONFIG(TAG, "  Deceleration: %.4f", deceleration_rate_);
    ESP_LOGCONFIG(TAG, "  Random Delay: %d", random_delay_);
}

}  // namespace mouse
}  // namespace esphome
