#include "Notifier.h"
#include "config.h"
#include "secrets.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <time.h>

Notifier* Notifier::instance_ = nullptr;

// MQTT transport. For TLS we use WiFiClientSecure (setInsecure by default — see
// docs/SECURITY.md; pin a CA for production).
#if MQTT_USE_TLS
  static WiFiClientSecure mqttNet;
#else
  static WiFiClient mqttNet;
#endif
static PubSubClient mqtt(mqttNet);

void Notifier::begin(Storage* storage) {
  storage_  = storage;
  instance_ = this;
#if MQTT_USE_TLS
  mqttNet.setInsecure();
#endif
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(&Notifier::mqttCallback);
  mqtt.setBufferSize(2048);
}

void Notifier::loop() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!mqtt.connected()) ensureMqtt();
  mqtt.loop();
}

void Notifier::ensureMqtt() {
  if (millis() - lastMqttTry_ < 5000) return;   // backoff
  lastMqttTry_ = millis();
  String cid = String(DEVICE_ID) + "-" + String((uint32_t)esp_random(), HEX);
  if (mqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS)) {
    mqtt.subscribe(TOPIC_CMD);
  }
}

bool Notifier::mqttConnected() { return mqtt.connected(); }

void Notifier::mqttCallback(char* topic, uint8_t* payload, unsigned int len) {
  if (!instance_) return;
  String t(topic);
  String msg;
  msg.reserve(len);
  for (unsigned i = 0; i < len; i++) msg += (char)payload[i];

  if (t == TOPIC_CMD && instance_->cmdHandler_) {
    // Payload: {"action":"dispense"} or {"action":"schedule","data":[...]}
    JsonDocument doc;
    if (deserializeJson(doc, msg) == DeserializationError::Ok) {
      String action = doc["action"] | "";
      String data;
      if (!doc["data"].isNull()) serializeJson(doc["data"], data);
      instance_->cmdHandler_(action, data);
    }
  }
}

void Notifier::publishStatus(const String& json) {
  if (mqtt.connected()) mqtt.publish(TOPIC_STATUS, json.c_str());
}

void Notifier::requestCameraCapture(const String& caption) {
  if (!mqtt.connected()) return;
  JsonDocument doc;
  doc["caption"] = caption;
  doc["ts"]      = (long)time(nullptr);
  String out; serializeJson(doc, out);
  mqtt.publish(TOPIC_CAM_CAPTURE, out.c_str());
}

String Notifier::urlencode(const String& s) {
  String out; out.reserve(s.length() * 3);
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < (size_t)s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

void Notifier::telegram(const String& text) {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client;
  client.setInsecure();   // see docs/SECURITY.md — pin Telegram's CA for prod
  HTTPClient https;
  String url = String("https://api.telegram.org/bot") + TELEGRAM_BOT_TOKEN + "/sendMessage";
  if (!https.begin(client, url)) return;
  https.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = "chat_id=" + String(TELEGRAM_CHAT_ID) +
                "&text=" + urlencode(text);
  https.POST(body);
  https.end();
}

void Notifier::event(const String& state, const String& label, bool withPhoto) {
  time_t now = time(nullptr);
  appendEvent(state, label, now);

  // MQTT event
  if (mqtt.connected()) {
    JsonDocument doc;
    doc["state"] = state;
    doc["label"] = label;
    doc["ts"]    = (long)now;
    String out; serializeJson(doc, out);
    mqtt.publish(TOPIC_EVENT, out.c_str(), true);   // retained
  }

  // Human-friendly Telegram line (Turkish-facing labels supplied by caller).
  String emoji = state == "taken"  ? "✅"
               : state == "missed" ? "⚠️"
               : state == "jam"    ? "⛔"
               : state == "due"    ? "💊"
               : "ℹ️";
  telegram(emoji + " " + label + " — " + state);

  if (withPhoto) requestCameraCapture(label + " (" + state + ")");
}

void Notifier::appendEvent(const String& state, const String& label, time_t when) {
  JsonDocument doc;
  deserializeJson(doc, storage_->loadEventsJson());
  if (!doc.is<JsonArray>()) doc.to<JsonArray>();
  JsonArray arr = doc.as<JsonArray>();

  JsonObject o = arr.add<JsonObject>();
  o["state"] = state;
  o["label"] = label;
  o["ts"]    = (long)when;

  while ((int)arr.size() > MAX_EVENTS) arr.remove(0);   // ring buffer

  String out; serializeJson(doc, out);
  storage_->saveEventsJson(out);
}

String Notifier::eventsJson() {
  return storage_->loadEventsJson();
}
