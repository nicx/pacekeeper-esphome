import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, sensor, text_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_SPEED,
    DEVICE_CLASS_DISTANCE,
    DEVICE_CLASS_DURATION,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    ENTITY_CATEGORY_DIAGNOSTIC,
    UNIT_KILOMETER_PER_HOUR,
    UNIT_KILOMETER,
    UNIT_SECOND,
)

DEPENDENCIES = ["ble_client"]
AUTO_LOAD = ["sensor", "text_sensor"]

walkingpad_ns = cg.esphome_ns.namespace("walkingpad")
WalkingPadComponent = walkingpad_ns.class_(
    "WalkingPadComponent",
    cg.Component,
    ble_client.BLEClientNode,
)

CONF_SPEED_FEEDBACK = "speed_feedback"
CONF_DISTANCE = "distance"
CONF_DURATION = "duration"
CONF_CALORIES = "calories"
CONF_STEPS = "steps"
CONF_MAX_SPEED = "max_speed"
CONF_FIRMWARE_VERSION = "firmware_version"
CONF_STATE = "state"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(WalkingPadComponent),
            cv.Optional(CONF_SPEED_FEEDBACK): sensor.sensor_schema(
                unit_of_measurement=UNIT_KILOMETER_PER_HOUR,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_SPEED,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_DISTANCE): sensor.sensor_schema(
                unit_of_measurement=UNIT_KILOMETER,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_DISTANCE,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional(CONF_DURATION): sensor.sensor_schema(
                unit_of_measurement=UNIT_SECOND,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_DURATION,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional(CONF_CALORIES): sensor.sensor_schema(
                unit_of_measurement="kcal",
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                icon="mdi:fire",
            ),
            cv.Optional(CONF_STEPS): sensor.sensor_schema(
                unit_of_measurement="steps",
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                icon="mdi:shoe-print",
            ),
            cv.Optional(CONF_MAX_SPEED): sensor.sensor_schema(
                unit_of_measurement=UNIT_KILOMETER_PER_HOUR,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_SPEED,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_FIRMWARE_VERSION): sensor.sensor_schema(
                accuracy_decimals=0,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:chip",
            ),
            cv.Optional(CONF_STATE): text_sensor.text_sensor_schema(
                icon="mdi:state-machine",
            ),
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)

    sensor_map = [
        (CONF_SPEED_FEEDBACK, "set_speed_feedback_sensor"),
        (CONF_DISTANCE, "set_distance_sensor"),
        (CONF_DURATION, "set_duration_sensor"),
        (CONF_CALORIES, "set_calories_sensor"),
        (CONF_STEPS, "set_steps_sensor"),
        (CONF_MAX_SPEED, "set_max_speed_sensor"),
        (CONF_FIRMWARE_VERSION, "set_firmware_sensor"),
    ]
    for conf_key, setter in sensor_map:
        if conf_key in config:
            sens = await sensor.new_sensor(config[conf_key])
            cg.add(getattr(var, setter)(sens))

    if CONF_STATE in config:
        ts = await text_sensor.new_text_sensor(config[CONF_STATE])
        cg.add(var.set_state_sensor(ts))
