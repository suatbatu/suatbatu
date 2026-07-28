// WebInterface.h — password-protected async web server.
//
// Auth model (see docs/SECURITY.md):
//  - Static UI (HTML/CSS/JS) is public; all /api/* control endpoints require a
//    valid session cookie issued by /api/login.
//  - Login is rate-limited via Auth. Sessions are HttpOnly, SameSite=Strict.
//  - Serve over a VPN or TLS reverse proxy for remote access — never expose the
//    ESP32 directly to the internet.
#pragma once
#include <Arduino.h>
#include <functional>
#include <ESPAsyncWebServer.h>
#include "Auth.h"

// Control callbacks supplied by main.cpp so the web layer stays decoupled from
// the carousel/scheduler internals.
struct WebHooks {
  std::function<String()>                statusJson;    // live status
  std::function<String()>                scheduleJson;  // current schedule
  std::function<bool(const String&)>     setSchedule;   // validate + persist
  std::function<bool()>                  dispenseNow;   // manual/test dispense
  std::function<bool(int)>               jog;           // jog N steps (+/-)
  std::function<bool()>                  home;          // re-home carousel
  std::function<String()>                eventsJson;    // event log
};

class WebInterface {
public:
  void begin(Auth* auth, const WebHooks& hooks);

private:
  bool authed(AsyncWebServerRequest* req);
  void sendUnauthorized(AsyncWebServerRequest* req);

  AsyncWebServer server_{WEB_PORT};
  Auth*   auth_ = nullptr;
  WebHooks hooks_;
};
