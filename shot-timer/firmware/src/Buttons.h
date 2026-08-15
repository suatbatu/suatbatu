// Three buttons to GND with internal pull-ups: debounce, plus short/long press.
#pragma once

#include <stdint.h>

enum class Btn : uint8_t { Start = 0, Up = 1, Down = 2, Count = 3 };
enum class Press : uint8_t { None, Short, Long };

class Buttons {
 public:
  void begin();
  void poll();

  // Each event is reported exactly once; reading it clears it.
  Press take(Btn b);

 private:
  struct State {
    uint8_t pin;
    bool stable = true;  // pull-up: true == released
    bool lastRaw = true;
    uint32_t changedAtMs = 0;
    uint32_t pressedAtMs = 0;
    bool longFired = false;
    Press pending = Press::None;
  };
  State s_[static_cast<uint8_t>(Btn::Count)];
};

extern Buttons buttons;
