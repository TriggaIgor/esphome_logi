import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID, CONF_INTERNAL

mouse_ns = cg.esphome_ns.namespace('mouse')
Mouse = mouse_ns.class_('Mouse', switch.Switch, cg.PollingComponent)

CONF_CE_PIN = 'ce_pin'
CONF_CS_PIN = 'cs_pin'
CONF_BASE_SPEED = 'base_speed'
CONF_JITTER_AMOUNT = 'jitter_amount'
CONF_MOVEMENT_SPEED = 'movement_speed'
CONF_MAX_SPEED = 'max_speed'
CONF_ACCELERATION_RATE = 'acceleration_rate'
CONF_DECELERATION_RATE = 'deceleration_rate'
CONF_RANDOM = 'random'

CONFIG_SCHEMA = switch.SWITCH_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(Mouse),
    cv.Required(CONF_CE_PIN): cv.int_,
    cv.Required(CONF_CS_PIN): cv.int_,
    cv.Optional(CONF_BASE_SPEED, default=15.0): cv.float_,
    cv.Optional(CONF_JITTER_AMOUNT, default=2.5): cv.float_,
    cv.Optional(CONF_MOVEMENT_SPEED, default=0.7): cv.float_,
    cv.Optional(CONF_MAX_SPEED, default=3.0): cv.float_,
    cv.Optional(CONF_ACCELERATION_RATE, default=0.02): cv.float_,
    cv.Optional(CONF_DECELERATION_RATE, default=0.03): cv.float_,
    cv.Optional(CONF_RANDOM, default=30000): cv.int_,
}).extend(cv.polling_component_schema('10ms'))

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield switch.register_switch(var, config)
    
    cg.add(var.set_ce_pin(config[CONF_CE_PIN]))
    cg.add(var.set_cs_pin(config[CONF_CS_PIN]))
    cg.add(var.set_base_speed(config[CONF_BASE_SPEED]))
    cg.add(var.set_jitter_amount(config[CONF_JITTER_AMOUNT]))
    cg.add(var.set_movement_speed(config[CONF_MOVEMENT_SPEED]))
    cg.add(var.set_max_speed(config[CONF_MAX_SPEED]))
    cg.add(var.set_acceleration_rate(config[CONF_ACCELERATION_RATE]))
    cg.add(var.set_deceleration_rate(config[CONF_DECELERATION_RATE]))
    cg.add(var.set_random(config[CONF_RANDOM]))
