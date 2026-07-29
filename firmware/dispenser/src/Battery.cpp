#include "Battery.h"
#include "config.h"

void Battery::begin() {
  analogReadResolution(12);
  // 11 dB attenuation → full-scale ~2.5–3.1V at the pin (before divider).
  analogSetPinAttenuation(PIN_VBAT_SENSE, ADC_11db);
}

int Battery::millivolts() {
  // Average a few samples to smooth ADC noise. analogReadMilliVolts uses the
  // per-chip eFuse calibration where available.
  uint32_t acc = 0;
  const int N = 8;
  for (int i = 0; i < N; i++) acc += analogReadMilliVolts(PIN_VBAT_SENSE);
  float atPin = (float)acc / N;
  return (int)(atPin * VBAT_DIVIDER_RATIO);
}

int Battery::percent() {
  int mv = millivolts();
  if (mv <= VBAT_EMPTY_MV) return 0;
  if (mv >= VBAT_FULL_MV)  return 100;
  return (int)(100.0f * (mv - VBAT_EMPTY_MV) / (VBAT_FULL_MV - VBAT_EMPTY_MV));
}

Battery::Level Battery::level() {
  int mv = millivolts();
  if (mv <= VBAT_CRITICAL_MV) return CRITICAL;
  if (mv <= VBAT_WARN_MV)     return WARN;
  return OK;
}

bool Battery::worsened(Level now) {
  if (now > lastAlerted_) { lastAlerted_ = now; return true; }
  if (now == OK) lastAlerted_ = OK;   // reset latch once recovered (e.g. charging)
  return false;
}
