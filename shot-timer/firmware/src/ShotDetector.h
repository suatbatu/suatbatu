// Acoustic shot detection off a two-microphone I2S array.
//
// A dedicated high-priority task owns the I2S channel, runs an envelope
// follower over every sample of the primary channel, and for each candidate
// onset decides three things before it reports a shot:
//
//   1. is it above the adaptive threshold?          (is it loud enough)
//   2. is it above the decaying echo guard?         (is it a shot, or shot 1's
//                                                    reflection coming back)
//   3. did it arrive along the shooter's axis?      (is it *your* shot, or the
//                                                    next bay over)
//
// Nothing in the audio path allocates, blocks on the network, or touches the
// display. See docs/DETECTION.md for the algorithm and the reasoning.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdint.h>

// Why a candidate onset was thrown away — surfaced in the UI so that a timer
// that seems to be "missing shots" can be told apart from one that is
// correctly rejecting the bay next door.
enum class RejectReason : uint8_t { None = 0, Echo, OffAxis, Muted, QueueFull };

struct DetectorStats {
  uint32_t accepted = 0;
  uint32_t rejectedEcho = 0;
  uint32_t rejectedOffAxis = 0;
  int16_t lastLagSamples = 0;
  int16_t lastAngleDeg = 0;
  uint8_t lastConfidence = 0;  // 0..100, cross-correlation quality
  bool secondMicPresent = false;
};

class ShotDetector {
 public:
  bool begin();

  // Arm/disarm detection. While disarmed the task keeps reading I2S so the
  // noise floor stays current, it just does not report shots.
  void arm();
  void disarm();
  bool armed() const { return armed_; }

  // Drop anything detected before `untilUs`. The start beep is a loud noise a
  // few centimetres from the microphone; without this every string would open
  // with a phantom shot at t=0.
  void muteUntil(int64_t untilUs);

  // Pops the next accepted shot timestamp (esp_timer microseconds).
  // Returns false when the queue is empty. Call from the main loop.
  bool popShot(int64_t& atUs);
  void drain();

  // Live meter for the sensitivity screen: 0..1000, log-ish, both from the
  // instantaneous envelope and the running noise floor.
  uint16_t levelPerMille() const;
  uint16_t floorPerMille() const;

  DetectorStats stats() const;
  void resetStats();

 private:
  static void taskTrampoline(void* arg);
  void run();

  QueueHandle_t queue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  volatile bool armed_ = false;

  // Written by the audio task, read by everyone else. Single 32-bit words, so
  // torn reads are not possible on this target; they are only ever displayed.
  volatile float envelope_ = 0.0f;
  volatile float noiseFloor_ = 0.0f;
  volatile uint32_t accepted_ = 0;
  volatile uint32_t rejectedEcho_ = 0;
  volatile uint32_t rejectedOffAxis_ = 0;
  volatile int32_t lastLag_ = 0;
  volatile int32_t lastAngle_ = 0;
  volatile uint32_t lastConfidence_ = 0;
  volatile bool secondMic_ = false;
};

extern ShotDetector detector;
