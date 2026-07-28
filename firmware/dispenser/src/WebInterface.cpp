#include "WebInterface.h"
#include "config.h"
#include <LittleFS.h>

// Accumulate a POST body into a String hung off request->_tempObject.
static void bodyAccumulate(AsyncWebServerRequest* req, uint8_t* data,
                           size_t len, size_t index, size_t total) {
  if (index == 0) {
    auto* buf = new String();
    buf->reserve(total ? total : len);
    req->_tempObject = buf;
  }
  auto* buf = reinterpret_cast<String*>(req->_tempObject);
  if (buf) for (size_t i = 0; i < len; i++) *buf += (char)data[i];
}

static String takeBody(AsyncWebServerRequest* req) {
  auto* buf = reinterpret_cast<String*>(req->_tempObject);
  String out = buf ? *buf : String();
  if (buf) { delete buf; req->_tempObject = nullptr; }
  return out;
}

bool WebInterface::authed(AsyncWebServerRequest* req) {
  if (!req->hasHeader("Cookie")) return false;
  String tok = Auth::cookieToken(req->header("Cookie"));
  return auth_->validateSession(tok);
}

void WebInterface::sendUnauthorized(AsyncWebServerRequest* req) {
  req->send(401, "application/json", "{\"error\":\"unauthorized\"}");
}

void WebInterface::begin(Auth* auth, const WebHooks& hooks) {
  auth_  = auth;
  hooks_ = hooks;

  // ── Public static UI ───────────────────────────────────────────────────────
  server_.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // ── Login (public) ─────────────────────────────────────────────────────────
  server_.on("/api/login", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (auth_->isLockedOut()) {
      req->send(429, "application/json", "{\"error\":\"locked_out\"}");
      return;
    }
    if (!req->hasParam("user", true) || !req->hasParam("pass", true)) {
      req->send(400, "application/json", "{\"error\":\"missing_fields\"}");
      return;
    }
    String user = req->getParam("user", true)->value();
    String pass = req->getParam("pass", true)->value();
    if (!auth_->check(user, pass)) {
      sendUnauthorized(req);
      return;
    }
    String tok = auth_->createSession();
    if (tok.isEmpty()) {
      req->send(503, "application/json", "{\"error\":\"session_table_full\"}");
      return;
    }
    AsyncWebServerResponse* res = req->beginResponse(200, "application/json", "{\"ok\":true}");
    String cookie = "SESSION=" + tok + "; HttpOnly; SameSite=Strict; Path=/; Max-Age="
                    + String(SESSION_TTL_SECONDS);
    res->addHeader("Set-Cookie", cookie);
    req->send(res);
  });

  // ── Logout ─────────────────────────────────────────────────────────────────
  server_.on("/api/logout", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (req->hasHeader("Cookie"))
      auth_->destroySession(Auth::cookieToken(req->header("Cookie")));
    AsyncWebServerResponse* res = req->beginResponse(200, "application/json", "{\"ok\":true}");
    res->addHeader("Set-Cookie", "SESSION=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
    req->send(res);
  });

  // ── Status (protected) ─────────────────────────────────────────────────────
  server_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!authed(req)) { sendUnauthorized(req); return; }
    req->send(200, "application/json", hooks_.statusJson ? hooks_.statusJson() : "{}");
  });

  // ── Events (protected) ─────────────────────────────────────────────────────
  server_.on("/api/events", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!authed(req)) { sendUnauthorized(req); return; }
    req->send(200, "application/json", hooks_.eventsJson ? hooks_.eventsJson() : "[]");
  });

  // ── Get schedule (protected) ───────────────────────────────────────────────
  server_.on("/api/schedule", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!authed(req)) { sendUnauthorized(req); return; }
    req->send(200, "application/json", hooks_.scheduleJson ? hooks_.scheduleJson() : "[]");
  });

  // ── Set schedule (protected, JSON body) ────────────────────────────────────
  server_.on("/api/schedule", HTTP_POST,
    [this](AsyncWebServerRequest* req) {
      if (!authed(req)) { takeBody(req); sendUnauthorized(req); return; }
      String body = takeBody(req);
      bool ok = hooks_.setSchedule && hooks_.setSchedule(body);
      req->send(ok ? 200 : 400, "application/json",
                ok ? "{\"ok\":true}" : "{\"error\":\"invalid_schedule\"}");
    },
    nullptr, bodyAccumulate);

  // ── Manual/test dispense (protected) ───────────────────────────────────────
  server_.on("/api/dispense", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!authed(req)) { sendUnauthorized(req); return; }
    bool ok = hooks_.dispenseNow && hooks_.dispenseNow();
    req->send(ok ? 200 : 409, "application/json",
              ok ? "{\"ok\":true}" : "{\"error\":\"busy\"}");
  });

  // ── Jog (protected) ────────────────────────────────────────────────────────
  server_.on("/api/jog", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!authed(req)) { sendUnauthorized(req); return; }
    int steps = req->hasParam("steps", true) ? req->getParam("steps", true)->value().toInt() : 0;
    bool ok = hooks_.jog && hooks_.jog(steps);
    req->send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"bad_request\"}");
  });

  // ── Re-home (protected) ────────────────────────────────────────────────────
  server_.on("/api/home", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!authed(req)) { sendUnauthorized(req); return; }
    bool ok = hooks_.home && hooks_.home();
    req->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"home_failed\"}");
  });

  // ── Change password (protected) ────────────────────────────────────────────
  server_.on("/api/password", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!authed(req)) { sendUnauthorized(req); return; }
    if (!req->hasParam("current", true) || !req->hasParam("new", true)) {
      req->send(400, "application/json", "{\"error\":\"missing_fields\"}"); return;
    }
    String cur = req->getParam("current", true)->value();
    String nw  = req->getParam("new", true)->value();
    if (!auth_->check(auth_->username(), cur)) { sendUnauthorized(req); return; }
    bool ok = auth_->setPassword(auth_->username(), nw);
    req->send(ok ? 200 : 400, "application/json",
              ok ? "{\"ok\":true}" : "{\"error\":\"weak_password\"}");
  });

  server_.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "application/json", "{\"error\":\"not_found\"}");
  });

  server_.begin();
}
