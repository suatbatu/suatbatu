#include "Net.h"

#include <WiFi.h>

#include "Settings.h"
#include "config.h"

Net net;

namespace {
constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 8000;
constexpr uint32_t STA_RECHECK_MS = 30000;
}  // namespace

void Net::startAp() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ssid[32];
  snprintf(ssid, sizeof(ssid), AP_SSID_PREFIX "%02X%02X", mac[4], mac[5]);
  apSsid_ = ssid;

  WiFi.mode(WIFI_AP);
  // The AP is open on purpose: it carries no traffic worth protecting, the web
  // UI itself is password-protected, and a shooter with cold hands should not
  // be typing a WPA key into a phone between stages. See docs/API.md.
  WiFi.softAP(ssid, nullptr, AP_CHANNEL);
  isAp_ = true;
  statusLine_ = String("AP ") + ssid;
}

void Net::begin() {
  WiFi.persistent(false);
  WiFi.setSleep(false);  // keeps the live view responsive between shots

  if (settings.wifiSsid[0] == '\0') {
    startAp();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(settings.wifiSsid, settings.wifiPass);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - started) < STA_CONNECT_TIMEOUT_MS) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    isAp_ = false;
    statusLine_ = WiFi.localIP().toString();
  } else {
    // The configured network is not here — which at a range is the normal
    // outcome, not an error. Serve our own and carry on.
    WiFi.disconnect(true);
    startAp();
  }
  lastCheckMs_ = millis();
}

void Net::loop() {
  const uint32_t now = millis();
  if (now - lastCheckMs_ < STA_RECHECK_MS) return;
  lastCheckMs_ = now;

  if (!isAp_ && WiFi.status() != WL_CONNECTED) {
    // Dropped off the home network mid-session; fall back rather than go dark.
    startAp();
  }
}

String Net::ipString() const {
  return isAp_ ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}
