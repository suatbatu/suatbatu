// Carousel.h — stepper control, homing, and single-slot advance.
//
// Supports 28BYJ-48 (ULN2003) or NEMA-17 (A4988) via MOTOR_DRIVER in config.h.
// Absolute position is recovered on boot by seeking a hall-sensor magnet at
// slot 0; without homing, position is unknown after a reset.
#pragma once
#include <Arduino.h>
#include "DoseSensors.h"

class Carousel {
public:
  void begin(DoseSensors* sensors);

  // Seek slot 0 via the hall sensor. Returns false if the mark is never found
  // (jam / sensor fault) — caller should treat that as a fault and alert.
  bool home();

  // Rotate forward by one compartment (360°/SLOTS). Returns true on completion.
  bool advanceOneSlot();

  int  currentSlot() const { return slot_; }
  void setSlot(int s)      { slot_ = ((s % CAROUSEL_SLOTS) + CAROUSEL_SLOTS) % CAROUSEL_SLOTS; }

  void release();          // de-energise coils (saves power, prevents heat)

private:
  void stepN(long steps, bool forward);
  void writeCoils(uint8_t mask);

  DoseSensors* sensors_ = nullptr;
  int  slot_ = 0;
};
