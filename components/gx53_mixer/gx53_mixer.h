#pragma once

#include <algorithm>

#include "esphome/components/json/json_util.h"
#include "esphome/components/light/light_json_schema.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_transformer.h"
#include "esphome/components/output/float_output.h"
#include "gx53_mixer_math.h"

namespace esphome::gx53_mixer {

class GX53MixerTransition;

class GX53MixerLightOutput final : public light::LightOutput {
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

  std::unique_ptr<light::LightTransformer> create_default_transition() override;

  void write_state(light::LightState* state) override {
    this->write_channels_(this->mix_values_(state->current_values));
  }

 protected:
  friend class GX53MixerTransition;

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
// then add one extension:
//
//   "transition_ms": 250
//
// LightCall::set_transition_length() takes milliseconds, so this preserves
// exact sub-second transitions. If both `transition` and `transition_ms` are
// present, transition_ms wins because it is applied after the standard parser.
inline void handle_mqtt_light_command(light::LightState& state, JsonObjectConst input) {
  // LightJSONSchema::parse_json() currently expects a mutable JsonObject while
  // mqtt.on_json_message supplies a const object. Make a small mutable copy.
  JsonDocument doc;
  doc.set(input);

  JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) return;

  auto call = state.make_call();

  // Standard ESPHome fields: state, brightness, color, color_temp, flash,
  // effect and whole-second `transition`.
  light::LightJSONSchema::parse_json(state, call, root);

  // Custom exact-millisecond transition extension.
  if (input["transition_ms"].is<uint32_t>()) {
    call.set_transition_length(input["transition_ms"].as<uint32_t>());
  }

  call.perform();
}

}  // namespace esphome::gx53_mixer
