#include "components/gx53_mixer/gx53_mixer_math.h"

#include <cassert>
#include <cmath>

using esphome::gx53_mixer::math::ChannelCurrents;
using esphome::gx53_mixer::math::ChannelLevels;

static bool close(float left, float right, float epsilon = 0.00001f) { return std::abs(left - right) <= epsilon; }

int main() {
  using namespace esphome::gx53_mixer::math;

  assert(remap_brightness(0.0f, 0.13f) == 0.0f);
  assert(remap_brightness(0.000001f, 0.13f) > 0.13f);
  assert(close(remap_brightness(0.01f, 0.13f), 0.1387f));
  assert(close(remap_brightness(0.42f, 0.0f), 0.42f));
  assert(close(remap_brightness(1.0f, 0.13f), 1.0f));

  // A common master gain preserves all active-channel ratios.
  ChannelLevels color{0.8f, 0.4f, 0.2f, 0.1f, 0.05f};
  const float gain = remap_brightness(0.01f, 0.13f) / 0.01f;
  ChannelLevels floored = color;
  scale(floored, gain);
  assert(close(floored.red / floored.green, color.red / color.green));
  assert(close(floored.cold_white / floored.warm_white, color.cold_white / color.warm_white));

  constexpr float cold = 1000000.0f / 6500.0f;
  constexpr float warm = 1000000.0f / 2700.0f;
  const auto cold_end = white_channels(cold, 1.0f, cold, warm);
  const auto warm_end = white_channels(warm, 1.0f, cold, warm);
  const auto cct_mid = white_channels((cold + warm) / 2.0f, 1.0f, cold, warm);
  assert(close(cold_end.cold_white + cold_end.warm_white, 1.0f));
  assert(close(warm_end.cold_white + warm_end.warm_white, 1.0f));
  assert(close(cct_mid.cold_white + cct_mid.warm_white, 1.0f));

  // A five-channel RGB->CCT crossfade never passes through black.
  const ChannelLevels red{1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  const auto white = white_channels(250.0f, 1.0f, cold, warm);
  const auto red_to_white_mid = lerp(red, white, 0.5f);
  assert(red_to_white_mid.red > 0.0f);
  assert(red_to_white_mid.cold_white > 0.0f);
  assert(red_to_white_mid.warm_white > 0.0f);

  // The limiter runs on the completed five-channel frame and preserves ratios.
  ChannelLevels all{1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  const ChannelCurrents currents{4.0f, 4.0f, 4.0f, 6.0f, 6.0f};
  apply_current_limit(all, currents, 12.0f);
  assert(close(requested_current_ma(all, currents), 12.0f));
  assert(close(all.red / all.cold_white, 1.0f));

  return 0;
}
