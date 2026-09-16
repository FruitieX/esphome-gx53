import esphome.codegen as cg
from esphome.components import light, output
import esphome.config_validation as cv
from esphome.const import (
    CONF_BLUE,
    CONF_COLD_WHITE,
    CONF_COLD_WHITE_COLOR_TEMPERATURE,
    CONF_GREEN,
    CONF_OUTPUT_ID,
    CONF_RED,
    CONF_WARM_WHITE,
    CONF_WARM_WHITE_COLOR_TEMPERATURE,
)
from esphome.types import ConfigType

CONF_RGB_WHITE_COLOR_TEMPERATURE = "rgb_white_color_temperature"
CONF_WHITE_EXTRACTION = "white_extraction"
CONF_MINIMUM_BRIGHTNESS = "minimum_brightness"
CONF_CURRENT_BUDGET_MA = "current_budget_ma"
CONF_RED_CURRENT_MA = "red_current_ma"
CONF_GREEN_CURRENT_MA = "green_current_ma"
CONF_BLUE_CURRENT_MA = "blue_current_ma"
CONF_COLD_WHITE_CURRENT_MA = "cold_white_current_ma"
CONF_WARM_WHITE_CURRENT_MA = "warm_white_current_ma"

gx53_mixer_ns = cg.esphome_ns.namespace("gx53_mixer")
GX53MixerLightOutput = gx53_mixer_ns.class_("GX53MixerLightOutput", light.LightOutput)


def _validate_mixer(config):
    cold = config[CONF_COLD_WHITE_COLOR_TEMPERATURE]
    warm = config[CONF_WARM_WHITE_COLOR_TEMPERATURE]
    rgb_white = config[CONF_RGB_WHITE_COLOR_TEMPERATURE]

    # ESPHome stores color temperature in mireds, so colder values are smaller.
    if not cold <= rgb_white <= warm:
        raise cv.Invalid(
            "rgb_white_color_temperature must be between the native cold-white "
            "and warm-white color temperatures",
            path=[CONF_RGB_WHITE_COLOR_TEMPERATURE],
        )

    return config


CONFIG_SCHEMA = cv.All(
    light.RGB_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(GX53MixerLightOutput),
            cv.Required(CONF_RED): cv.use_id(output.FloatOutput),
            cv.Required(CONF_GREEN): cv.use_id(output.FloatOutput),
            cv.Required(CONF_BLUE): cv.use_id(output.FloatOutput),
            cv.Required(CONF_COLD_WHITE): cv.use_id(output.FloatOutput),
            cv.Required(CONF_WARM_WHITE): cv.use_id(output.FloatOutput),
            cv.Required(CONF_COLD_WHITE_COLOR_TEMPERATURE): cv.color_temperature,
            cv.Required(CONF_WARM_WHITE_COLOR_TEMPERATURE): cv.color_temperature,
            cv.Required(CONF_RGB_WHITE_COLOR_TEMPERATURE): cv.color_temperature,
            cv.Optional(CONF_WHITE_EXTRACTION, default="100%"): cv.percentage,
            cv.Optional(CONF_MINIMUM_BRIGHTNESS, default="0%"): cv.percentage,
            cv.Optional(CONF_CURRENT_BUDGET_MA, default=12.0): cv.positive_float,
            cv.Optional(CONF_RED_CURRENT_MA, default=12.0): cv.positive_float,
            cv.Optional(CONF_GREEN_CURRENT_MA, default=12.0): cv.positive_float,
            cv.Optional(CONF_BLUE_CURRENT_MA, default=12.0): cv.positive_float,
            cv.Optional(CONF_COLD_WHITE_CURRENT_MA, default=12.0): cv.positive_float,
            cv.Optional(CONF_WARM_WHITE_CURRENT_MA, default=12.0): cv.positive_float,
        }
    ),
    light.validate_color_temperature_channels,
    _validate_mixer,
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)

    red = await cg.get_variable(config[CONF_RED])
    green = await cg.get_variable(config[CONF_GREEN])
    blue = await cg.get_variable(config[CONF_BLUE])
    cold_white = await cg.get_variable(config[CONF_COLD_WHITE])
    warm_white = await cg.get_variable(config[CONF_WARM_WHITE])

    cg.add(var.set_red(red))
    cg.add(var.set_green(green))
    cg.add(var.set_blue(blue))
    cg.add(var.set_cold_white(cold_white))
    cg.add(var.set_warm_white(warm_white))

    cg.add(var.set_cold_white_temperature(config[CONF_COLD_WHITE_COLOR_TEMPERATURE]))
    cg.add(var.set_warm_white_temperature(config[CONF_WARM_WHITE_COLOR_TEMPERATURE]))
    cg.add(var.set_rgb_white_temperature(config[CONF_RGB_WHITE_COLOR_TEMPERATURE]))
    cg.add(var.set_white_extraction(config[CONF_WHITE_EXTRACTION]))
    cg.add(var.set_minimum_brightness(config[CONF_MINIMUM_BRIGHTNESS]))

    cg.add(var.set_current_budget_ma(config[CONF_CURRENT_BUDGET_MA]))
    cg.add(var.set_red_current_ma(config[CONF_RED_CURRENT_MA]))
    cg.add(var.set_green_current_ma(config[CONF_GREEN_CURRENT_MA]))
    cg.add(var.set_blue_current_ma(config[CONF_BLUE_CURRENT_MA]))
    cg.add(var.set_cold_white_current_ma(config[CONF_COLD_WHITE_CURRENT_MA]))
    cg.add(var.set_warm_white_current_ma(config[CONF_WARM_WHITE_CURRENT_MA]))
