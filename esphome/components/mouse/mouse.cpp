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

// =====================================================
// Реализация AES (минимальная версия для ECB шифрования)
// =====================================================

static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

static const uint8_t Rcon[11] = {
    0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

#define Nb 4
#define Nk 4
#define Nr 10

void LogitechUnifying::AES_init_ctx(struct AES_ctx *ctx, const uint8_t *key) {
    uint8_t i, j;
    uint8_t *RoundKey = ctx->RoundKey;
    
    for (i = 0; i < Nk; i++) {
        RoundKey[(i * 4) + 0] = key[(i * 4) + 0];
        RoundKey[(i * 4) + 1] = key[(i * 4) + 1];
        RoundKey[(i * 4) + 2] = key[(i * 4) + 2];
        RoundKey[(i * 4) + 3] = key[(i * 4) + 3];
    }

    for (i = Nk; i < Nb * (Nr + 1); i++) {
        uint8_t temp[4];
        temp[0] = RoundKey[(i-1)*4+0];
        temp[1] = RoundKey[(i-1)*4+1];
        temp[2] = RoundKey[(i-1)*4+2];
        temp[3] = RoundKey[(i-1)*4+3];

        if (i % Nk == 0) {
            uint8_t t = temp[0];
            temp[0] = temp[1];
            temp[1] = temp[2];
            temp[2] = temp[3];
            temp[3] = t;

            temp[0] = sbox[temp[0]];
            temp[1] = sbox[temp[1]];
            temp[2] = sbox[temp[2]];
            temp[3] = sbox[temp[3]];

            temp[0] ^= Rcon[i/Nk];
        }

        for (j = 0; j < 4; j++) {
            RoundKey[i*4+j] = RoundKey[(i-Nk)*4+j] ^ temp[j];
        }
    }
}

static void AddRoundKey(uint8_t round, uint8_t *state, const uint8_t *RoundKey) {
    for (uint8_t i = 0; i < 4; i++) {
        for (uint8_t j = 0; j < 4; j++) {
            state[i*4+j] ^= RoundKey[round*16 + i*4+j];
        }
    }
}

static void SubBytes(uint8_t *state) {
    for (uint8_t i = 0; i < 16; i++) {
        state[i] = sbox[state[i]];
    }
}

static void ShiftRows(uint8_t *state) {
    uint8_t temp;
    
    temp = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = temp;
    
    temp = state[2];
    state[2] = state[10];
    state[10] = temp;
    temp = state[6];
    state[6] = state[14];
    state[14] = temp;
    
    temp = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = state[3];
    state[3] = temp;
}

static uint8_t xtime(uint8_t x) {
    return ((x << 1) ^ (((x >> 7) & 1) * 0x1b));
}

static void MixColumns(uint8_t *state) {
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t t = state[i*4];
        uint8_t Tmp = state[i*4] ^ state[i*4+1] ^ state[i*4+2] ^ state[i*4+3];
        uint8_t Tm = state[i*4] ^ state[i*4+1];
        Tm = xtime(Tm);
        state[i*4] ^= Tm ^ Tmp;
        
        Tm = state[i*4+1] ^ state[i*4+2];
        Tm = xtime(Tm);
        state[i*4+1] ^= Tm ^ Tmp;
        
        Tm = state[i*4+2] ^ state[i*4+3];
        Tm = xtime(Tm);
        state[i*4+2] ^= Tm ^ Tmp;
        
        Tm = state[i*4+3] ^ t;
        Tm = xtime(Tm);
        state[i*4+3] ^= Tm ^ Tmp;
    }
}

void LogitechUnifying::AES_ECB_encrypt(struct AES_ctx *ctx, uint8_t *buf) {
    AddRoundKey(0, buf, ctx->RoundKey);
    
    for (uint8_t round = 1; round < Nr; round++) {
        SubBytes(buf);
        ShiftRows(buf);
        MixColumns(buf);
        AddRoundKey(round, buf, ctx->RoundKey);
    }
    
    SubBytes(buf);
    ShiftRows(buf);
    AddRoundKey(Nr, buf, ctx->RoundKey);
}

