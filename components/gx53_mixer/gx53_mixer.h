#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/light/light_json_schema.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_transformer.h"
#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/components/output/float_output.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "gx53_mixer_math.h"
#include "thermal_interlock.h"

namespace esphome::gx53_mixer {

static const char *const TAG = "gx53_mixer";

class GX53MixerTransition;

class GX53MixerLightOutput final : public Component, public light::LightOutput {
 public:
  void set_red(output::FloatOutput* red) { this->red_ = red; }
  void set_green(output::FloatOutput* green) { this->green_ = green; }
  void set_blue(output::FloatOutput* blue) { this->blue_ = blue; }
  void set_cold_white(output::FloatOutput* cold_white) { this->cold_white_ = cold_white; }
  void set_warm_white(output::FloatOutput* warm_white) { this->warm_white_ = warm_white; }

  void set_cold_white_temperature(float value) { this->cold_white_temperature_ = value; }
  void set_warm_white_temperature(float value) { this->warm_white_temperature_ = value; }
  void set_rgb_white_temperature(float value) { this->rgb_white_temperature_ = value; }
  void set_white_extraction(float value) { this->white_extraction_ = value; }
  void set_minimum_brightness(float value) { this->minimum_brightness_ = value; }

  void set_current_budget_ma(float value) { this->current_budget_ma_ = value; }
  void set_red_current_ma(float value) { this->red_current_ma_ = value; }
  void set_green_current_ma(float value) { this->green_current_ma_ = value; }
  void set_blue_current_ma(float value) { this->blue_current_ma_ = value; }
  void set_cold_white_current_ma(float value) { this->cold_white_current_ma_ = value; }
  void set_warm_white_current_ma(float value) { this->warm_white_current_ma_ = value; }

  void set_temperature_sensor(sensor::Sensor* value) { this->temperature_sensor_ = value; }
  void set_thermal_shutdown_temperature(float value) { this->thermal_interlock_.set_shutdown_temperature(value); }
  void set_thermal_reset_temperature(float value) { this->thermal_interlock_.set_reset_temperature(value); }
  void set_thermal_cooldown_ms(uint32_t value) { this->thermal_interlock_.set_cooldown_ms(value); }
  void set_thermal_lockout_sensor(binary_sensor::BinarySensor* value) { this->thermal_lockout_sensor_ = value; }

  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();

    // Externally this behaves like a normal RGB + color-temperature light.
    // The RGB -> RGB + CW/WW decomposition is entirely internal.
    traits.set_supported_color_modes({light::ColorMode::RGB, light::ColorMode::COLD_WARM_WHITE});
    traits.set_min_mireds(this->cold_white_temperature_);
    traits.set_max_mireds(this->warm_white_temperature_);
    return traits;
  }

  void setup_state(light::LightState* state) override { this->state_ = state; }

