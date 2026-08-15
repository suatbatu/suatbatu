// Piezo sounder on an LEDC channel, plus the par-time beep schedule.
//
// Nothing here blocks: start() records when the tone should stop, and tick()
// (called from the main loop) turns it off. The main loop also asks this class
// when the *next* par beep is due, so par timing is driven off the same
// esp_timer clock as the shot timestamps rather than off millis().
#pragma once

#include <stdint.h>

class Buzzer {
 public:
  void begin();

  // Starts a tone now. Returns the esp_timer timestamp of the tone's first
  // edge — this is t=0 for the string.
  int64_t beep(uint16_t freqHz, uint16_t durationMs, uint8_t volume);
  void stop();
  void tick(int64_t nowUs);
  bool sounding() const { return sounding_; }

  // Short confirmation blip for menu actions; never used as a start signal.
  void blip();

 private:
  bool sounding_ = false;
  int64_t stopAtUs_ = 0;
};

extern Buzzer buzzer;
