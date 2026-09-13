#pragma once

#include <algorithm>

#include "esphome/components/json/json_util.h"
#include "esphome/components/light/light_json_schema.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/output/float_output.h"

namespace esphome::gx53_mixer {

class GX53MixerLightOutput final : public light::LightOutput {
 public:
  void set_red(output::FloatOutput *red) { this->red_ = red; }
  void set_green(output::FloatOutput *green) { this->green_ = green; }
  void set_blue(output::FloatOutput *blue) { this->blue_ = blue; }
  void set_cold_white(output::FloatOutput *cold_white) { this->cold_white_ = cold_white; }
  void set_warm_white(output::FloatOutput *warm_white) { this->warm_white_ = warm_white; }

  void set_cold_white_temperature(float value) { this->cold_white_temperature_ = value; }
  void set_warm_white_temperature(float value) { this->warm_white_temperature_ = value; }
  void set_rgb_white_temperature(float value) { this->rgb_white_temperature_ = value; }
  void set_white_extraction(float value) { this->white_extraction_ = value; }

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

  void write_state(light::LightState *state) override {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float cold_white = 0.0f;
    float warm_white = 0.0f;

    const auto mode = state->current_values.get_color_mode();

    if (mode == light::ColorMode::RGB) {
      // Includes on/off, master brightness, color brightness, transitions and
      // ESPHome's normal light-value processing.
      state->current_values_as_rgb(&red, &green, &blue);

      // Extract the neutral/common RGB component and render it with the white
      // LEDs instead. Saturated colors therefore remain RGB, while desaturated
      // colors progressively use more CW/WW.
      //
      // Examples with white_extraction == 1:
      //   (1.0, 1.0, 1.0) -> RGB (0, 0, 0) + white 1.0
      //   (1.0, 0.7, 0.7) -> RGB (0.3, 0, 0) + white 0.7
      //   (1.0, 0.0, 0.0) -> RGB unchanged
      const float common = std::min({red, green, blue}) * this->white_extraction_;
      red -= common;
      green -= common;
      blue -= common;

      this->mix_white_(this->rgb_white_temperature_, common, &cold_white, &warm_white);
    } else if (mode == light::ColorMode::COLD_WARM_WHITE) {
      // Let ESPHome calculate the requested CW/WW ratio from color temperature.
      // constant_brightness is deliberately false here: our global limiter below
      // is the final authority on aggregate output/current.
      state->current_values_as_cwww(&cold_white, &warm_white, false);
    }

    // Approximate aggregate-current limiter. The configured *_current_ma values
    // MUST match the corresponding BP5758D output `current:` values.
    const float requested_ma =
        red * this->red_current_ma_ +
        green * this->green_current_ma_ +
        blue * this->blue_current_ma_ +
        cold_white * this->cold_white_current_ma_ +
        warm_white * this->warm_white_current_ma_;

    if (requested_ma > this->current_budget_ma_ && requested_ma > 0.0f) {
      const float scale = this->current_budget_ma_ / requested_ma;
      red *= scale;
      green *= scale;
      blue *= scale;
      cold_white *= scale;
      warm_white *= scale;
    }

    this->red_->set_level(this->clamp01_(red));
    this->green_->set_level(this->clamp01_(green));
    this->blue_->set_level(this->clamp01_(blue));
    this->cold_white_->set_level(this->clamp01_(cold_white));
    this->warm_white_->set_level(this->clamp01_(warm_white));
  }

 protected:
  static float clamp01_(float value) { return std::max(0.0f, std::min(1.0f, value)); }

  void mix_white_(float mireds, float level, float *cold_white, float *warm_white) const {
    const float span = this->warm_white_temperature_ - this->cold_white_temperature_;
    if (span <= 0.0f || level <= 0.0f) {
      *cold_white = 0.0f;
      *warm_white = 0.0f;
      return;
    }

    // Mireds increase as the requested white gets warmer.
    const float warm_fraction =
        clamp01_((mireds - this->cold_white_temperature_) / span);

    *cold_white = level * (1.0f - warm_fraction);
    *warm_white = level * warm_fraction;
  }

  output::FloatOutput *red_{nullptr};
  output::FloatOutput *green_{nullptr};
  output::FloatOutput *blue_{nullptr};
  output::FloatOutput *cold_white_{nullptr};
  output::FloatOutput *warm_white_{nullptr};

  // ESPHome stores color temperatures in mireds internally.
  float cold_white_temperature_{153.846f};  // 6500 K
  float warm_white_temperature_{370.370f};  // 2700 K
  float rgb_white_temperature_{250.0f};     // 4000 K
  float white_extraction_{1.0f};

  float current_budget_ma_{12.0f};
  float red_current_ma_{12.0f};
  float green_current_ma_{12.0f};
  float blue_current_ma_{12.0f};
  float cold_white_current_ma_{12.0f};
  float warm_white_current_ma_{12.0f};
};

// Parse a normal ESPHome MQTT light command using ESPHome's own JSON schema,
// then add one extension:
//
//   "transition_ms": 250
//
// LightCall::set_transition_length() takes milliseconds, so this preserves
// exact sub-second transitions. If both `transition` and `transition_ms` are
// present, transition_ms wins because it is applied after the standard parser.
inline void handle_mqtt_light_command(light::LightState &state, JsonObjectConst input) {
  // LightJSONSchema::parse_json() currently expects a mutable JsonObject while
  // mqtt.on_json_message supplies a const object. Make a small mutable copy.
  JsonDocument doc;
  doc.set(input);

  JsonObject root = doc.as<JsonObject>();
  if (root.isNull())
    return;

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
