#include "components/gx53_mixer/thermal_interlock.h"

#include <cassert>
#include <cmath>
#include <cstdint>

using esphome::gx53_mixer::ThermalInterlock;
using esphome::gx53_mixer::ThermalInterlockUpdate;

int main() {
  ThermalInterlock interlock;

  // Boot is fail-safe until a sufficiently cool reading arrives.
  assert(interlock.is_locked());
  assert(interlock.update(75.0f, 1000) == ThermalInterlockUpdate::NONE);
  assert(interlock.is_locked());
  assert(interlock.update(70.0f, 2000) == ThermalInterlockUpdate::UNLOCKED);
  assert(!interlock.is_locked());

  // Crossing the shutdown threshold locks immediately. Cooling alone is not
  // sufficient until the full minute has elapsed.
  assert(interlock.update(79.9f, 3000) == ThermalInterlockUpdate::NONE);
  assert(interlock.update(80.0f, 4000) == ThermalInterlockUpdate::LOCKED);
  assert(interlock.is_locked());
  assert(interlock.update(69.0f, 63999) == ThermalInterlockUpdate::NONE);
  assert(interlock.is_locked());
  assert(interlock.update(70.0f, 64000) == ThermalInterlockUpdate::UNLOCKED);
  assert(!interlock.is_locked());

  // A failed temperature reading is also fail-safe and requires both a valid
  // cool reading and the cooldown before rearming.
  assert(interlock.update(NAN, 70000) == ThermalInterlockUpdate::LOCKED);
  assert(interlock.update(65.0f, 129999) == ThermalInterlockUpdate::NONE);
  assert(interlock.update(65.0f, 130000) == ThermalInterlockUpdate::UNLOCKED);

  // A hot boot remains locked and starts the same minimum cooldown without
  // briefly enabling the output.
  ThermalInterlock hot_boot;
  assert(hot_boot.update(85.0f, 200000) == ThermalInterlockUpdate::NONE);
  assert(hot_boot.is_locked());
  assert(hot_boot.update(65.0f, 259999) == ThermalInterlockUpdate::NONE);
  assert(hot_boot.update(65.0f, 260000) == ThermalInterlockUpdate::UNLOCKED);

  // Unsigned elapsed-time arithmetic keeps the cooldown correct across the
  // millis() wraparound.
  ThermalInterlock wrapping;
  assert(wrapping.update(60.0f, UINT32_MAX - 1000) == ThermalInterlockUpdate::UNLOCKED);
  assert(wrapping.update(85.0f, UINT32_MAX - 500) == ThermalInterlockUpdate::LOCKED);
  assert(wrapping.update(60.0f, 58498) == ThermalInterlockUpdate::NONE);
  assert(wrapping.update(60.0f, 59499) == ThermalInterlockUpdate::UNLOCKED);

  return 0;
}
