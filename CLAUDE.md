# PaceKeeper ESPHome — Project Guide

## What this is

ESPHome firmware for an ESP32 that connects via Bluetooth LE to a WalkingPad treadmill (Superun/PitPat BA06-B1) and exposes it to Home Assistant via the native ESPHome API — no MQTT broker required.

This is a fork of [peteh/pacekeeper](https://github.com/peteh/pacekeeper), which used PlatformIO + MQTT. This fork replaces that stack with an ESPHome custom component.

**Repo:** https://github.com/nicx/pacekeeper-esphome  
**Author:** nicx (25152397+nicx@users.noreply.github.com)

## Repo layout

```
esphome/
  esp-pacekeeper.example.yaml  # Config template — copy to esp-pacekeeper.yaml and fill in secrets
  components/walkingpad/
    __init__.py                # ESPHome component registration
    walkingpad.h               # BLE client logic + packet parsing
doc/
  ha_device.png                # Screenshot of HA device page
```

The real `esp-pacekeeper.yaml` (with WiFi credentials and the treadmill MAC) is gitignored so secrets stay local.

## Key technical details

- BLE WRITE characteristic: `0xFBA1` — 23-byte speed/stop commands with XOR checksum
- BLE NOTIFY characteristic: `0xFBA2` — binary packets parsed in `WalkingPadComponent::on_notification()`
- Connection params: 15–30 ms interval, 6 s supervision timeout (matches vendor expectations, prevents ~30 s idle disconnect)
- Custom component inherits from `ble_client.BLEClientNode`; NOTIFY is subscribed via ESPHome's built-in `ble_client` sensor platform (YAML side), which forwards raw bytes into `on_notification()`

## Entities exposed to Home Assistant

| Entity           | Type        | Notes                               |
|------------------|-------------|-------------------------------------|
| Speed            | sensor      | current km/h                        |
| Distance         | sensor      | total km                            |
| Duration         | sensor      | seconds                             |
| Calories         | sensor      | kcal                                |
| Steps            | sensor      | not all firmwares report            |
| Max Speed        | sensor      | device-configured max               |
| Firmware Version | sensor      | treadmill firmware string           |
| State            | text_sensor | running / paused / stopped / …      |
| Speed Control    | number      | slider 0–6 km/h → sets target speed |
| Start-Stop       | button      | toggles run/stop                    |

## Setup (quick reference)

1. Find treadmill BT address via nRF Connect (device name: `PitPat-T01`)
2. `cp esphome/esp-pacekeeper.example.yaml esphome/esp-pacekeeper.yaml`, then copy `esphome/esp-pacekeeper.yaml` + `esphome/components/walkingpad/` into your ESPHome config directory
3. Edit WiFi credentials and `mac_address` in the YAML
4. `esphome run esp-pacekeeper.yaml` (first flash via USB, then OTA)

## Cloud-free treadmill init

If the treadmill beeps annoyingly on button press after power-on, it needs to be unlocked:
- Power on → immediately press **(+)** → then **−, −, −, +** → hold **(+) 3 s**
- Each correct step gives a short happy beep.

Source: https://www.reddit.com/r/treadmills/comments/1jtuwix/
