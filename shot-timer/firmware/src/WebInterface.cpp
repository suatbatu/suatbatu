#include "WebInterface.h"

#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <esp_random.h>

#include <memory>
#include <set>

#include "Net.h"
#include "Settings.h"
#include "ShotDetector.h"
#include "Storage.h"
#include "TimerApp.h"
#include "config.h"

WebInterface web;

namespace {

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Browsers cannot set headers on a WebSocket handshake, and basic-auth replay
// on the handshake is inconsistent between browsers. So the socket authenticates
// itself: the first frame a client sends must be the token it fetched from the
// basic-auth-protected /api/wstoken. Until then it receives nothing.
char wsToken[33] = "";
std::set<uint32_t> authedClients;

// While a string is open the client wants a moving clock, not just shot events.
constexpr uint32_t LIVE_STATUS_MS = 200;

void makeToken() {
  static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  for (size_t i = 0; i < sizeof(wsToken) - 1; i++) {
    wsToken[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
  }
  wsToken[sizeof(wsToken) - 1] = '\0';
}

bool authed(AsyncWebServerRequest* request) {
  if (request->authenticate(settings.webUser, settings.webPass)) return true;
  request->requestAuthentication();
  return false;
}

void sendJson(AsyncWebServerRequest* request, int code, const JsonDocument& doc) {
  AsyncResponseStream* res = request->beginResponseStream("application/json");
  res->setCode(code);
  serializeJson(doc, *res);
  request->send(res);
}

void sendStatus(AsyncWebServerRequest* request) {
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  app.statusJson(o);
  o["stored"] = storage.count();
  o["ip"] = net.ipString();
  o["ap"] = net.isAp();
  o["heap"] = ESP.getFreeHeap();
  sendJson(request, 200, doc);
}

// --- CSV export ------------------------------------------------------------
// Chunked, because a full history is a few hundred kilobytes of text and the
// device has a few hundred kilobytes of RAM in total.
struct CsvState {
  File file;
  String pending;
  bool eof = false;
};

void csvAppendString(String& out, const String& line) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) return;
  const uint32_t id = doc["id"] | 0u;
  JsonArrayConst shots = doc["shots"];
  uint32_t prev = 0;
  uint8_t i = 0;
  for (JsonVariantConst v : shots) {
    const uint32_t t = v.as<uint32_t>();
    const uint32_t split = i == 0 ? t : t - prev;
    char row[64];
    snprintf(row, sizeof(row), "%lu,%u,%lu.%02lu,%lu.%02lu\n", static_cast<unsigned long>(id),
             i + 1, static_cast<unsigned long>(t / 1000),
             static_cast<unsigned long>((t % 1000) / 10), static_cast<unsigned long>(split / 1000),
             static_cast<unsigned long>((split % 1000) / 10));
    out += row;
    prev = t;
    i++;
  }
}

}  // namespace

