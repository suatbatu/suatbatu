#pragma once

#include <stdint.h>

enum class AppState : uint8_t {
  Ready,      // armed and waiting for the start button
  Countdown,  // start button pressed, random delay running, beep not yet fired
  Running,    // beep has sounded, shots are being recorded
  Review,     // string closed, results on screen
  Menu,       // settings screen
};

inline const char* appStateName(AppState s) {
  switch (s) {
    case AppState::Ready: return "ready";
    case AppState::Countdown: return "countdown";
    case AppState::Running: return "running";
    case AppState::Review: return "review";
    case AppState::Menu: return "menu";
  }
  return "?";
}
