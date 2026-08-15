// Acoustic shot detection off an I2S MEMS microphone.
//
// A dedicated high-priority task owns the I2S channel, runs an envelope
// follower over every sample, and pushes a microsecond timestamp into a queue
// whenever a transient clears the threshold. Nothing in the audio path
// allocates, blocks on the network, or touches the display.
//
// See docs/DETECTION.md for the algorithm and how the timestamp is derived.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdint.h>

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

  // Pops the next detected shot timestamp (esp_timer microseconds).
  // Returns false when the queue is empty. Call from the main loop.
  bool popShot(int64_t& atUs);
  void drain();

  // Live meter for the sensitivity screen: 0..1000, log-ish, both from the
  // instantaneous envelope and the running noise floor.
  uint16_t levelPerMille() const;
  uint16_t floorPerMille() const;

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
};

extern ShotDetector detector;
