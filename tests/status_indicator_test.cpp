#include "components/gx53_mixer/status_indicator.h"

#include <cassert>

using esphome::gx53_mixer::classify_status;
using esphome::gx53_mixer::StatusIndication;
using esphome::gx53_mixer::StatusIndicatorFilter;

int main() {
  assert(classify_status(false, false, false) == StatusIndication::NO_WIFI);
  assert(classify_status(false, true, false) == StatusIndication::FALLBACK_AP);
  assert(classify_status(true, false, false) == StatusIndication::NO_MQTT);
  // The fallback AP can briefly remain up after the station connects.
  assert(classify_status(true, true, true) == StatusIndication::NONE);

  StatusIndicatorFilter filter;
  filter.set_delay_ms(30000);

  // Before the first full connection every problem is shown immediately.
  assert(filter.update(StatusIndication::NO_WIFI, 0));
  assert(filter.shown() == StatusIndication::NO_WIFI);
  assert(!filter.update(StatusIndication::NO_WIFI, 1000));
  assert(filter.update(StatusIndication::NO_MQTT, 2000));
  assert(filter.update(StatusIndication::NONE, 3000));
  assert(filter.shown() == StatusIndication::NONE);

  // Afterwards a problem must persist for the full delay.
  assert(!filter.update(StatusIndication::NO_WIFI, 10000));
  assert(!filter.update(StatusIndication::NO_WIFI, 39999));
  assert(filter.shown() == StatusIndication::NONE);
  assert(filter.update(StatusIndication::NO_WIFI, 40000));

  // The delay covers the whole outage, not each problem type separately, and
  // a shown problem switches type immediately.
  assert(filter.update(StatusIndication::FALLBACK_AP, 41000));
  assert(filter.shown() == StatusIndication::FALLBACK_AP);

  // Recovery is immediate; a short blip afterwards is hidden.
  assert(filter.update(StatusIndication::NONE, 50000));
  assert(!filter.update(StatusIndication::NO_MQTT, 60000));
  assert(!filter.update(StatusIndication::NONE, 65000));
  assert(!filter.update(StatusIndication::NO_MQTT, 70000));
  assert(!filter.update(StatusIndication::NO_MQTT, 99999));
  assert(filter.update(StatusIndication::NO_MQTT, 100000));

  return 0;
}
