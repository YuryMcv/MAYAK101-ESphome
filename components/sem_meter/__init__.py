import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, sensor
from esphome.const import CONF_ID, CONF_ADDRESS, CONF_PASSWORD

CODEOWNERS = ["@YuryMCV"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor"]

CONF_COMMAND = "command"
CONF_SENSORS = "sensors"

sem_meter_ns = cg.esphome_ns.namespace("sem_meter")
SEMMeter = sem_meter_ns.class_("SEMMeter", cg.PollingComponent, uart.UARTDevice)

# Схема для сенсора
SENSOR_SCHEMA = sensor._SENSOR_SCHEMA.extend({
    cv.Required(CONF_COMMAND): cv.string,
})

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(SEMMeter),
    cv.Optional(CONF_ADDRESS, default=1): cv.int_range(min=0, max=999),
    cv.Optional(CONF_PASSWORD, default="00000"): cv.string,
    cv.Optional(CONF_SENSORS): cv.ensure_list(SENSOR_SCHEMA),
}).extend(cv.polling_component_schema("30s")).extend(uart.UART_DEVICE_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    
    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_password(config[CONF_PASSWORD]))
    
    if CONF_SENSORS in config:
        for sensor_config in config[CONF_SENSORS]:
            sens = await sensor.new_sensor(sensor_config)
            cg.add(var.add_sensor(sens, sensor_config[CONF_COMMAND]))
