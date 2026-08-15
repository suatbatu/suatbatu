// Minimal LIS3DH driver — just enough to make the accelerometer raise a
// hardware interrupt when it feels a sharp impulse.
//
// Written directly against the registers rather than pulling in Adafruit's
// driver plus BusIO plus Unified Sensor: three dependencies for a part we use
// one feature of, in a build environment where the library registry is blocked
// anyway. The datasheet register map is small and stable.
#pragma once

#include <stddef.h>
#include <stdint.h>

class Lis3dh {
 public:
  // `addr` is 0x18 (SA0 low) or 0x19 (SA0 high). Returns false if WHO_AM_I
  // does not answer 0x33, which is the only honest test that a part is there.
  bool begin(uint8_t addr);

  // Configures INT1 to pulse when high-pass-filtered acceleration on any axis
  // exceeds `thresholdLsb`. At the ±4 g range this driver selects, one LSB is
  // 32 mg, so 10 LSB ≈ 0.32 g.
  //
  // The interrupt is deliberately *not* latched: a latched interrupt would have
  // to be cleared over I2C before the next event could fire, putting a blocking
  // bus transaction in the detection path. Unlatched, the pin pulses and the
  // software refractory does the de-bouncing.
  bool configureImpulseInterrupt(uint8_t thresholdLsb);

  bool present() const { return present_; }
  uint8_t address() const { return addr_; }

  // Instantaneous magnitude in milli-g, for the tuning meter. Costs an I2C
  // transaction, so call it from the UI path, never from a hot loop.
  bool readMilliG(uint16_t& out);

 private:
  bool write(uint8_t reg, uint8_t value);
  bool read(uint8_t reg, uint8_t* buf, size_t len);

  uint8_t addr_ = 0;
  bool present_ = false;
};
