#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <esp_gattc_api.h>
#include <esp_gap_ble_api.h>
#include <esp_bt_defs.h>

#include <vector>

namespace esphome {
namespace walkingpad {

namespace espbt = esphome::esp32_ble_tracker;

static const char *const TAG = "walkingpad";

static const uint16_t SERVICE_UUID       = 0xFBA0;
static const uint16_t CHAR_WRITE_UUID    = 0xFBA1;

static const uint8_t CMD_STOP            = 0x00;
static const uint8_t CMD_START_SET_SPEED = 0x04;

static const float STOP_THRESHOLD_KMH    = 0.1f;

enum class TreadmillStatus { UNKNOWN, COUNTDOWN, RUNNING, PAUSED, STOPPED, DISCONNECTED };

// The component only handles the WRITE characteristic (commands).
// NOTIFICATIONS are subscribed via ESPHome's built-in `ble_client` sensor
// platform (YAML side) which calls on_notification() with raw bytes.
class WalkingPadComponent : public esphome::Component,
                             public esphome::ble_client::BLEClientNode {
 public:
  void set_speed_feedback_sensor(sensor::Sensor *s) { speed_feedback_ = s; }
  void set_distance_sensor(sensor::Sensor *s)       { distance_ = s; }
  void set_duration_sensor(sensor::Sensor *s)       { duration_ = s; }
  void set_calories_sensor(sensor::Sensor *s)       { calories_ = s; }
  void set_steps_sensor(sensor::Sensor *s)          { steps_ = s; }
  void set_max_speed_sensor(sensor::Sensor *s)      { max_speed_ = s; }
  void set_firmware_sensor(sensor::Sensor *s)       { firmware_ = s; }
  void set_state_sensor(text_sensor::TextSensor *s) { state_ = s; }

  // Called from the YAML `ble_client` sensor lambda whenever a notification
  // packet is received from the treadmill.
  void on_notification(const std::vector<uint8_t> &data) {
    last_data_ms_ = millis();
    parse_data_(data.data(), static_cast<uint16_t>(data.size()));
  }

  // ── Commands ─────────────────────────────────────────────────────────────────

  void set_speed(float speed_kmh) {
    if (speed_kmh <= STOP_THRESHOLD_KMH) {
      if (send_command_(CMD_STOP, 0)) is_running_ = false;
    } else {
      uint16_t speed = static_cast<uint16_t>(speed_kmh * 1000.0f);
      if (send_command_(CMD_START_SET_SPEED, speed)) is_running_ = true;
    }
  }

  // Single play/stop button. Uses is_running_ as source of truth; parse_data_
  // keeps it in sync with the real device state once notifications arrive.
  void start_stop_toggle() {
    if (is_running_) {
      ESP_LOGI(TAG, "Toggle → stop");
      if (send_command_(CMD_STOP, 0)) is_running_ = false;
    } else {
      ESP_LOGI(TAG, "Toggle → start");
      if (send_command_(CMD_START_SET_SPEED, 0)) is_running_ = true;
    }
  }

  // ── ESPHome lifecycle ────────────────────────────────────────────────────────
  float get_setup_priority() const override { return setup_priority::DATA; }

  void dump_config() override {
    ESP_LOGCONFIG(TAG, "WalkingPad BLE component (commands via 0x%04X)", CHAR_WRITE_UUID);
  }

  // ── BLE GATTC event handler ──────────────────────────────────────────────────
  void gattc_event_handler(esp_gattc_cb_event_t event,
                            esp_gatt_if_t gattc_if,
                            esp_ble_gattc_cb_param_t *param) override {
    switch (event) {

      case ESP_GATTC_OPEN_EVT:
        if (param->open.status == ESP_GATT_OK) {
          ESP_LOGI(TAG, "Connected to WalkingPad");
          // Match original NimBLE connection params:
          // 15–30 ms interval, no latency, 6 s supervision timeout.
          esp_ble_conn_update_params_t cp{};
          memcpy(cp.bda, param->open.remote_bda, sizeof(esp_bd_addr_t));
          cp.min_int = 12;
          cp.max_int = 24;
          cp.latency  = 0;
          cp.timeout  = 600;
          esp_ble_gap_update_conn_params(&cp);
        } else {
          ESP_LOGW(TAG, "Connection failed, status=%d", param->open.status);
        }
        break;

      case ESP_GATTC_DISCONNECT_EVT:
        write_handle_ = 0;
        ready_ = false;
        is_running_ = false;
        treadmill_status_ = TreadmillStatus::DISCONNECTED;
        publish_state_("disconnected");
        ESP_LOGI(TAG, "Disconnected from WalkingPad");
        break;

      case ESP_GATTC_SEARCH_CMPL_EVT:
        if (param->search_cmpl.status != ESP_GATT_OK) {
          ESP_LOGE(TAG, "Service discovery failed, status=%d",
                   param->search_cmpl.status);
          break;
        }
        find_write_characteristic_();
        break;

      default:
        break;
    }
  }

 protected:
  uint16_t        write_handle_{0};
  bool            ready_{false};
  bool            is_running_{false};
  uint32_t        last_data_ms_{0};
  TreadmillStatus treadmill_status_{TreadmillStatus::UNKNOWN};

  sensor::Sensor          *speed_feedback_{nullptr};
  sensor::Sensor          *distance_{nullptr};
  sensor::Sensor          *duration_{nullptr};
  sensor::Sensor          *calories_{nullptr};
  sensor::Sensor          *steps_{nullptr};
  sensor::Sensor          *max_speed_{nullptr};
  sensor::Sensor          *firmware_{nullptr};
  text_sensor::TextSensor *state_{nullptr};

  void publish_state_(const char *s) {
    if (state_) state_->publish_state(s);
  }

  void find_write_characteristic_() {
    using esphome::esp32_ble_tracker::ESPBTUUID;
    auto *chr = parent()->get_characteristic(
        ESPBTUUID::from_uint16(SERVICE_UUID),
        ESPBTUUID::from_uint16(CHAR_WRITE_UUID));
    if (!chr) {
      ESP_LOGE(TAG, "Write characteristic 0x%04X not found", CHAR_WRITE_UUID);
      return;
    }
    write_handle_ = chr->handle;
    ready_ = true;
    this->node_state = espbt::ClientState::ESTABLISHED;
    last_data_ms_ = millis();
    ESP_LOGI(TAG, "Write characteristic ready (handle=%d)", write_handle_);
    if (treadmill_status_ == TreadmillStatus::UNKNOWN ||
        treadmill_status_ == TreadmillStatus::DISCONNECTED) {
      treadmill_status_ = TreadmillStatus::STOPPED;
      publish_state_("stopped");
    }
  }

  bool send_command_(uint8_t cmd_type, uint16_t speed) {
    if (!ready_ || !write_handle_) {
      ESP_LOGW(TAG, "Cannot send command: not ready");
      return false;
    }
    uint8_t pkt[23];
    make_packet_(cmd_type, speed, pkt);
    esp_err_t err = esp_ble_gattc_write_char(
        parent()->get_gattc_if(), parent()->get_conn_id(), write_handle_,
        sizeof(pkt), pkt, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Write failed: %s", esp_err_to_name(err));
      return false;
    }
    return true;
  }

  static void make_packet_(uint8_t cmd_type, uint16_t speed, uint8_t *pkt) {
    pkt[0] = 0x6A; pkt[1] = 0x17;
    for (int i = 2; i <= 5; ++i) pkt[i] = 0;
    pkt[6]  = (speed >> 8) & 0xFF;
    pkt[7]  = speed & 0xFF;
    pkt[8]  = (speed != 0) ? 5 : 1;
    pkt[9]  = 0; pkt[10] = 80; pkt[11] = 0;
    pkt[12] = cmd_type & 0xF7;
    static const uint64_t USER_ID = 58965456623ULL;
    for (int i = 0; i < 8; ++i)
      pkt[13 + i] = static_cast<uint8_t>((USER_ID >> (56 - i * 8)) & 0xFF);
    uint8_t checksum = 0;
    for (int i = 1; i <= 20; ++i) checksum ^= pkt[i];
    pkt[21] = checksum;
    pkt[22] = 0x43;
  }

  void parse_data_(const uint8_t *d, uint16_t len) {
    if (len < 31) { ESP_LOGW(TAG, "Packet too short: %d bytes", len); return; }

    auto r16 = [](const uint8_t *b, int o) -> uint16_t {
      return (static_cast<uint16_t>(b[o]) << 8) | b[o + 1];
    };
    auto r32 = [](const uint8_t *b, int o) -> uint32_t {
      return (static_cast<uint32_t>(b[o])     << 24) |
             (static_cast<uint32_t>(b[o + 1]) << 16) |
             (static_cast<uint32_t>(b[o + 2]) <<  8) |
              static_cast<uint32_t>(b[o + 3]);
    };

    float    speed_fb = r16(d, 3)  / 1000.0f;
    float    distance = r32(d, 7)  / 1000.0f;
    uint32_t steps    = r32(d, 14);
    uint16_t calories = r16(d, 18);
    float    duration = r32(d, 20) / 1000.0f;
    uint8_t  fw       = d[25];
    uint8_t  flags    = d[26];
    float    max_spd  = r16(d, 27) / 1000.0f;

    const uint8_t state_bits = flags & 0x18;
    const char *state_str;
    if      (state_bits == 0x18) { state_str = "countdown"; treadmill_status_ = TreadmillStatus::COUNTDOWN; is_running_ = true;  }
    else if (state_bits == 0x08) { state_str = "running";   treadmill_status_ = TreadmillStatus::RUNNING;   is_running_ = true;  }
    else if (state_bits == 0x10) { state_str = "paused";    treadmill_status_ = TreadmillStatus::PAUSED;    is_running_ = false; }
    else                         { state_str = "stopped";   treadmill_status_ = TreadmillStatus::STOPPED;   is_running_ = false; }

    if (speed_feedback_) speed_feedback_->publish_state(speed_fb);
    if (distance_)       distance_->publish_state(distance);
    if (steps_)          steps_->publish_state(static_cast<float>(steps));
    if (calories_)       calories_->publish_state(static_cast<float>(calories));
    if (duration_)       duration_->publish_state(duration);
    if (firmware_)       firmware_->publish_state(static_cast<float>(fw));
    if (max_speed_)      max_speed_->publish_state(max_spd);
    publish_state_(state_str);

    ESP_LOGD(TAG, "speed=%.2f  dist=%.3f  dur=%.0f  state=%s",
             speed_fb, distance, duration, state_str);
  }
};

}  // namespace walkingpad
}  // namespace esphome
