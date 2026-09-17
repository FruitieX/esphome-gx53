#pragma once

#include <cmath>
#include <cstdint>

namespace esphome::gx53_mixer {

enum class ThermalInterlockUpdate : uint8_t {
  NONE,
  LOCKED,
  UNLOCKED,
};

// A thermal trip has two independent release conditions: the minimum cooldown
// must have elapsed and the temperature must be below the reset threshold.
// The interlock starts locked so a reboot cannot energize the LEDs before the
// first trustworthy temperature reading arrives.
class ThermalInterlock {
 public:
  void set_shutdown_temperature(float value) { this->shutdown_temperature_ = value; }
  void set_reset_temperature(float value) { this->reset_temperature_ = value; }
  void set_cooldown_ms(uint32_t value) { this->cooldown_ms_ = value; }

  bool is_locked() const { return this->locked_; }
  bool cooldown_started() const { return this->cooldown_started_; }
  float shutdown_temperature() const { return this->shutdown_temperature_; }

  ThermalInterlockUpdate update(float temperature, uint32_t now) {
    if (!std::isfinite(temperature)) {
      return this->lock_(now);
    }

    if (!this->locked_) {
      if (temperature >= this->shutdown_temperature_) {
        return this->lock_(now);
      }
      return ThermalInterlockUpdate::NONE;
    }

    // At startup there is no previous thermal incident to cool down from. A
    // first reading at or below the reset threshold is enough to establish
    // that it is safe to enable the output.
    if (!this->cooldown_started_) {
      if (temperature >= this->shutdown_temperature_) {
        this->cooldown_started_ = true;
        this->locked_at_ = now;
      } else if (temperature <= this->reset_temperature_) {
        this->locked_ = false;
        return ThermalInterlockUpdate::UNLOCKED;
      }
      return ThermalInterlockUpdate::NONE;
    }

    if (temperature <= this->reset_temperature_ && now - this->locked_at_ >= this->cooldown_ms_) {
      this->locked_ = false;
      this->cooldown_started_ = false;
      return ThermalInterlockUpdate::UNLOCKED;
    }

    return ThermalInterlockUpdate::NONE;
  }

 protected:
  ThermalInterlockUpdate lock_(uint32_t now) {
    if (!this->cooldown_started_) {
      this->cooldown_started_ = true;
      this->locked_at_ = now;
    }
    if (this->locked_) return ThermalInterlockUpdate::NONE;

    this->locked_ = true;
    return ThermalInterlockUpdate::LOCKED;
  }

  float shutdown_temperature_{80.0f};
  float reset_temperature_{70.0f};
  uint32_t cooldown_ms_{60000};
  uint32_t locked_at_{0};
  bool locked_{true};
  bool cooldown_started_{false};
};

}  // namespace esphome::gx53_mixer
