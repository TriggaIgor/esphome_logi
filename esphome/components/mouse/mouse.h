#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/log.h"
#include "ludevice.h"
#include <algorithm>

namespace esphome {
namespace mouse {

class Mouse : public switch_::Switch, public PollingComponent {
 public:
  // Увеличиваем частоту опроса для плавной анимации
  Mouse() : PollingComponent(50) {}

  static constexpr float MOUSE_SPEED = 10.0f;
  static constexpr float DEGREES_TO_RAD = 2.0f * 3.14159265358979323846f / 360.0f;

  static const char *const TAG;

  // Оптимизация: используем float вместо double
  float x = 0, y = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0, r = 100;
  uint32_t move_timer = 0;
  uint32_t last_animation_step = 0;
  ludevice kespb{2, 0};
  bool enable = true;
  int max_random = 15000;
  
  // Состояния анимации
  enum AnimationState {
    ANIMATION_IDLE,
    ANIMATION_RUNNING
  } animation_state = ANIMATION_IDLE;
  
  // Параметры текущей анимации
  float anim_i = 0;
  float anim_step_size = 0;
  uint32_t anim_delay = 0;
  uint32_t anim_last_step = 0;
  int anim_steps = 0;

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
    // Сбрасываем состояние анимации при выключении
    if (!state) animation_state = ANIMATION_IDLE;
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

  void start_animation() {
    animation_state = ANIMATION_RUNNING;
    anim_i = 0;
    anim_steps = 2;
    anim_step_size = PI / random(2, 20);
    anim_delay = 0;
    anim_last_step = millis();
    ESP_LOGD(TAG, "Animation started");
  }

  void animation_step() {
    const uint32_t current_time = millis();
    
    // Пропускаем шаг, если не прошло нужное время задержки
    if (current_time - anim_last_step < anim_delay) {
      return;
    }
    
    anim_last_step = current_time;
    
    // Вычисляем следующий шаг анимации
    r = anim_i * random(20, 25) + anim_i;
    
    // Оптимизация: предвычисленные значения sin/cos
    const float angle = anim_i;
    const float sin_val = sin(angle);
    const float cos_val = cos(angle);
    
    x1 = r * sin_val;
    y1 = r * cos_val;
    x = x1 - x0;
    y = y1 - y0;
    x0 = x1;
    y0 = y1;
    
    kespb.move(x, y);
    
    // Рассчитываем задержку для следующего шага
    anim_delay = static_cast<uint32_t>(r / 2);
    anim_i += anim_step_size;
    
    // Проверяем завершение анимации
    if (anim_i >= anim_steps * PI) {
      animation_state = ANIMATION_IDLE;
      ESP_LOGD(TAG, "Animation completed");
    }
  }

  void update() override {
    // Обновляем состояние устройства
    kespb.loop();
    
    if (!enable) return;
    
    const uint32_t current_time = millis();
    
    // Обрабатываем анимацию, если она активна
    if (animation_state == ANIMATION_RUNNING) {
      animation_step();
      return;
    }
    
    // Запускаем новую анимацию по таймеру
    if ((current_time - move_timer) > random(1000, max_random)) {
      ESP_LOGD(TAG, "Starting mouse movement");
      start_animation();
      move_timer = current_time;
    }
  }
};

const char *const Mouse::TAG = "mouse";

}  // namespace mouse
}  // namespace esphome
