// DoseSensors.h — IR drop-detect, "taken" confirm button, buzzer + LED.
#pragma once
#include <Arduino.h>

class DoseSensors {
public:
  void begin();
  void update();                 // call frequently from loop()

  // Drop detection: arm() before a rotation, then poll dropped() within a window.
  void arm();
  bool dropped() const { return dropLatched_; }

  bool takenPressed();           // debounced; true once per press
  bool hallHome();               // true when homing magnet is present

  void buzz(bool on);
  void led(bool on);
  void beep(uint16_t ms);        // blocking short beep

private:
  bool     dropLatched_   = false;
  int      lastBeam_      = -1;
  uint32_t lastBtnMs_     = 0;
  bool     lastBtnState_  = false;
};