// =====================================================
// Конец реализации AES
// =====================================================

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
    EEPROM.put(sizeof(rf_address), device_key);
    if (EEPROM.commit()) {
        ESP_LOGI(TAG, "Settings saved");
    } else {
        ESP_LOGE(TAG, "EEPROM commit failed");
    }
}

void LogitechUnifying::load_from_eeprom() {
    EEPROM.get(0, rf_address);
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
            
            // Инициализация AES контекста
            AES_init_ctx(&aes_ctx_, device_key);
            
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

void LogitechUnifying::update_little_known_secret_counter(uint8_t *counter) {
    memcpy(little_known_secret + 7, counter, 4);
}

void LogitechUnifying::logitacker_unifying_crypto_calculate_frame_key(uint8_t *frame_key, uint8_t *counter_bytes, bool silent) {
    if (!silent) {
        ESP_LOGD(TAG, "1. last plain l_k_s: %s", format_hex_pretty(little_known_secret, 16).c_str());
    }
    
    update_little_known_secret_counter(counter_bytes);
    
    if (!silent) {
        ESP_LOGD(TAG, "2. plain l_k_s+counter: %s", format_hex_pretty(little_known_secret, 16).c_str());
        ESP_LOGD(TAG, "3. device_key: %s", format_hex_pretty(device_key, 16).c_str());
    }
    
    memcpy(frame_key, little_known_secret, 16);
    AES_ECB_encrypt(&aes_ctx_, frame_key);
    
    if (!silent) {
        ESP_LOGD(TAG, "4. frame_key: %s", format_hex_pretty(frame_key, 16).c_str());
    }
}

void LogitechUnifying::logitacker_unifying_crypto_encrypt_keyboard_frame(uint8_t *rf_frame, uint8_t *plain_payload, uint32_t counter) {
    rf_frame[1] = 0x13; // LOGITACKER_DEVICE_REPORT_TYPES_ENCRYPTED_KEYBOARD
    
    uint8_t counter_bytes[4] = {
        static_cast<uint8_t>((counter >> 24) & 0xFF),
        static_cast<uint8_t>((counter >> 16) & 0xFF),
        static_cast<uint8_t>((counter >> 8) & 0xFF),
        static_cast<uint8_t>(counter & 0xFF)
    };
    
    memcpy(rf_frame + 10, counter_bytes, 4);
    
    uint8_t frame_key[16];
    logitacker_unifying_crypto_calculate_frame_key(frame_key, counter_bytes, true);
    
    plain_payload[7] = 0xC9;
    memcpy(rf_frame + 2, plain_payload, 8);
    
    for (int i = 0; i < 8; i++) {
        rf_frame[2 + i] ^= frame_key[i];
    }
    
    // Расчет контрольной суммы
    uint8_t sum = 0;
    for (int i = 0; i < 21; i++) sum += rf_frame[i];
    rf_frame[21] = ~sum + 1;
}

void LogitechUnifying::move(int16_t x, int16_t y) {
    // Ограничение значений
    if (x > 2047) x = 2047;
    else if (x < -2048) x = -2048;
    if (y > 2047) y = 2047;
    else if (y < -2048) y = -2048;
    
    uint8_t x_sign = (x < 0) ? 0x40 : 0;
    uint8_t y_sign = (y < 0) ? 0x40 : 0;
    
    uint8_t x_high = static_cast<uint8_t>((x >> 8) & 0x0F) | x_sign;
    uint8_t y_high = static_cast<uint8_t>((y >> 8) & 0x0F) | y_sign;
    
    uint8_t packet[10] = {
        rf_address[0], 
        0xC2, 
        0x00,
        static_cast<uint8_t>(x & 0xFF), 
        x_high,
        static_cast<uint8_t>(y & 0xFF), 
        y_high,
        0x00,
        0x00
    };
    
    // Расчет контрольной суммы
    uint8_t sum = 0;
    for (int i = 0; i < 9; i++) sum += packet[i];
    packet[9] = ~sum + 1;
    
    radio.write(packet, sizeof(packet));
}

bool LogitechUnifying::is_other_device_active() {
    // Упрощенная реализация
    return false;
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
