#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/log.h"
#include "ludevice.h"
#include <algorithm>
#include <vector>

namespace esphome {
namespace mouse {

// Определяем константу PI
static constexpr float PI = 3.14159265358979323846f;

class Mouse : public switch_::Switch, public PollingComponent {
 public:
  Mouse() : PollingComponent(20) {}

  static const char *const TAG;

  // Состояния анимации
  enum AnimationState {
    ANIMATION_IDLE,
    ANIMATION_RUNNING
  } animation_state = ANIMATION_IDLE;
  
  // Типы движений
  enum MovementPattern {
    FIGURE_EIGHT,
    RANDOM_PATH,
    SMALL_CIRCLES,
    HUMAN_LIKE
  };

  // Параметры текущей анимации
  MovementPattern current_pattern = HUMAN_LIKE;
  uint32_t anim_start_time = 0;
  uint32_t anim_duration = 0;
  float anim_progress = 0;
  
  // Состояние паузы
  bool is_pausing = false;
  uint32_t pause_start = 0;
  uint32_t pause_duration = 0;
  
  // Позиции
  float last_x = 0, last_y = 0;
  float target_x = 0, target_y = 0;
  
  ludevice kespb{2, 0};
  bool enable = true;
  int max_random = 15000;
  uint32_t move_timer = 0;

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


  // Квадратичная функция плавности (ease-in-out)
  float ease_in_out_quad(float t) {
    if (t < 0.5f) {
      return 2.0f * t * t;
    } else {
      t = t * 2.0f - 1.0f;
      return 0.5f * (1.0f - t * t * t) + 0.5f;
    }
  }

  // Генерация случайного числа float в диапазоне
  float random_float(float min, float max) {
    return min + static_cast<float>(random(0, 10000)) / 10000.0f * (max - min);
  }

  // Генерация человеческого движения
  void start_human_animation() {
    animation_state = ANIMATION_RUNNING;
    anim_start_time = millis();
    anim_duration = random(min_duration_, max_duration_); // Случайная длительность
    anim_progress = 0;
    is_pausing = false;
    
    // Выбор случайного паттерна
    current_pattern = static_cast<MovementPattern>(random(0, 4));
    
    // Установка новой цели
    last_x = 0;
    last_y = 0;
    
    // Случайная цель в пределах рабочей области
    target_x = random_float(-80.0f, 80.0f);
    target_y = random_float(-50.0f, 50.0f);
    
    ESP_LOGD(TAG, "Starting human-like move: pattern=%d, target=(%.1f,%.1f)", 
             current_pattern, target_x, target_y);
  }

  // Вычисление позиции на основе паттерна
  std::pair<float, float> get_pattern_position(float progress) {
    float x = 0, y = 0;
    const float scale = 50.0f;  // Масштаб движений
    
    switch (current_pattern) {
      case FIGURE_EIGHT:
        // Восьмёрка
        x = scale * sin(progress * 2 * PI);
        y = scale * sin(progress * PI) * cos(progress * PI);
        break;
        
      case RANDOM_PATH:
        // Случайный путь
        x = progress * target_x;
        y = progress * target_y;
        break;
        
      case SMALL_CIRCLES:
        // Маленькие круги
        x = scale * 0.5f * cos(progress * 4 * PI);
        y = scale * 0.5f * sin(progress * 4 * PI);
        break;
        
      case HUMAN_LIKE:
      default:
        // Человекоподобное движение с дрожью
        x = ease_in_out_quad(progress) * target_x;
        y = ease_in_out_quad(progress) * target_y;
        break;
    }
    
    return {x, y};
  }

  void human_animation_step() {
    const uint32_t current_time = millis();
    const uint32_t elapsed = current_time - anim_start_time;
    
    // Проверка завершения анимации
    if (elapsed >= anim_duration) {
      // Плавное завершение движения
      const auto [final_x, final_y] = get_pattern_position(1.0f);
      const float dx = final_x - last_x;
      const float dy = final_y - last_y;
      kespb.move(dx, dy);
      
      animation_state = ANIMATION_IDLE;
      ESP_LOGD(TAG, "Human move completed");
      return;
    }
    
    // Проверка на паузу
    if (!is_pausing && random_float(0.0f, 1.0f) < pause_probability) {
      is_pausing = true;
      pause_start = current_time;
      pause_duration = random(50, 200);  // Короткая пауза
      return;
    }
    
    // Если в паузе - пропускаем движение
    if (is_pausing) {
      if (current_time - pause_start >= pause_duration) {
        is_pausing = false;
      }
      return;
    }
    
    // Прогресс анимации с учетом easing
    anim_progress = static_cast<float>(elapsed) / anim_duration;
    
    // Получаем текущую позицию
    const auto [current_x, current_y] = get_pattern_position(anim_progress);
    
    // Вычисляем разницу с предыдущей позицией
    float dx = current_x - last_x;
    float dy = current_y - last_y;
    
    // Добавляем "дрожь" руки
    dx += random_float(-jitter_amount, jitter_amount);
    dy += random_float(-jitter_amount, jitter_amount);
    
    // Сохраняем текущую позицию
    last_x = current_x;
    last_y = current_y;
    
    // Отправляем движение
    kespb.move(dx, dy);
  }

  void update() override {
    kespb.loop(); // Поддерживаем соединение
    
    if (!enable) return;
    
    // Обрабатываем анимацию
    if (animation_state == ANIMATION_RUNNING) {
      human_animation_step();
      return;
    }
    
    // Запускаем новую анимацию по таймеру
    const uint32_t current_time = millis();
    if ((current_time - move_timer) > random(1000, max_random)) {
      start_human_animation();
      move_timer = current_time;
    }
  }
  
  void set_base_speed(float speed) { base_speed = speed; }
  void set_jitter_amount(float jitter) { jitter_amount = jitter; }
  void set_pause_probability(float probability) { pause_probability = probability; }
  void set_movement_duration(int min_duration, int max_duration) { 
      min_duration_ = min_duration;
      max_duration_ = max_duration;
  }

  void dump_config() override {
      ESP_LOGCONFIG(TAG, "Mouse Settings:");
      ESP_LOGCONFIG(TAG, "  Base Speed: %.1f", base_speed);
      ESP_LOGCONFIG(TAG, "  Jitter Amount: %.2f", jitter_amount);
      ESP_LOGCONFIG(TAG, "  Pause Probability: %.2f", pause_probability);
      ESP_LOGCONFIG(TAG, "  Movement Duration: %d-%d ms", min_duration_, max_duration_);
  }

private:
  // Параметры конфигурации
  float base_speed = 15.0f;
  float jitter_amount = 0.5f;
  float pause_probability = 0.1f;
  int min_duration_ = 800;   // по умолчанию
  int max_duration_ = 2500;  // по умолчанию
};

const char *const Mouse::TAG = "mouse";

}  // namespace mouse
}  // namespace esphome
