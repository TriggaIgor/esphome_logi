import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID

mouse_ns = cg.esphome_ns.namespace('mouse')
Mouse = mouse_ns.class_('Mouse', switch.Switch, cg.Component)

# Конфигурационные ключи
CONF_BASE_SPEED = "base_speed"
CONF_JITTER_AMOUNT = "jitter_amount"
CONF_MOVEMENT_SPEED = "movement_speed"
CONF_MAX_SPEED = "max_speed"
CONF_ACCELERATION_RATE = "acceleration_rate"
CONF_DECELERATION_RATE = "deceleration_rate"

# Обновленная схема с использованием switch_schema
CONFIG_SCHEMA = switch.switch_schema(Mouse).extend({
    cv.Optional(CONF_BASE_SPEED, default=8.0): cv.float_range(min=0.1, max=100.0),
    cv.Optional(CONF_JITTER_AMOUNT, default=1.5): cv.float_range(min=0.0, max=10.0),
    cv.Optional(CONF_MOVEMENT_SPEED, default=0.5): cv.float_range(min=0.01, max=10.0),
    cv.Optional(CONF_MAX_SPEED, default=2.0): cv.float_range(min=0.1, max=20.0),
    cv.Optional(CONF_ACCELERATION_RATE, default=0.01): cv.float_range(min=0.001, max=1.0),
    cv.Optional(CONF_DECELERATION_RATE, default=0.02): cv.float_range(min=0.001, max=1.0),
}).extend(cv.COMPONENT_SCHEMA)

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield switch.register_switch(var, config)
    
    # Установка параметров
    if CONF_BASE_SPEED in config:
        cg.add(var.set_base_speed(config[CONF_BASE_SPEED]))
    if CONF_JITTER_AMOUNT in config:
        cg.add(var.set_jitter_amount(config[CONF_JITTER_AMOUNT]))
    if CONF_MOVEMENT_SPEED in config:
        cg.add(var.set_movement_speed(config[CONF_MOVEMENT_SPEED]))
    if CONF_MAX_SPEED in config:
        cg.add(var.set_max_speed(config[CONF_MAX_SPEED]))
    if CONF_ACCELERATION_RATE in config:
        cg.add(var.set_acceleration_rate(config[CONF_ACCELERATION_RATE]))
    if CONF_DECELERATION_RATE in config:
        cg.add(var.set_deceleration_rate(config[CONF_DECELERATION_RATE]))
