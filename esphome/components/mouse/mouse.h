#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/log.h"
#include "ludevice.h"
#include <algorithm>
#include <vector>
#include <cmath>

namespace esphome {
namespace mouse {

class Mouse : public switch_::Switch, public PollingComponent {
 public:
  Mouse() : PollingComponent(10) {}

  static const char *const TAG;
  static constexpr float MOUSE_PI = 3.14159265358979323846f;

  enum AnimationState {
    ANIMATION_IDLE,
    ANIMATION_ACCELERATING,
    ANIMATION_MOVING,
    ANIMATION_DECELERATING,
    REAL_DEVICE_DETECTED
  } animation_state = ANIMATION_IDLE;
  
  struct Point {
    float x = 0;
    float y = 0;
  };

  Point current_position;
  Point target_position;
  Point velocity;
  Point acceleration;
  
  uint32_t anim_start_time = 0;
  uint32_t move_duration = 0;
  uint32_t last_update_time = 0;
  
  ludevice kespb{2, 0};
  bool enable = true;
  int max_random = 30000;
  uint32_t move_timer = 0;
  uint32_t real_mouse_timeout = 0;

  float get_setup_priority() const override { 
    return esphome::setup_priority::HARDWARE; 
  }

  bool pair() {
    if (kespb.pair()) {
      if (kespb.register_device()) {
        ESP_LOGD(TAG, "Paired and connected");
        return true;
      }
    }
    ESP_LOGD(TAG, "Pairing failed");
    return false;
  }

  void set_random(int rand) {
    max_random = std::clamp(rand, 1000, 15000);
  }

  void setup() override {
    ESP_LOGD(TAG, "Initializing mouse device");
    kespb.begin();
    publish_state(true);
    
    for (int i = 0; i < 10; i++) {
      if (kespb.reconnect() || pair()) {
        ESP_LOGD(TAG, "Connection established");
        return;
      }
      yield();
    }
    ESP_LOGW(TAG, "Failed to initialize on setup. Will retry in main loop");
  }

  void write_state(bool state) override {
    enable = state;
    if (!state) {
      animation_state = ANIMATION_IDLE;
    }
    publish_state(state);
  }

  float random_float(float min, float max) {
    return min + static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * (max - min);
  }

  void start_human_animation() {
    anim_start_time = millis();
    last_update_time = anim_start_time;
    
    target_position.x = random_float(-200.0f, 200.0f);
    target_position.y = random_float(-150.0f, 150.0f);
    
    current_position.x = 0;
    current_position.y = 0;
    
    velocity.x = 0;
    velocity.y = 0;
    acceleration.x = 0;
    acceleration.y = 0;
    
    float dx = target_position.x - current_position.x;
    float dy = target_position.y - current_position.y;
    float distance = std::sqrt(dx*dx + dy*dy);
    
    move_duration = std::max(800, static_cast<int>(distance / movement_speed));
    
    animation_state = ANIMATION_ACCELERATING;
    
    ESP_LOGD(TAG, "New move: target=(%.1f,%.1f), duration=%dms", 
             target_position.x, target_position.y, move_duration);
  }