  // Run just after ESPHome's stock MQTT light component. The stock component
  // remains active for state publishing and discovery, but its command
  // subscription is replaced with the parser below so transition_ms has one
  // authoritative command path.
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION - 1.0f; }
  void setup() override;
  void loop() override;

  std::unique_ptr<light::LightTransformer> create_default_transition() override;

  void write_state(light::LightState* state) override {
    if (this->thermal_interlock_.is_locked()) {
      this->write_channels_({});
      return;
    }
    this->write_channels_(this->mix_values_(state->current_values));
  }

 protected:
  friend class GX53MixerTransition;

  void handle_mqtt_light_command_(JsonObjectConst input);
  bool mqtt_command_requests_on_(JsonObjectConst input) const;
  bool force_off_();
  void handle_temperature_(float temperature);
  void apply_thermal_update_(ThermalInterlockUpdate update);

  float gamma_correct_(float value) const {
    return this->state_ == nullptr ? math::clamp01(value) : this->state_->gamma_correct_lut(math::clamp01(value));
  }

  math::ChannelLevels mix_values_(const light::LightColorValues& values) const {
    math::ChannelLevels levels;
    const float logical_master = values.get_state() * values.get_brightness();
    const float effective_logical_master = math::remap_brightness(logical_master, this->minimum_brightness_);
    if (effective_logical_master <= 0.0f) return levels;
    const float corrected_master = this->gamma_correct_(effective_logical_master);

    const auto mode = values.get_color_mode();
    if (mode == light::ColorMode::RGB) {
      // Keep the master brightness separate from color. Besides making the
      // floor common to all channels, this avoids low-end gamma-LUT
      // quantization changing RGB ratios before the floor is applied.
      const float color_brightness = values.get_color_brightness();
      levels.red = this->gamma_correct_(color_brightness * values.get_red());
      levels.green = this->gamma_correct_(color_brightness * values.get_green());
      levels.blue = this->gamma_correct_(color_brightness * values.get_blue());

      // Extract the neutral/common RGB component and render it with the white
      // LEDs instead. Saturated colors remain RGB; desaturated colors
      // progressively use more CW/WW.
      const float common = std::min({levels.red, levels.green, levels.blue}) * this->white_extraction_;
      levels.red -= common;
      levels.green -= common;
      levels.blue -= common;

      const auto white = math::white_channels(this->rgb_white_temperature_, common, this->cold_white_temperature_,
                                              this->warm_white_temperature_);
      levels.cold_white = white.cold_white;
      levels.warm_white = white.warm_white;
      math::scale(levels, corrected_master);
    } else if (mode == light::ColorMode::COLD_WARM_WHITE) {
      // Derive both white channels from the requested color temperature and
      // one common master level. CW+WW therefore sums to the same level at
      // every point, including while color temperature is being interpolated.
      levels = math::white_channels(values.get_color_temperature(), corrected_master, this->cold_white_temperature_,
                                    this->warm_white_temperature_);
    }

    return levels;
  }

  math::ChannelCurrents currents_() const {
    return {this->red_current_ma_, this->green_current_ma_, this->blue_current_ma_, this->cold_white_current_ma_,
            this->warm_white_current_ma_};
  }

  math::ChannelLevels limited_(math::ChannelLevels levels) const {
    math::apply_current_limit(levels, this->currents_(), this->current_budget_ma_);
    return levels;
  }

  void write_channels_(math::ChannelLevels levels) {
    // Transitions write channel vectors directly, bypassing write_state().
    // Clamp here as the final safety boundary so no control path can energize
    // an LED while the thermal interlock is active.
    if (this->thermal_interlock_.is_locked()) levels = {};

    // This is intentionally the final calculation. It covers normal writes and
    // every custom-transition frame, so no intermediate vector can exceed the
    // configured aggregate current budget.
    levels = this->limited_(levels);
    levels.red = math::clamp01(levels.red);
    levels.green = math::clamp01(levels.green);
    levels.blue = math::clamp01(levels.blue);
    levels.cold_white = math::clamp01(levels.cold_white);
    levels.warm_white = math::clamp01(levels.warm_white);
    this->last_output_ = levels;

    this->red_->set_level(levels.red);
    this->green_->set_level(levels.green);
    this->blue_->set_level(levels.blue);
    this->cold_white_->set_level(levels.cold_white);
    this->warm_white_->set_level(levels.warm_white);
  }

  output::FloatOutput* red_{nullptr};
  output::FloatOutput* green_{nullptr};
  output::FloatOutput* blue_{nullptr};
  output::FloatOutput* cold_white_{nullptr};
  output::FloatOutput* warm_white_{nullptr};

  // ESPHome stores color temperatures in mireds internally.
  float cold_white_temperature_{153.846f};  // 6500 K
  float warm_white_temperature_{370.370f};  // 2700 K
  float rgb_white_temperature_{250.0f};     // 4000 K
  float white_extraction_{1.0f};
  float minimum_brightness_{0.0f};

  float current_budget_ma_{12.0f};
  float red_current_ma_{12.0f};
  float green_current_ma_{12.0f};
  float blue_current_ma_{12.0f};
  float cold_white_current_ma_{12.0f};
  float warm_white_current_ma_{12.0f};

  light::LightState* state_{nullptr};
  math::ChannelLevels last_output_{};
  std::string mqtt_command_topic_{};
  bool mqtt_command_intercepted_{false};

  sensor::Sensor* temperature_sensor_{nullptr};
  binary_sensor::BinarySensor* thermal_lockout_sensor_{nullptr};
  ThermalInterlock thermal_interlock_{};
  float last_temperature_{NAN};
  bool has_temperature_sample_{false};
};

class GX53MixerTransition final : public light::LightTransformer {
 public:
  explicit GX53MixerTransition(GX53MixerLightOutput& output) : output_(output) {}

