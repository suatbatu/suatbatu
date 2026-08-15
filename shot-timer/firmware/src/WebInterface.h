// HTTP + WebSocket interface.
//
// REST for things that change rarely (settings, stored strings), a WebSocket
// for the live view — a running string produces an event every time a shot
// lands, and polling for that would either be laggy or hammer the device.
//
// Everything is behind HTTP basic auth. See docs/API.md.
#pragma once

#include <ArduinoJson.h>

class WebInterface {
 public:
  // Returns false (and starts nothing) if no web password is configured.
  bool begin();
  void loop();

  void pushEvent(const JsonDocument& doc);

 private:
  bool started_ = false;
  uint32_t lastStatusPushMs_ = 0;
};

extern WebInterface web;