  void physics_step() {
    const uint32_t current_time = millis();
    const float delta_time = (current_time - last_update_time) / 1000.0f;
    last_update_time = current_time;
    
    float dx = target_position.x - current_position.x;
    float dy = target_position.y - current_position.y;
    float distance = sqrtf(dx*dx + dy*dy);
    
    if (distance < 0.5f) {
        animation_state = ANIMATION_IDLE;
        return;
    }
    
    float dir_x = dx / distance;
    float dir_y = dy / distance;
    
    switch (animation_state) {
        case ANIMATION_IDLE:
        case REAL_DEVICE_DETECTED:
            return;
            
        case ANIMATION_ACCELERATING:
            acceleration.x = dir_x * acceleration_rate;
            acceleration.y = dir_y * acceleration_rate;
            
            if (std::sqrt(velocity.x*velocity.x + velocity.y*velocity.y) >= movement_speed) {
                animation_state = ANIMATION_MOVING;
            }
            break;
            
        case ANIMATION_MOVING:
            acceleration.x = 0;
            acceleration.y = 0;
            
            if (distance < 50.0f) {
                animation_state = ANIMATION_DECELERATING;
            }
            break;
            
        case ANIMATION_DECELERATING: {
            float brake_factor = std::min(1.0f, distance / 30.0f);
            acceleration.x = -velocity.x * deceleration_rate * brake_factor;
            acceleration.y = -velocity.y * deceleration_rate * brake_factor;
            break;
        }
            
        default: {
            animation_state = ANIMATION_IDLE;
            return;
        }
    }
    
    velocity.x += acceleration.x * delta_time;
    velocity.y += acceleration.y * delta_time;
    
    float current_speed = std::sqrt(velocity.x*velocity.x + velocity.y*velocity.y);
    if (current_speed > max_speed) {
        velocity.x = velocity.x * max_speed / current_speed;
        velocity.y = velocity.y * max_speed / current_speed;
    }
    
    const float scale_factor = 2.0f;
    
    current_position.x += static_cast<int>(velocity.x * delta_time * 1000.0f * scale_factor);
    current_position.y += static_cast<int>(velocity.y * delta_time * 1000.0f * scale_factor);
    
    float jitter_x = random_float(-jitter_amount, jitter_amount);
    float jitter_y = random_float(-jitter_amount, jitter_amount);
    
    int move_x = static_cast<int>((velocity.x + jitter_x) * base_speed);
    int move_y = static_cast<int>((velocity.y + jitter_y) * base_speed);
    
    move_x = std::max(std::min(move_x, 200), -200);
    move_y = std::max(std::min(move_y, 200), -200);
    
    kespb.move(move_x, move_y);
  }

  void update() override {
    kespb.loop();
    
    if (!enable) return;

    const uint32_t current_time = millis();
    
    // Проверка активности реальной мыши
    if (kespb.is_other_device_active()) {
        if (animation_state != REAL_DEVICE_DETECTED) {
            ESP_LOGW(TAG, "Real mouse detected! Suspending emulation");
            animation_state = REAL_DEVICE_DETECTED;
            real_mouse_timeout = current_time;
        } else {
            // Обновляем таймер при каждом обнаружении
            real_mouse_timeout = current_time;
        }
        return;
    }
    
    // Восстановление после обнаружения реальной мыши
    if (animation_state == REAL_DEVICE_DETECTED) {
        if (current_time - real_mouse_timeout > 30000) { // 30 секунд неактивности
            ESP_LOGI(TAG, "Real mouse inactive. Resuming emulation");
            animation_state = ANIMATION_IDLE;
            move_timer = current_time;
        } else {
            return; // Ждем окончания таймаута
        }
    }

    // Обработка анимации
    if (animation_state != ANIMATION_IDLE && 
        animation_state != REAL_DEVICE_DETECTED) {
        physics_step();
    }
    
    // Запуск нового движения
    if (animation_state == ANIMATION_IDLE) {
        uint32_t elapsed = current_time - move_timer;
        uint32_t required_delay = static_cast<uint32_t>(random(5000, max_random));
        
        if (elapsed > required_delay) {
            start_human_animation();
            move_timer = current_time;
        }
    }
  }
  
  // Методы конфигурации
  void set_base_speed(float speed) { base_speed = speed; }
  void set_jitter_amount(float jitter) { jitter_amount = jitter; }
  void set_movement_speed(float speed) { movement_speed = speed; }
  void set_max_speed(float speed) { max_speed = speed; }
  void set_acceleration_rate(float rate) { acceleration_rate = rate; }
  void set_deceleration_rate(float rate) { deceleration_rate = rate; }

  void dump_config() override {
    ESP_LOGCONFIG(TAG, "Mouse Settings:");
    ESP_LOGCONFIG(TAG, "  Base Speed: %.1f", base_speed);
    ESP_LOGCONFIG(TAG, "  Jitter Amount: %.2f", jitter_amount);
    ESP_LOGCONFIG(TAG, "  Movement Speed: %.2f px/ms", movement_speed);
    ESP_LOGCONFIG(TAG, "  Max Speed: %.2f px/ms", max_speed);
    ESP_LOGCONFIG(TAG, "  Acceleration: %.4f", acceleration_rate);
    ESP_LOGCONFIG(TAG, "  Deceleration: %.4f", deceleration_rate);
  }

private:
  float base_speed = 15.0f;
  float jitter_amount = 2.5f;
  float movement_speed = 0.7f;
  float max_speed = 3.0f;
  float acceleration_rate = 0.02f;
  float deceleration_rate = 0.03f;
};

const char *const Mouse::TAG = "mouse";

}  // namespace mouse
}  // namespace esphome
