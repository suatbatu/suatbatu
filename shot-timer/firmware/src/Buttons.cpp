#include "Buttons.h"

#include <Arduino.h>

#include "config.h"

Buttons buttons;

namespace {
constexpr uint32_t DEBOUNCE_MS = 25;
constexpr uint32_t LONG_PRESS_MS = 700;
}  // namespace

void Buttons::begin() {
  s_[static_cast<uint8_t>(Btn::Start)].pin = PIN_BTN_START;
  s_[static_cast<uint8_t>(Btn::Up)].pin = PIN_BTN_UP;
  s_[static_cast<uint8_t>(Btn::Down)].pin = PIN_BTN_DOWN;
  for (auto& s : s_) pinMode(s.pin, INPUT_PULLUP);
}

void Buttons::poll() {
  const uint32_t now = millis();
  for (auto& s : s_) {
    const bool raw = digitalRead(s.pin) != LOW;  // true == released
    if (raw != s.lastRaw) {
      s.lastRaw = raw;
      s.changedAtMs = now;
      continue;
    }
    if (raw == s.stable || (now - s.changedAtMs) < DEBOUNCE_MS) {
      // Still bouncing, or nothing changed — but a held button can still cross
      // into long-press territory.
      if (!s.stable && !s.longFired && (now - s.pressedAtMs) >= LONG_PRESS_MS) {
        s.longFired = true;
        s.pending = Press::Long;
      }
      continue;
    }

    s.stable = raw;
    if (!s.stable) {  // pressed
      s.pressedAtMs = now;
      s.longFired = false;
    } else {  // released
      // A long press already fired on the way down; don't also report a short.
      if (!s.longFired) s.pending = Press::Short;
    }
  }
}

Press Buttons::take(Btn b) {
  State& s = s_[static_cast<uint8_t>(b)];
  const Press p = s.pending;
  s.pending = Press::None;
  return p;
}
