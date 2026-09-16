#pragma once

#include <algorithm>
#include <cmath>

namespace esphome::gx53_mixer::math {

struct ChannelLevels {
  float red{0.0f};
  float green{0.0f};
  float blue{0.0f};
  float cold_white{0.0f};
  float warm_white{0.0f};
};

struct ChannelCurrents {
  float red;
  float green;
  float blue;
  float cold_white;
  float warm_white;
};

inline float clamp01(float value) { return std::max(0.0f, std::min(1.0f, value)); }

// Remap one common logical/master brightness before gamma correction. This
// keeps the floor in the same user-facing brightness domain in which the
// hardware's low-end cutoff was observed.
inline float remap_brightness(float brightness, float minimum_brightness) {
  brightness = clamp01(brightness);
  minimum_brightness = clamp01(minimum_brightness);
  if (brightness <= 0.0f) return 0.0f;
  return minimum_brightness + brightness * (1.0f - minimum_brightness);
}

inline ChannelLevels white_channels(float mireds, float level, float cold_mireds, float warm_mireds) {
  ChannelLevels result;
  const float span = warm_mireds - cold_mireds;
  if (span <= 0.0f || level <= 0.0f) return result;

  const float warm_fraction = clamp01((mireds - cold_mireds) / span);
  result.cold_white = level * (1.0f - warm_fraction);
  result.warm_white = level * warm_fraction;
  return result;
}

inline ChannelLevels lerp(const ChannelLevels& start, const ChannelLevels& end, float completion) {
  completion = clamp01(completion);
  return {
      start.red + completion * (end.red - start.red),
      start.green + completion * (end.green - start.green),
      start.blue + completion * (end.blue - start.blue),
      start.cold_white + completion * (end.cold_white - start.cold_white),
      start.warm_white + completion * (end.warm_white - start.warm_white),
  };
}

inline void scale(ChannelLevels& levels, float factor) {
  levels.red *= factor;
  levels.green *= factor;
  levels.blue *= factor;
  levels.cold_white *= factor;
  levels.warm_white *= factor;
}

inline float requested_current_ma(const ChannelLevels& levels, const ChannelCurrents& currents) {
  return levels.red * currents.red + levels.green * currents.green + levels.blue * currents.blue +
         levels.cold_white * currents.cold_white + levels.warm_white * currents.warm_white;
}

inline void apply_current_limit(ChannelLevels& levels, const ChannelCurrents& currents, float budget_ma) {
  const float requested_ma = requested_current_ma(levels, currents);
  if (requested_ma <= budget_ma || requested_ma <= 0.0f) return;

  scale(levels, budget_ma / requested_ma);
}

inline bool nearly_equal(const ChannelLevels& left, const ChannelLevels& right, float epsilon = 0.0001f) {
  return std::abs(left.red - right.red) <= epsilon && std::abs(left.green - right.green) <= epsilon &&
         std::abs(left.blue - right.blue) <= epsilon && std::abs(left.cold_white - right.cold_white) <= epsilon &&
         std::abs(left.warm_white - right.warm_white) <= epsilon;
}

}  // namespace esphome::gx53_mixer::math
