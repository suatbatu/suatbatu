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

  // Called with (action, payload) when a command arrives (MQTT or Telegram).
  void setCommandHandler(std::function<void(const String&, const String&)> h) {
    cmdHandler_ = h;
  }

  // Supplies a human-readable status string for the Telegram /durum command.
  void setStatusTextProvider(std::function<String()> h) { statusProvider_ = h; }

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

  // Telegram command polling (getUpdates)
  void pollTelegram();
  void handleTelegramCommand(const String& cmd, long chatId, time_t now);

  static void mqttCallback(char* topic, uint8_t* payload, unsigned int len);
  static Notifier* instance_;

  Storage* storage_ = nullptr;
  std::function<void(const String&, const String&)> cmdHandler_;
  std::function<String()>                            statusProvider_;
  uint32_t lastMqttTry_    = 0;

  // Telegram polling state
  long     tgOffset_       = 0;
  bool     tgPrimed_       = false;   // drained backlog once after boot?
  uint32_t lastTgPoll_     = 0;
  time_t   dozConfirmUntil_ = 0;      // /doz awaiting /doz_onay until this epoch
};
