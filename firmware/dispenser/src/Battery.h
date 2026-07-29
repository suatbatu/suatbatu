// Battery.h — optional battery monitoring for the 18650 UPS rail.
//
// Reads a divided battery voltage on PIN_VBAT_SENSE (ADC1) and reports a level
// with hysteresis so alerts fire once per downward crossing, not repeatedly.
#pragma once
#include <Arduino.h>

class Battery {
public:
  enum Level { OK = 0, WARN = 1, CRITICAL = 2 };

  void begin();

  int millivolts();          // battery voltage in mV (already × divider ratio)
  int percent();             // rough 0..100 from the Li-ion curve endpoints
  Level level();             // current level given the latest reading

  // Returns true (and updates the internal latch) only when the level has
  // *worsened* since the last alert — use this to decide whether to notify.
  bool worsened(Level now);

private:
  Level lastAlerted_ = OK;
};
