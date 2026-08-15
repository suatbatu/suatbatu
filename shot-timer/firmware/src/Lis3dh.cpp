#include "Lis3dh.h"

#include <Wire.h>
#include <math.h>

namespace {
// Register map — ST LIS3DH datasheet, section 8.
constexpr uint8_t REG_WHO_AM_I = 0x0F;
constexpr uint8_t REG_CTRL1 = 0x20;
constexpr uint8_t REG_CTRL2 = 0x21;
constexpr uint8_t REG_CTRL3 = 0x22;
constexpr uint8_t REG_CTRL4 = 0x23;
constexpr uint8_t REG_CTRL5 = 0x24;
constexpr uint8_t REG_OUT_X_L = 0x28;
constexpr uint8_t REG_INT1_CFG = 0x30;
constexpr uint8_t REG_INT1_SRC = 0x31;
constexpr uint8_t REG_INT1_THS = 0x32;
constexpr uint8_t REG_INT1_DUR = 0x33;

constexpr uint8_t WHO_AM_I_VALUE = 0x33;

// CTRL1: ODR = 1.344 kHz in normal (10-bit) mode, all three axes enabled.
// That rate sets the interrupt's timing granularity at ~0.74 ms — an order of
// magnitude coarser than the acoustic path, and documented as such.
constexpr uint8_t CTRL1_1344HZ_XYZ = 0x97;
// CTRL2: route the high-pass filter into the INT1 generator. Without it the
// interrupt would trip on gravity, which is permanently 1 g.
constexpr uint8_t CTRL2_HP_ON_INT1 = 0x01;
// CTRL3: interrupt activity 1 drives the INT1 pin.
constexpr uint8_t CTRL3_I1_IA1 = 0x40;
// CTRL4: block data update, ±4 g, high resolution. ±4 g rather than ±16 g
// because a trigger break is a fraction of a g and the finer 32 mg LSB is
// worth more here than headroom we would only use on a slide slam.
constexpr uint8_t CTRL4_BDU_4G_HR = 0x98;
// CTRL5: no interrupt latching — see the header for why.
constexpr uint8_t CTRL5_NO_LATCH = 0x00;
// INT1_CFG: OR of "high" events on X, Y and Z.
constexpr uint8_t INT1_CFG_ANY_AXIS_HIGH = 0x2A;

constexpr float MILLIG_PER_COUNT_4G = 4000.0f / 32768.0f * 16.0f;  // 10-bit, left-justified
}  // namespace

bool Lis3dh::write(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr_);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool Lis3dh::read(uint8_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(addr_);
  // Bit 7 set means auto-increment across a multi-byte read.
  Wire.write(len > 1 ? (reg | 0x80) : reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr_, static_cast<uint8_t>(len)) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

bool Lis3dh::begin(uint8_t addr) {
  addr_ = addr;
  present_ = false;

  uint8_t who = 0;
  if (!read(REG_WHO_AM_I, &who, 1)) return false;
  if (who != WHO_AM_I_VALUE) return false;

  present_ = true;
  return true;
}

bool Lis3dh::configureImpulseInterrupt(uint8_t thresholdLsb) {
  if (!present_) return false;

  bool ok = true;
  ok &= write(REG_CTRL1, CTRL1_1344HZ_XYZ);
  ok &= write(REG_CTRL4, CTRL4_BDU_4G_HR);
  ok &= write(REG_CTRL2, CTRL2_HP_ON_INT1);
  ok &= write(REG_CTRL5, CTRL5_NO_LATCH);

  // Threshold is 7-bit; 0 would mean "interrupt on everything".
  ok &= write(REG_INT1_THS, thresholdLsb < 1 ? 1 : (thresholdLsb & 0x7F));
  // Duration 0: fire on the first sample over threshold. A trigger break is a
  // single-sample event; requiring it to persist would miss it entirely.
  ok &= write(REG_INT1_DUR, 0x00);
  ok &= write(REG_INT1_CFG, INT1_CFG_ANY_AXIS_HIGH);
  ok &= write(REG_CTRL3, CTRL3_I1_IA1);

  // Reading the source register clears any event latched during setup, so the
  // first thing the ISR sees is a real one.
  uint8_t src = 0;
  read(REG_INT1_SRC, &src, 1);
  return ok;
}

bool Lis3dh::readMilliG(uint16_t& out) {
  if (!present_) return false;
  uint8_t raw[6];
  if (!read(REG_OUT_X_L, raw, sizeof(raw))) return false;

  const int16_t x = static_cast<int16_t>(raw[0] | (raw[1] << 8));
  const int16_t y = static_cast<int16_t>(raw[2] | (raw[3] << 8));
  const int16_t z = static_cast<int16_t>(raw[4] | (raw[5] << 8));

  const float mg = sqrtf(static_cast<float>(x) * x + static_cast<float>(y) * y +
                         static_cast<float>(z) * z) *
                   MILLIG_PER_COUNT_4G;
  out = static_cast<uint16_t>(mg > 65535.0f ? 65535.0f : mg);
  return true;
}
