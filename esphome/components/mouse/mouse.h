#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/log.h"
#include "ludevice.h"
#include <algorithm> // Добавлен для std::clamp

namespace esphome {
namespace mouse {

class Mouse : public switch_::Switch, public PollingComponent {
 public:
  Mouse() : PollingComponent(1000) {}

  // 1. Исправлен конфликт имен с Arduino.h
  static constexpr float MOUSE_SPEED = 10.0f;
  static constexpr float DEGREES_TO_RAD = 2.0f * 3.14159265358979323846f / 360.0f; // Использовано полное значение PI

  // 2. Добавлен TAG для логгирования
  static const char *const TAG; 

  double x = 0, y = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0, r = 100; // Исправлены имена (y0 вместо y00)
  uint32_t move_timer = 0; // Инициализировано значение
  uint32_t last_move_time = 0;
  ludevice kespb{2, 0};
  bool enable = true;
  bool keydown = false;
  int max_random = 15000;

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

  void write_state(bool state) override {
    enable = state;
    publish_state(state);
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
    ESP_LOGW(TAG, "Failed to initialize device");
  }

  void left_rand() {
    const int steps = 2; // Фиксированное значение, т.к. random(2,2) всегда возвращает 2
    const float step_size = PI / random(2, 20);
    
    for(float i = 0; i < steps * PI; i += step_size) {
      last_move_time = millis();
      r = i * random(20, 25) + i;
      
      const float sin_val = sin(i);
      const float cos_val = cos(i);
      
      x1 = r * sin_val;
      y1 = r * cos_val;
      x = x1 - x0;
      y = y1 - y0;
      x0 = x1;
      y0 = y1;
      
      kespb.move(x, y);
      delay(static_cast<uint32_t>(r / 2));
    } 
  }

  void update() override {
    if (!enable) return;
    
    const uint32_t current_time = millis();
    const uint32_t random_delay = random(1000, max_random);
    
    if ((current_time - move_timer) > random_delay) {
      ESP_LOGD(TAG, "Moving mouse");
      left_rand();
      move_timer = current_time;
    }
  }
};

// 3. Определение TAG вне класса
const char *const Mouse::TAG = "mouse";

}  // namespace mouse
}  // namespace esphome
