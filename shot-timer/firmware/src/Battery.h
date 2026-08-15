// Battery monitoring off the divider on PIN_VBAT_ADC.
//
// Sampled slowly and smoothed hard: nothing here is time-critical, and an
// unsmoothed ADC reading on a rail that a piezo is periodically slamming would
// make the percentage jump around for no reason.
#pragma once

#include <stdint.h>

class Battery {
 public:
  void begin();
  void tick();  // call from the main loop; samples at its own pace

  float volts() const { return volts_; }
  uint8_t percent() const { return percent_; }

  // False when the reading is too low to be a battery at all — a board running
  // from USB with nothing on the divider reads near zero, and reporting "0%"
  // there would be a lie rather than a warning.
  bool present() const { return present_; }
  bool low() const { return present_ && percent_ <= LOW_PERCENT; }
  bool critical() const { return present_ && percent_ <= CRITICAL_PERCENT; }

  static constexpr uint8_t LOW_PERCENT = 20;
  static constexpr uint8_t CRITICAL_PERCENT = 5;

 private:
  void sample();

  float volts_ = 0.0f;
  uint8_t percent_ = 0;
  bool present_ = false;
  bool primed_ = false;
  uint32_t lastSampleMs_ = 0;
};

extern Battery battery;
