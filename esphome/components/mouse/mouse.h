#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/log.h"
#include <RF24.h>
#include <vector>
#include <cmath>
#include <algorithm>

namespace esphome {
namespace mouse {

// Структура контекста AES
struct AES_ctx {
    uint8_t RoundKey[176];
};

class LogitechUnifying {
public:
    static const char *const TAG;
    
    LogitechUnifying(uint8_t ce_pin, uint8_t cs_pin);
    bool begin();
    bool pair();
    bool reconnect();
    void move(int16_t x, int16_t y);
    void loop();
    bool is_other_device_active();
    void logitacker_unifying_crypto_encrypt_keyboard_frame(uint8_t *encrypted, uint8_t *plain, uint32_t counter);
    void logitacker_unifying_crypto_calculate_frame_key(uint8_t *frame_key, uint8_t *counter_bytes, bool silent);
    void update_little_known_secret_counter(uint8_t *counter);

private:
    struct AES_ctx aes_ctx_; 
    RF24 radio;
    uint8_t rf_address[5];
    uint8_t device_key[16];
    uint8_t device_raw_key_material[16];
    uint8_t current_channel;
    bool is_paired = false;
    uint32_t last_channel_scan = 0;
    uint32_t aes_base = 0xed3456ed;
    uint8_t aes_counter = 0;
    uint8_t little_known_secret[16] = {
        0x04, 0x14, 0x1d, 0x1f, 0x27, 0x28, 0x0d, 0xde, 0xad, 0xbe, 0xef, 0x0a, 0x0d, 0x13, 0x26, 0x0e
    };
    
    void save_to_eeprom();
    void load_from_eeprom();
    bool send_pairing_packet();
    void AES_init_ctx(struct AES_ctx *ctx, const uint8_t *key);
    void AES_ECB_encrypt(struct AES_ctx *ctx, uint8_t *buf);
};

class Mouse : public switch_::Switch, public PollingComponent {
public:
    Mouse() : PollingComponent(10) {}
    
    void setup() override;
    void update() override;
    void write_state(bool state) override;
    void dump_config() override;
    
    void set_base_speed(float speed) { base_speed_ = speed; }
    void set_jitter_amount(float jitter) { jitter_amount_ = jitter; }
    void set_movement_speed(float speed) { movement_speed_ = speed; }
    void set_max_speed(float speed) { max_speed_ = speed; }
    void set_acceleration_rate(float rate) { acceleration_rate_ = rate; }
    void set_deceleration_rate(float rate) { deceleration_rate_ = rate; }
    void set_random(int rand) { random_delay_ = rand; }
    
    void set_ce_pin(uint8_t pin) { ce_pin_ = pin; }
    void set_cs_pin(uint8_t pin) { cs_pin_ = pin; }

private:
    static const char *const TAG;
    
    uint8_t ce_pin_ = 2;
    uint8_t cs_pin_ = 0;
    bool enabled_ = true;
    uint32_t last_move_ = 0;
    float angle_ = 0.0f;
    
    float base_speed_ = 15.0f;
    float jitter_amount_ = 2.5f;
    float movement_speed_ = 0.7f;
    float max_speed_ = 3.0f;
    float acceleration_rate_ = 0.02f;
    float deceleration_rate_ = 0.03f;
    int random_delay_ = 30000;
    
    std::unique_ptr<LogitechUnifying> unifying_;
    void move_in_circle();
};

}  // namespace mouse
}  // namespace esphome