  void start() override {
    this->logical_start_ = this->start_values_;
    this->logical_end_ = this->target_values_;

    // Match ESPHome's useful on/off behavior: use the target color while
    // turning on and the source color while turning off. Only brightness goes
    // to zero; unlike ESPHome's default color-mode transition, an RGB<->CCT
    // change never routes through an artificial OFF midpoint.
    if (!this->start_values_.is_on() && this->target_values_.is_on()) {
      this->logical_start_ = this->target_values_;
      this->logical_start_.set_brightness(0.0f);
    } else if (this->start_values_.is_on() && !this->target_values_.is_on()) {
      this->logical_end_ = this->start_values_;
      this->logical_end_.set_brightness(0.0f);
    }

    this->start_channels_ = this->output_.last_output_;
    this->end_channels_ = this->output_.mix_values_(this->logical_end_);

    // For an uninterrupted CCT transition, interpolate color temperature and
    // master brightness, then derive CW/WW on each frame. If a previous direct
    // transition was interrupted, start from the actual physical vector to
    // avoid a jump caused by LightState current_values being stale during a
    // transformer that writes directly to hardware.
    const auto expected_start = this->output_.limited_(this->output_.mix_values_(this->logical_start_));
    this->parametric_cct_ = this->start_values_.is_on() && this->target_values_.is_on() &&
                            this->logical_start_.get_color_mode() == light::ColorMode::COLD_WARM_WHITE &&
                            this->logical_end_.get_color_mode() == light::ColorMode::COLD_WARM_WHITE &&
                            math::nearly_equal(this->start_channels_, expected_start);
  }

  optional<light::LightColorValues> apply() override {
    const float progress = light::LightTransformer::smoothed_progress(this->get_progress_());
    if (this->parametric_cct_) {
      const auto values = light::LightColorValues::lerp(this->logical_start_, this->logical_end_, progress);
      this->output_.write_channels_(this->output_.mix_values_(values));
    } else {
      this->output_.write_channels_(math::lerp(this->start_channels_, this->end_channels_, progress));
    }
    return {};
  }

 protected:
  GX53MixerLightOutput& output_;
  light::LightColorValues logical_start_{};
  light::LightColorValues logical_end_{};
  math::ChannelLevels start_channels_{};
  math::ChannelLevels end_channels_{};
  bool parametric_cct_{false};
};

inline std::unique_ptr<light::LightTransformer> GX53MixerLightOutput::create_default_transition() {
  return make_unique<GX53MixerTransition>(*this);
}

// Parse a normal ESPHome MQTT light command using ESPHome's own JSON schema,
// enforce the thermal interlock, then add one extension:
//
//   "transition_ms": 250
//
// LightCall::set_transition_length() takes milliseconds, so this preserves
// exact sub-second transitions. If both `transition` and `transition_ms` are
// present, transition_ms wins because it is applied after the standard parser.
inline void GX53MixerLightOutput::handle_mqtt_light_command_(JsonObjectConst input) {
  if (this->state_ == nullptr) return;
  if (this->thermal_interlock_.is_locked() && this->mqtt_command_requests_on_(input)) {
    ESP_LOGW(TAG, "Rejected MQTT turn-on command while thermal lockout is active");
    return;
  }

  // LightJSONSchema::parse_json() currently expects a mutable JsonObject while
  // mqtt.on_json_message supplies a const object. Make a small mutable copy.
  JsonDocument doc;
  doc.set(input);

  JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) return;

  auto call = this->state_->make_call();

  // Standard ESPHome fields: state, brightness, color, color_temp, flash,
  // effect and whole-second `transition`.
  light::LightJSONSchema::parse_json(*this->state_, call, root);

  // Custom exact-millisecond transition extension.
  if (input["transition_ms"].is<uint32_t>()) {
    call.set_transition_length(input["transition_ms"].as<uint32_t>());
  }

  call.perform();
}

inline bool GX53MixerLightOutput::mqtt_command_requests_on_(JsonObjectConst input) const {
  if (!input[ESPHOME_F("state")].is<const char*>()) return false;

  switch (parse_on_off(input[ESPHOME_F("state")].as<const char*>())) {
    case PARSE_ON:
      return true;
    case PARSE_TOGGLE:
      return this->state_ == nullptr || !this->state_->remote_values.is_on();
    case PARSE_OFF:
    case PARSE_NONE:
      return false;
  }
  return false;
}

