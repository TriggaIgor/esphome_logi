#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h" // Для random_uint32()
#include <elapsedMillis.h>
#include "ludevice.h"

namespace esphome {
namespace logitech_mouse {

static const char *const TAG = "logitech_mouse";

class Mouse : public switch_::Switch, public Component {
 public:
  // Конфигурируемые параметры
  float move_interval_min = 5000;  // 5 сек
  float move_interval_max = 25000; // 25 сек
  float move_duration = 2000;      // 2 сек движения
  float mouse_speed = 10.0f;

  void setup() override {
    randomSeed(random_uint32()); // Исправленная инициализация ГСЧ
    this->begin();
  }

  void begin() {
    ESP_LOGI(TAG, "Initializing mouse emulator");
    if (!kespb_.begin()) {
      ESP_LOGE(TAG, "Failed to initialize HID device");
      return;
    }
    this->reconnect();
  }

  bool reconnect() {
    if (kespb_.reconnect()) {
      ESP_LOGI(TAG, "Reconnected to dongle");
      return true;
    }
    
    ESP_LOGW(TAG, "Pairing attempt...");
    if (kespb_.pair() && kespb_.register_device()) {
      ESP_LOGI(TAG, "Paired successfully");
      return true;
    }
    
    ESP_LOGE(TAG, "Pairing failed");
    return false;
  }

  void write_state(bool state) override {
    enabled_ = state;
    publish_state(state);
    if (state) ESP_LOGD(TAG, "Mouse emulation enabled");
  }

  void loop() override {
    static elapsedMillis connection_timer;
    
    // Поддержание соединения
    if (connection_timer > 10000) {
      if (!kespb_.connected() && !this->reconnect()) { // Исправленный вызов
        ESP_LOGW(TAG, "Connection lost");
      }
      connection_timer = 0;
    }

    // Периодическое движение
    if (enabled_ && move_timer_ > random(move_interval_min, move_interval_max)) {
      this->generate_movement();
      move_timer_ = 0;
    }
    
    kespb_.loop();
  }

 private:
  ludevice kespb_{2, 0};
  elapsedMillis move_timer_;
  bool enabled_ = true;
  
  void generate_movement() {
    const uint32_t start_time = millis();
    const uint32_t duration = random(1000, static_cast<uint32_t>(move_duration));
    
    ESP_LOGD(TAG, "Generating mouse movement");
    float x0 = 0, y0 = 0;
    
    while (millis() - start_time < duration) {
      const float angle = random(0, 360) * (PI / 180.0f);
      const float distance = mouse_speed * (0.5f + this->randomf());
      
      const float x = distance * cos(angle);
      const float y = distance * sin(angle);
      
      kespb_.move(x, y);
      
      // Неблокирующая задержка
      const uint32_t step_delay = random(20, 100);
      delay(step_delay);
      App.feed_wdt(); // Важно для ESP32
    }
  }

  float randomf() {
    return random(0, 100) / 100.0f;
  }
};

}  // namespace logitech_mouse
}  // namespace esphome