bool WebInterface::begin() {
  if (settings.webPass[0] == '\0') {
    // Refusing to serve is the right default: an unauthenticated timer on an
    // open AP lets any passer-by fire the buzzer mid-string.
    log_w("no web password set — web interface disabled");
    return false;
  }
  makeToken();

  // ---- WebSocket --------------------------------------------------------
  ws.onEvent([](AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void*,
                uint8_t* data, size_t len) {
    switch (type) {
      case WS_EVT_DISCONNECT:
      case WS_EVT_ERROR:
        authedClients.erase(client->id());
        break;

      case WS_EVT_DATA: {
        if (authedClients.count(client->id())) return;  // already in, ignore chatter
        String msg;
        msg.reserve(len);
        for (size_t i = 0; i < len; i++) msg += static_cast<char>(data[i]);
        if (wsToken[0] && msg == wsToken) {
          authedClients.insert(client->id());
          JsonDocument doc;
          JsonObject o = doc.to<JsonObject>();
          o["type"] = "state";
          app.statusJson(o);
          String out;
          serializeJson(doc, out);
          client->text(out);
        } else {
          client->close(1008, "bad token");
        }
        break;
      }

      default:
        break;
    }
  });
  server.addHandler(&ws);

  // ---- read-only endpoints ----------------------------------------------
  server.on("/api/wstoken", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!authed(request)) return;
    JsonDocument doc;
    doc["token"] = wsToken;
    sendJson(request, 200, doc);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!authed(request)) return;
    sendStatus(request);
  });

  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!authed(request)) return;
    JsonDocument doc;
    settings.toJson(doc.to<JsonObject>());
    sendJson(request, 200, doc);
  });

  server.on("/api/strings", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!authed(request)) return;
    // Summaries only. The shot arrays are what make this big, and the history
    // list does not need them — /api/string?id= returns one string in full.
    AsyncResponseStream* res = request->beginResponseStream("application/json");
    res->print("{\"strings\":[");
    bool first = true;
    storage.forEachLine([&](const String& line) {
      JsonDocument doc;
      if (deserializeJson(doc, line)) return true;
      doc.remove("shots");
      if (!first) res->print(',');
      first = false;
      serializeJson(doc, *res);
      return true;
    });
    res->print("]}");
    request->send(res);
  });

  server.on("/api/string", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!authed(request)) return;
    if (!request->hasParam("id")) {
      request->send(400, "text/plain", "missing id");
      return;
    }
    const uint32_t id = request->getParam("id")->value().toInt();
    const String line = storage.findById(id);
    if (line.isEmpty()) {
      request->send(404, "text/plain", "no such string");
      return;
    }
    request->send(200, "application/json", line);
  });

  server.on("/api/export.csv", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!authed(request)) return;
    auto st = std::make_shared<CsvState>();
    st->file = LittleFS.open(PATH_STRINGS, "r");
    st->pending = "string_id,shot,time_s,split_s\n";

    AsyncWebServerResponse* res = request->beginChunkedResponse(
        "text/csv", [st](uint8_t* buffer, size_t maxLen, size_t) -> size_t {
          while (st->pending.length() < maxLen && !st->eof) {
            if (!st->file || !st->file.available()) {
              st->eof = true;
              break;
            }
            const String line = st->file.readStringUntil('\n');
            if (line.length() >= 2) csvAppendString(st->pending, line);
          }
          const size_t n = std::min(maxLen, static_cast<size_t>(st->pending.length()));
          if (n == 0) return 0;  // pending empty and eof -> response complete
          memcpy(buffer, st->pending.c_str(), n);
          st->pending.remove(0, n);
          return n;
        });
    res->addHeader("Content-Disposition", "attachment; filename=\"shot-timer.csv\"");
    request->send(res);
  });

  // ---- control ----------------------------------------------------------
  server.on("/api/start", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!authed(request)) return;
    // Queued, not executed here: this runs on the AsyncTCP task and the state
    // machine belongs to the main loop. The WebSocket reports the transition.
    app.postStart();
    sendStatus(request);
  });

  server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!authed(request)) return;
    app.postStop();
    sendStatus(request);
  });

  server.on("/api/strings", HTTP_DELETE, [](AsyncWebServerRequest* request) {
    if (!authed(request)) return;
    storage.clear();
    sendStatus(request);
  });

  auto* settingsHandler = new AsyncCallbackJsonWebHandler(
      "/api/settings", [](AsyncWebServerRequest* request, JsonVariant& json) {
        if (!authed(request)) return;
        if (!json.is<JsonObject>()) {
          request->send(400, "text/plain", "expected a JSON object");
          return;
        }
        const bool ok = settings.applyJson(json.as<JsonObjectConst>());
        settings.save();
        JsonDocument doc;
        JsonObject o = doc.to<JsonObject>();
        settings.toJson(o);
        o["accepted"] = ok;
        // 200 with accepted=false: the valid keys were applied, at least one
        // was rejected, and the body says exactly what the settings now are.
        sendJson(request, 200, doc);
      });
  settingsHandler->setMethod(HTTP_POST);
  server.addHandler(settingsHandler);

  // ---- static UI ---------------------------------------------------------
  server.serveStatic("/", LittleFS, "/")
      .setDefaultFile("index.html")
      .setAuthentication(settings.webUser, settings.webPass);

  server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "not found");
  });

  server.begin();
  started_ = true;
  return true;
}

void WebInterface::pushEvent(const JsonDocument& doc) {
  if (!started_ || authedClients.empty()) return;
  String out;
  serializeJson(doc, out);
  for (uint32_t id : authedClients) {
    AsyncWebSocketClient* c = ws.client(id);
    if (c && c->canSend()) c->text(out);
  }
}

void WebInterface::loop() {
  if (!started_) return;
  ws.cleanupClients();

  const AppState st = app.state();
  if (st != AppState::Running) return;

  const uint32_t now = millis();
  if (now - lastStatusPushMs_ < LIVE_STATUS_MS) return;
  lastStatusPushMs_ = now;

  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["type"] = "tick";
  o["elapsedMs"] = app.elapsedMs();
  pushEvent(doc);
}