inline bool GX53MixerLightOutput::force_off_() {
  if (this->state_ == nullptr || !this->state_->remote_values.is_on()) return false;

  auto call = this->state_->make_call();
  call.set_state(false);
  call.set_transition_length(0);
  call.set_save(false);
  call.perform();
  return true;
}

inline void GX53MixerLightOutput::apply_thermal_update_(ThermalInterlockUpdate update) {
  switch (update) {
    case ThermalInterlockUpdate::LOCKED:
      ESP_LOGW(TAG, "Thermal lockout activated; forcing light off");
      if (this->thermal_lockout_sensor_ != nullptr) this->thermal_lockout_sensor_->publish_state(true);
      this->force_off_();
      break;
    case ThermalInterlockUpdate::UNLOCKED: {
      ESP_LOGI(TAG, "Thermal lockout cleared; light may be turned on again");
      if (this->thermal_lockout_sensor_ != nullptr) this->thermal_lockout_sensor_->publish_state(false);
      const bool state_was_published = this->force_off_();
      // Re-advertise OFF after rearming. Controllers such as homectl can then
      // retry an expected ON state that was rejected during the lockout.
      if (!state_was_published && this->state_ != nullptr) this->state_->publish_state();
      break;
    }
    case ThermalInterlockUpdate::NONE:
      break;
  }
}

inline void GX53MixerLightOutput::handle_temperature_(float temperature) {
  this->last_temperature_ = temperature;
  this->has_temperature_sample_ = true;
  const bool cooldown_was_started = this->thermal_interlock_.cooldown_started();
  const auto update = this->thermal_interlock_.update(temperature, millis());

  if (!cooldown_was_started && !std::isfinite(temperature)) {
    ESP_LOGW(TAG, "Temperature reading is invalid; thermal lockout remains active");
  } else if (!cooldown_was_started && temperature >= this->thermal_interlock_.shutdown_temperature()) {
    ESP_LOGW(TAG, "Thermal shutdown at %.1f C", temperature);
  }
  this->apply_thermal_update_(update);
}

inline void GX53MixerLightOutput::setup() {
  if (this->state_ == nullptr) return;

  if (this->thermal_lockout_sensor_ != nullptr) this->thermal_lockout_sensor_->publish_initial_state(true);
  if (this->temperature_sensor_ != nullptr) {
    this->temperature_sensor_->add_on_state_callback([this](float temperature) {
      this->handle_temperature_(temperature);
    });
    if (this->temperature_sensor_->has_state()) this->handle_temperature_(this->temperature_sensor_->state);
  }

  if (mqtt::global_mqtt_client == nullptr) return;

  this->mqtt_command_topic_ = mqtt::global_mqtt_client->get_topic_prefix();
  this->mqtt_command_topic_ += "/light/";
  std::array<char, OBJECT_ID_MAX_LEN> object_id_buf{};
  const auto object_id = this->state_->get_object_id_to(object_id_buf);
  this->mqtt_command_topic_.append(object_id.c_str(), object_id.size());
  this->mqtt_command_topic_ += "/command";
}

inline void GX53MixerLightOutput::loop() {
  if (this->has_temperature_sample_ && this->thermal_interlock_.is_locked()) {
    this->apply_thermal_update_(this->thermal_interlock_.update(this->last_temperature_, millis()));
  }
  if (this->thermal_interlock_.is_locked()) this->force_off_();

  if (this->mqtt_command_intercepted_ || this->mqtt_command_topic_.empty() || mqtt::global_mqtt_client == nullptr ||
      !mqtt::global_mqtt_client->is_connected())
    return;

  // MQTTJSONLightComponent subscribed during its setup at the immediately
  // higher priority. Wait until MQTT is connected before touching the backend:
  // unsubscribe() is not safe during the client's asynchronous startup on
  // BK7231N. The callback list survives later reconnects.
  mqtt::global_mqtt_client->unsubscribe(this->mqtt_command_topic_);
  mqtt::global_mqtt_client->subscribe_json(
      this->mqtt_command_topic_,
      [this](const std::string&, JsonObject root) { this->handle_mqtt_light_command_(root); });
  this->mqtt_command_intercepted_ = true;
}

}  // namespace esphome::gx53_mixer
