// Time-difference-of-arrival between two microphones, by cross-correlation.
//
// Deliberately free of Arduino and ESP-IDF headers: this is the one piece of
// the detector whose sign conventions and ring indexing are easy to get subtly
// wrong and impossible to eyeball on a device, so it is built to be testable on
// a host. See tools/test_tdoa.cpp.
#pragma once

#include <math.h>
#include <stdint.h>
#include <stddef.h>

struct LagResult {
  int lag = 0;             // samples; positive means channel B heard it later
  uint32_t confidence = 0; // 0..100, normalised correlation peak
  float energyB = 0.0f;    // window energy in channel B, to spot a missing mic
  bool valid = false;      // false when the window is not fully in the ring
};

// `ringA`/`ringB` are circular buffers of `ringSize` (a power of two) samples.
// `start` is the absolute index of the first sample of the correlation window;
// `available` is the absolute index one past the newest sample written.
//
// Positive lag means B matched A when shifted later in time, i.e. the sound
// reached A first and the source is on A's side of the array.
inline LagResult estimateLag(const int16_t* ringA, const int16_t* ringB, size_t ringSize,
                             uint64_t start, uint64_t available, int window, int maxLag) {
  LagResult out;
  const size_t mask = ringSize - 1;

  // Everything the correlation touches must still be inside the ring, and the
  // lagged reads run maxLag past the end of the window in both directions.
  if (start < static_cast<uint64_t>(maxLag)) return out;
  if (start + window + maxLag > available) return out;
  if (available - (start - maxLag) > ringSize) return out;

  double energyA = 0.0;
  double energyB = 0.0;
  for (int k = 0; k < window; k++) {
    const int16_t a = ringA[(start + k) & mask];
    const int16_t b = ringB[(start + k) & mask];
    energyA += static_cast<double>(a) * a;
    energyB += static_cast<double>(b) * b;
  }
  out.energyB = static_cast<float>(energyB);
  if (energyA <= 0.0 || energyB <= 0.0) {
    out.valid = true;  // a silent channel is a real answer: "no second mic"
    return out;
  }

  int64_t bestSum = INT64_MIN;
  int bestLag = 0;
  for (int lag = -maxLag; lag <= maxLag; lag++) {
    int64_t sum = 0;
    for (int k = 0; k < window; k++) {
      const int16_t a = ringA[(start + k) & mask];
      const int16_t b = ringB[(start + k + lag) & mask];
      sum += static_cast<int32_t>(a) * b;
    }
    if (sum > bestSum) {
      bestSum = sum;
      bestLag = lag;
    }
  }

  // Normalising by sqrt(Ea*Eb) makes the peak 1.0 for identical windows and
  // near zero for unrelated noise, independent of how loud the shot was.
  const double norm = sqrt(energyA * energyB);
  double quality = norm > 0.0 ? static_cast<double>(bestSum) / norm : 0.0;
  if (quality < 0.0) quality = 0.0;
  if (quality > 1.0) quality = 1.0;

  out.lag = bestLag;
  out.confidence = static_cast<uint32_t>(quality * 100.0 + 0.5);
  out.valid = true;
  return out;
}

// Converts a lag in samples into an off-axis angle in degrees, given the
// microphone spacing. Returns 90 (fully off-axis) when the geometry says the
// lag is impossible, which happens with noise rather than a real source.
inline int lagToAngleDeg(int lag, float micSpacingMetres, float sampleRate, float speedOfSound) {
  if (micSpacingMetres <= 0.0f) return 0;
  float s = lag * speedOfSound / (sampleRate * micSpacingMetres);
  if (s > 1.0f) s = 1.0f;
  if (s < -1.0f) s = -1.0f;
  return static_cast<int>(asinf(s) * 180.0f / 3.14159265358979f);
}

// The largest lag, in samples, that still counts as "in front" for a given
// acceptance half-angle.
inline int maxLagForAngle(int degrees, float micSpacingMetres, float sampleRate,
                          float speedOfSound, int hardLimit) {
  if (degrees < 1) degrees = 1;
  if (degrees > 89) degrees = 89;
  const float lag = micSpacingMetres * sinf(degrees * 3.14159265358979f / 180.0f) /
                    speedOfSound * sampleRate;
  int n = static_cast<int>(ceilf(lag));
  if (n < 1) n = 1;
  if (n > hardLimit) n = hardLimit;
  return n;
}
