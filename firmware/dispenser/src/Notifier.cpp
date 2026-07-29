#include "Notifier.h"
#include "config.h"
#include "secrets.h"
#include "Tls.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <time.h>

Notifier* Notifier::instance_ = nullptr;

// MQTT transport. For TLS we use WiFiClientSecure, verified via configureTls()
// (Mozilla CA bundle, or a pinned MQTT_CA_CERT). See docs/SECURITY.md.
#if MQTT_USE_TLS
  static WiFiClientSecure mqttNet;
#else
  static WiFiClient mqttNet;
#endif
static PubSubClient mqtt(mqttNet);

void Notifier::begin(Storage* storage) {
  storage_  = storage;
  instance_ = this;
  tgOffset_ = storage_->loadTgOffset();   // resume point; backlog drained on 1st poll
#if MQTT_USE_TLS
  #ifdef MQTT_CA_CERT
    mqttNet.setCACert(MQTT_CA_CERT);       // pin a self-signed broker CA
  #else
    configureTls(mqttNet);                 // verify against the CA bundle
  #endif
#endif
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(&Notifier::mqttCallback);
  mqtt.setBufferSize(2048);
}

void Notifier::loop() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!mqtt.connected()) ensureMqtt();
  mqtt.loop();

  if (millis() - lastTgPoll_ >= TELEGRAM_POLL_INTERVAL_MS) {
    lastTgPoll_ = millis();
    pollTelegram();
  }
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
  configureTls(client);   // verify Telegram's certificate (see docs/SECURITY.md)
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

// ── Telegram command polling (getUpdates) ─────────────────────────────────────
void Notifier::pollTelegram() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  configureTls(client);
  HTTPClient https;
  String url = String("https://api.telegram.org/bot") + TELEGRAM_BOT_TOKEN +
               "/getUpdates?timeout=0&limit=10&allowed_updates=%5B%22message%22%5D";
  if (tgOffset_ > 0) url += "&offset=" + String(tgOffset_);

  if (!https.begin(client, url)) return;
  int code = https.GET();
  if (code != 200) { https.end(); return; }

  String payload = https.getString();
  https.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) return;
  if (!(doc["ok"] | false)) return;

  time_t now = time(nullptr);
  for (JsonObject upd : doc["result"].as<JsonArray>()) {
    long updId = upd["update_id"] | 0;
    tgOffset_ = updId + 1;

    if (!tgPrimed_) continue;             // drain boot backlog silently

    JsonObject msg = upd["message"];
    if (msg.isNull()) continue;
    long chatId = msg["chat"]["id"] | 0;
    String text = String((const char*)(msg["text"] | ""));
    text.trim();
    if (text.startsWith("/")) handleTelegramCommand(text, chatId, now);
  }

  storage_->saveTgOffset(tgOffset_);
  tgPrimed_ = true;
}

void Notifier::handleTelegramCommand(const String& raw, long chatId, time_t now) {
  // Authorization: only the configured family group may command the device.
  if (String(chatId) != String(TELEGRAM_ALLOWED_CHAT_ID)) return;

  // Normalize: first token, strip any @botname suffix, lowercase.
  String c = raw;
  int sp = c.indexOf(' ');   if (sp > 0) c = c.substring(0, sp);
  int at = c.indexOf('@');   if (at > 0) c = c.substring(0, at);
  c.toLowerCase();

  if (c == "/durum") {
    telegram(statusProvider_ ? statusProvider_() : "Durum bilgisi yok.");
  } else if (c == "/foto") {
    requestCameraCapture("Manuel istek");
    telegram("📸 Fotoğraf istendi.");
  } else if (c == "/doz") {
    dozConfirmUntil_ = now + DOZ_CONFIRM_SECONDS;
    telegram("⚠️ Doz vermek için " + String(DOZ_CONFIRM_SECONDS) +
             " sn içinde /doz_onay gönderin.");
  } else if (c == "/doz_onay") {
    if (dozConfirmUntil_ && now <= dozConfirmUntil_) {
      dozConfirmUntil_ = 0;
      if (cmdHandler_) cmdHandler_("dispense", "");
      telegram("✅ Doz veriliyor.");
    } else {
      telegram("⌛ Onay süresi doldu. Yeniden /doz gönderin.");
    }
  } else if (c == "/yardim" || c == "/help" || c == "/start") {
    telegram("Komutlar:\n/durum — durum\n/foto — fotoğraf\n/doz + /doz_onay — doz ver");
  }
}
