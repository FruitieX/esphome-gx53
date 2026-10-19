#pragma once

#include <cstdint>

namespace esphome::gx53_mixer {

// Connectivity problems, shown as a faint color while the light is off.
enum class StatusIndication : uint8_t {
  NONE,         // Connected; the light shows only what the controller asks for.
  NO_WIFI,      // Station not connected: red.
  FALLBACK_AP,  // Station not connected and the fallback AP is up: magenta.
  NO_MQTT,      // Wi-Fi connected but the MQTT broker is not: blue.
};

inline StatusIndication classify_status(bool wifi_connected, bool ap_active, bool mqtt_connected) {
  if (!wifi_connected) return ap_active ? StatusIndication::FALLBACK_AP : StatusIndication::NO_WIFI;
  if (!mqtt_connected) return StatusIndication::NO_MQTT;
  return StatusIndication::NONE;
}

inline const char* status_name(StatusIndication status) {
  switch (status) {
    case StatusIndication::NONE:
      return "none";
    case StatusIndication::NO_WIFI:
      return "no Wi-Fi (red)";
    case StatusIndication::FALLBACK_AP:
      return "fallback AP (magenta)";
    case StatusIndication::NO_MQTT:
      return "no MQTT (blue)";
  }
  return "unknown";
}

// Until the device has been fully connected once, every problem is shown at
// once so a bulb that never comes online is visible immediately. Afterwards a
// problem must persist for the delay before it is shown, so brief Wi-Fi or
// broker outages don't make the bulbs glow. Once a problem is shown, changes
// between problem types and recovery are shown immediately.
class StatusIndicatorFilter {
 public:
  void set_delay_ms(uint32_t value) { this->delay_ms_ = value; }
  StatusIndication shown() const { return this->shown_; }

  // Returns true when the shown indication changed.
  bool update(StatusIndication status, uint32_t now) {
    if (status == StatusIndication::NONE) {
      this->connected_once_ = true;
      this->in_problem_ = false;
      return this->show_(status);
    }

    if (!this->in_problem_) {
      this->in_problem_ = true;
      this->problem_since_ = now;
    }
    if (this->connected_once_ && this->shown_ == StatusIndication::NONE && now - this->problem_since_ < this->delay_ms_)
      return false;
    return this->show_(status);
  }

 protected:
  bool show_(StatusIndication status) {
    if (status == this->shown_) return false;
    this->shown_ = status;
    return true;
  }

  uint32_t delay_ms_{30000};
  uint32_t problem_since_{0};
  StatusIndication shown_{StatusIndication::NONE};
  bool connected_once_{false};
  bool in_problem_{false};
};

}  // namespace esphome::gx53_mixer
