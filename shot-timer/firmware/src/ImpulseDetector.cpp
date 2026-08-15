#include "ImpulseDetector.h"

#include <Arduino.h>
#include <esp_timer.h>

#include "Lis3dh.h"
#include "Settings.h"
#include "config.h"

ImpulseDetector impulse;

namespace {

Lis3dh sensor;
QueueHandle_t isrQueue = nullptr;
volatile bool isrArmed = false;
// Refractory in the ISR itself: the accelerometer's INT1 line can chatter
// through the tail of a single impulse, and the cheapest place to swallow that
// is before it ever reaches a queue.
volatile int64_t lastIsrUs = 0;
constexpr int64_t ISR_REFRACTORY_US = 8000;

// Threshold LSB at the driver's ±4 g range is 32 mg. Sensitivity 1 is the
// least sensitive: ~1.3 g, which only a slide slam or a dropped timer clears.
// Sensitivity 10 is ~0.13 g, which a trigger break on a coupled sensor makes.
uint8_t thresholdLsbFor(uint8_t sensitivity) {
  static const uint8_t table[10] = {40, 32, 25, 20, 16, 12, 9, 7, 5, 4};
  const uint8_t s = sensitivity < 1 ? 1 : (sensitivity > 10 ? 10 : sensitivity);
  return table[s - 1];
}

// Timestamping happens here and nowhere else: the whole point of using the
// sensor's hardware interrupt rather than polling it is that this runs within
// microseconds of the event. No I2C, no allocation, no logging.
void IRAM_ATTR onImpulse() {
  if (!isrArmed) return;
  const int64_t now = esp_timer_get_time();
  if (now - lastIsrUs < ISR_REFRACTORY_US) return;
  lastIsrUs = now;
  BaseType_t woken = pdFALSE;
  xQueueSendFromISR(isrQueue, &now, &woken);
  if (woken) portYIELD_FROM_ISR();
}

}  // namespace

bool ImpulseDetector::begin() {
  queue_ = xQueueCreate(MAX_SHOTS_PER_STRING, sizeof(int64_t));
  if (!queue_) return false;
  isrQueue = queue_;

  // The I2C bus is already up — Display::begin() owns it. Probe both possible
  // addresses so an SA0 strap either way is found without configuration.
  if (!sensor.begin(LIS3DH_ADDR_PRIMARY) && !sensor.begin(LIS3DH_ADDR_ALT)) {
    log_i("no LIS3DH found — impulse detection unavailable");
    return false;
  }

  applyThreshold();
  pinMode(PIN_IMU_INT, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_IMU_INT), onImpulse, RISING);

  present_ = true;
  log_i("LIS3DH at 0x%02X — impulse detection available", sensor.address());
  return true;
}

void ImpulseDetector::applyThreshold() {
  const uint8_t lsb = thresholdLsbFor(settings.profile().impulseThreshold);
  if (lsb == appliedThreshold_) return;
  if (sensor.configureImpulseInterrupt(lsb)) appliedThreshold_ = lsb;
}

void ImpulseDetector::arm() {
  // Guarded inside, so this is a no-op unless the active profile changed the
  // threshold since the last string — the I2C write never lands in a hot path.
  applyThreshold();
  drain();
  lastIsrUs = 0;
  armed_ = true;
  isrArmed = true;
}

void ImpulseDetector::disarm() {
  armed_ = false;
  isrArmed = false;
}

bool ImpulseDetector::popShot(int64_t& atUs) {
  if (!queue_ || !present_) return false;
  if (xQueueReceive(queue_, &atUs, 0) != pdTRUE) return false;
  detected_++;
  return true;
}

void ImpulseDetector::drain() {
  if (queue_) xQueueReset(queue_);
}

uint16_t ImpulseDetector::milliG() {
  if (!present_) return 0;
  // Rate-limited: this is an I2C transaction on the same bus as the display,
  // and the meter that consumes it refreshes four times a second.
  const uint32_t now = millis();
  if (now - lastMeterMs_ >= 200) {
    lastMeterMs_ = now;
    uint16_t mg = 0;
    if (sensor.readMilliG(mg)) lastMilliG_ = mg;
  }
  return lastMilliG_;
}
