// The state machine that is actually "the shot timer".
//
// Owns the start delay, the beep, the open string, the par schedule and the
// auto-stop. Both the buttons and the web UI drive it through the same three
// requests, so a string started from a phone behaves identically to one started
// from the device.
#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

#include <functional>

#include "AppState.h"
#include "StringRun.h"

class TimerApp {
 public:
  // Emitted for every state transition, every shot and every beep. The web
  // interface turns these into WebSocket frames; nothing else subscribes.
  using EventFn = std::function<void(const JsonDocument&)>;

  void begin();
  void loop();

  // Call these only from the task that calls loop() — they run the transition
  // immediately, and that includes sounding the buzzer and writing flash.
  void requestStart();  // start button: new string, or restart from Review
  void requestStop();   // stop the open string now
  void openMenu();
  void closeMenu();

  // Safe from any task. The web handlers run on the AsyncTCP task, so they
  // queue the request here and loop() performs it; letting two tasks drive the
  // state machine directly would race on the open string.
  void postStart();
  void postStop();

  AppState state() const { return state_; }
  const StringRun& run() const { return run_; }
  // Live time since the beep while Running; the final total once closed.
  uint32_t elapsedMs() const;
  bool lastSaveFailed() const { return saveFailed_; }

  void onEvent(EventFn fn) { sink_ = std::move(fn); }
  void statusJson(JsonObject out) const;

 private:
  void enter(AppState s);
  void fireStartBeep();
  void closeString(const char* reason);
  void servicePar(int64_t nowUs);
  // Resolves which par schedule is in force. A drill with par times overrides
  // the global Par settings; the shipped "Free" drill has none, which is what
  // makes the timer behave exactly as it did before drills existed.
  uint8_t parSchedule(const uint16_t*& times) const;
  void emit(const char* type, const std::function<void(JsonObject)>& fill = nullptr);

  AppState state_ = AppState::Ready;
  AppState stateBeforeMenu_ = AppState::Ready;
  StringRun run_;

  int64_t beepDueAtUs_ = 0;   // Countdown: when the start beep should fire
  int64_t lastShotAtUs_ = 0;  // Running: for the auto-stop timeout
  uint8_t parFired_ = 0;      // how many par beeps have already sounded
  bool saveFailed_ = false;

  volatile bool pendingStart_ = false;
  volatile bool pendingStop_ = false;

  EventFn sink_;
};

extern TimerApp app;
