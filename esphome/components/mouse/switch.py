import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID
from esphome import automation
from esphome.automation import maybe_simple_id

mouse_ns = cg.esphome_ns.namespace('mouse')
Mouse = mouse_ns.class_('Mouse', switch.Switch, cg.Component)

CONFIG_SCHEMA = switch.SWITCH_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(Mouse)
}).extend(cv.COMPONENT_SCHEMA)

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield switch.register_switch(var, config)

MousePair = mouse_ns.class_(
    "MousePairAction", automation.Action
)


@automation.register_action(
    f"mouse.pair",
    MousePair,
    maybe_simple_id(OPERATION_BASE_SCHEMA),
)
async def mouse_pair_to_code(
    config: dict, action_id: ID, template_arg: TemplateArguments, args: list
) -> MockObj:
    """Action release

    :param config: dict
    :param action_id: ID
    :param template_arg: TemplateArguments
    :param args: list
    :return: MockObj
    """

    paren: MockObj = await cg.get_variable(config[CONF_ID])

    return cg.new_Pvariable(action_id, template_arg, paren)