#include "Battery.h"

#include <Arduino.h>

#include "config.h"

Battery battery;

namespace {

constexpr uint32_t SAMPLE_INTERVAL_MS = 2000;
constexpr uint8_t OVERSAMPLE = 8;
// Slow EMA. A string lasts seconds; the battery does not meaningfully move in
// that time, so there is no reason for the number on screen to.
constexpr float SMOOTHING = 0.25f;
// Below this, there is no battery on the divider — USB-only, or not built yet.
constexpr float PRESENT_THRESHOLD_V = 2.5f;

// A lithium cell's voltage is a famously non-linear proxy for charge: mapping
// 3.0-4.2 V linearly would show 50% for most of the discharge and then fall
// off a cliff. This is the usual 18650/LiPo curve, interpolated between points.
struct Point {
  float volts;
  uint8_t percent;
};
constexpr Point CURVE[] = {
    {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.93f, 70}, {3.87f, 60},
    {3.82f, 50},  {3.79f, 40}, {3.77f, 30}, {3.74f, 20}, {3.68f, 10},
    {3.45f, 5},   {3.00f, 0},
};
constexpr size_t CURVE_LEN = sizeof(CURVE) / sizeof(CURVE[0]);

uint8_t percentFromVolts(float v) {
  if (v >= CURVE[0].volts) return 100;
  if (v <= CURVE[CURVE_LEN - 1].volts) return 0;
  for (size_t i = 1; i < CURVE_LEN; i++) {
    if (v >= CURVE[i].volts) {
      const Point& hi = CURVE[i - 1];
      const Point& lo = CURVE[i];
      const float span = hi.volts - lo.volts;
      const float frac = span > 0.0f ? (v - lo.volts) / span : 0.0f;
      return static_cast<uint8_t>(lo.percent + frac * (hi.percent - lo.percent));
    }
  }
  return 0;
}

}  // namespace

void Battery::begin() {
  // 12 dB gives the widest input range on the S3; the divider already halves
  // the cell voltage, so a full 4.2 V cell presents ~2.1 V at the pin.
  analogSetPinAttenuation(PIN_VBAT_ADC, ADC_11db);
  sample();
  primed_ = true;
}

void Battery::sample() {
  uint32_t sumMv = 0;
  for (uint8_t i = 0; i < OVERSAMPLE; i++) {
    // analogReadMilliVolts applies the chip's factory ADC calibration, which
    // is worth several percent of accuracy over a raw count.
    sumMv += analogReadMilliVolts(PIN_VBAT_ADC);
  }
  const float pinVolts = (sumMv / static_cast<float>(OVERSAMPLE)) / 1000.0f;
  const float cellVolts = pinVolts * VBAT_DIVIDER_RATIO;

  volts_ = primed_ ? volts_ + (cellVolts - volts_) * SMOOTHING : cellVolts;
  present_ = volts_ > PRESENT_THRESHOLD_V;
  percent_ = present_ ? percentFromVolts(volts_) : 0;
}

void Battery::tick() {
  const uint32_t now = millis();
  if (primed_ && (now - lastSampleMs_) < SAMPLE_INTERVAL_MS) return;
  lastSampleMs_ = now;
  sample();
}
