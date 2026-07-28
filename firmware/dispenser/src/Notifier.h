// Notifier.h — outbound alerts (Telegram over TLS), MQTT state/commands,
// camera-capture requests, and a persisted event log.
#pragma once
#include <Arduino.h>
#include <functional>
#include "Storage.h"

class Notifier {
public:
  void begin(Storage* storage);
  void loop();

  // Called with (action, payload) when a command arrives on the MQTT cmd topic.
  void setCommandHandler(std::function<void(const String&, const String&)> h) {
    cmdHandler_ = h;
  }

  // Log + broadcast an event. state e.g. "taken","missed","jam","due","boot".
  void event(const String& state, const String& label, bool withPhoto = false);

  void   telegram(const String& text);         // plain message (TLS)
  void   requestCameraCapture(const String& caption);
  void   publishStatus(const String& json);
  bool   mqttConnected();
  String eventsJson();                          // recent events for the UI

  static String urlencode(const String& s);

private:
  void ensureMqtt();
  void appendEvent(const String& state, const String& label, time_t when);

  static void mqttCallback(char* topic, uint8_t* payload, unsigned int len);
  static Notifier* instance_;

  Storage* storage_ = nullptr;
  std::function<void(const String&, const String&)> cmdHandler_;
  uint32_t lastMqttTry_ = 0;
};
