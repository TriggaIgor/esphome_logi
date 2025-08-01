import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID

mouse_ns = cg.esphome_ns.namespace('mouse')
Mouse = mouse_ns.class_('Mouse', switch.Switch, cg.Component)

# Конфигурационные ключи
CONF_BASE_SPEED = "base_speed"
CONF_JITTER_AMOUNT = "jitter_amount"
CONF_PAUSE_PROBABILITY = "pause_probability"
CONF_MOVEMENT_DURATION = "movement_duration"
CONF_MIN_DURATION = "min"
CONF_MAX_DURATION = "max"

# Схема для длительности движения
MOVEMENT_DURATION_SCHEMA = cv.Schema({
    cv.Required(CONF_MIN_DURATION): cv.positive_time_period_milliseconds,
    cv.Required(CONF_MAX_DURATION): cv.positive_time_period_milliseconds,
})

CONFIG_SCHEMA = switch.SWITCH_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(Mouse),
    cv.Optional(CONF_BASE_SPEED, default=15.0): cv.float_range(min=1.0, max=100.0),
    cv.Optional(CONF_JITTER_AMOUNT, default=0.5): cv.float_range(min=0.0, max=5.0),
    cv.Optional(CONF_PAUSE_PROBABILITY, default=0.1): cv.float_range(min=0.0, max=1.0),
    cv.Optional(CONF_MOVEMENT_DURATION): MOVEMENT_DURATION_SCHEMA,
}).extend(cv.COMPONENT_SCHEMA)

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield switch.register_switch(var, config)
    
    # Установка базовой скорости
    if CONF_BASE_SPEED in config:
        cg.add(var.set_base_speed(config[CONF_BASE_SPEED]))
    
    # Установка величины дрожи
    if CONF_JITTER_AMOUNT in config:
        cg.add(var.set_jitter_amount(config[CONF_JITTER_AMOUNT]))
    
    # Установка вероятности паузы
    if CONF_PAUSE_PROBABILITY in config:
        cg.add(var.set_pause_probability(config[CONF_PAUSE_PROBABILITY]))
    
    # Установка длительности движения
    if CONF_MOVEMENT_DURATION in config:
        duration_config = config[CONF_MOVEMENT_DURATION]
        min_duration = int(duration_config[CONF_MIN_DURATION])
        max_duration = int(duration_config[CONF_MAX_DURATION])
        cg.add(var.set_movement_duration(min_duration, max_duration))
