// BLE in the AMG Lab Commander dialect, so PractiScore and anything else that
// already speaks to a Commander can speak to this timer.
//
// The wire format lives in docs/BLE_PROTOCOL.md. Nothing here decides anything
// about the timer: outbound frames mirror the same TimerApp events the web
// interface consumes, and inbound commands are queued for the main loop rather
// than executed on the BLE host task.
#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

class StringRun;

class BleTimer {
 public:
  // Returns false — and starts nothing — when BLE is disabled in settings.
  bool begin();
  void loop();

  bool active() const { return active_; }
  bool connected() const;

  // Subscribed to TimerApp's event sink, alongside the web interface.
  void onTimerEvent(const JsonDocument& doc);

 private:
  void handleCommand(const char* cmd);
  void sendStringFrames(const StringRun& run);

  bool active_ = false;
};

extern BleTimer ble;
