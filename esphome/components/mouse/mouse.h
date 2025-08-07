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

// Константы протокола
#define PAIRING_MARKER_PHASE_1 0xe1
#define PAIRING_MARKER_PHASE_2 0xe2
#define PAIRING_MARKER_PHASE_3 0xe3

#define LOGITACKER_UNIFYING_PAIRING_REQ1_OFFSET_DEVICE_WPID 9
#define LOGITACKER_UNIFYING_PAIRING_RSP1_OFFSET_DONGLE_WPID 9
#define LOGITACKER_UNIFYING_PAIRING_RSP1_OFFSET_BASE_ADDR 3
#define LOGITACKER_UNIFYING_PAIRING_REQ2_OFFSET_DEVICE_NONCE 3
#define LOGITACKER_UNIFYING_PAIRING_RSP2_OFFSET_DONGLE_NONCE 3

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
    bool radiowrite(uint8_t *packet, uint8_t packet_size, const char *name, uint8_t retry);
    bool pair_response(uint8_t *packet, const char *name, uint8_t retry);
    uint8_t read(uint8_t *&packet);

private:
    RF24 radio;
    uint8_t rf_address[5];
    uint8_t device_key[16];
    uint8_t device_raw_key_material[16];
    uint8_t current_channel;
    uint8_t channel_pairing_id = 0;
    uint8_t channel_tx_id = 0;
    bool is_paired = false;
    bool is_pairing = false;
    bool lock_channel = false;
    uint32_t last_channel_scan = 0;
    uint32_t aes_base = 0xed3456ed;
    uint8_t aes_counter = 0;
    uint8_t little_known_secret[16] = {
        0x04, 0x14, 0x1d, 0x1f, 0x27, 0x28, 0x0d, 0xde, 0xad, 0xbe, 0xef, 0x0a, 0x0d, 0x13, 0x26, 0x0e
    };
    struct AES_ctx aes_ctx_;
    
    const uint8_t channel_pairing[11] = {62, 8, 35, 65, 14, 41, 71, 17, 44, 74, 5};
    const uint8_t channel_tx[25] = {5, 8, 11, 14, 17, 20, 23, 26, 29, 32, 35, 38, 41, 44, 47, 50, 53, 56, 59, 62, 65, 68, 71, 74, 77};
    
    void save_to_eeprom();
    void load_from_eeprom();
    void changeChannel();
    void setAddress(uint8_t *address);
    void setChecksum(uint8_t *payload, uint8_t len);
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
