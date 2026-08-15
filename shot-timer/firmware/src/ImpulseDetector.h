// Shot detection by mechanical impulse, using the LIS3DH's own threshold
// interrupt.
//
// This exists because dry fire is the case the acoustic detector handles worst:
// a trigger break is barely above room noise, which is exactly where every
// false trigger lives too. A trigger break is a *mechanical* event, and an
// accelerometer sidesteps the problem instead of straining against it.
//
// ⚠️ It only works if the sensor is mechanically coupled to the firearm. A
// timer clipped to a belt will not feel a trigger break at arm's length. See
// docs/DETECTION.md — this is a real constraint, not a tuning problem.
//
// Timing: the accelerometer samples at 1.344 kHz, so an impulse timestamp is
// good to roughly a millisecond — an order of magnitude coarser than the
// acoustic path's sample-counter timestamps. Fine against the hundredths a
// timer displays; worth knowing before trusting an impulse split.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdint.h>

class ImpulseDetector {
 public:
  // Probes for the sensor on the shared I2C bus. Returns false when no LIS3DH
  // answers, which is the normal case on a build without one — impulse mode
  // then reports itself unavailable rather than silently detecting nothing.
  bool begin();

  void arm();
  void disarm();
  bool armed() const { return armed_; }

  // Re-applies the threshold after a profile change.
  void applyThreshold();

  bool popShot(int64_t& atUs);
  void drain();

  bool present() const { return present_; }
  uint32_t detected() const { return detected_; }
  // Live magnitude in milli-g for the tuning meter. Costs an I2C transaction.
  uint16_t milliG();

 private:
  QueueHandle_t queue_ = nullptr;
  volatile bool armed_ = false;
  bool present_ = false;
  uint32_t detected_ = 0;
  uint8_t appliedThreshold_ = 0;
  uint32_t lastMeterMs_ = 0;
  uint16_t lastMilliG_ = 0;
};

extern ImpulseDetector impulse;
