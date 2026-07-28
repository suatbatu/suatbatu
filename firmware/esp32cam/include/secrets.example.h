// secrets.example.h — copy to secrets.h and fill in. secrets.h is git-ignored.
#pragma once

// ── Wi-Fi ────────────────────────────────────────────────────────────────────
#define WIFI_SSID          "your-2.4GHz-ssid"
#define WIFI_PASS          "your-wifi-password"

// ── Telegram (same bot/group as the dispenser) ───────────────────────────────
#define TELEGRAM_BOT_TOKEN "123456789:AA...your-token"
#define TELEGRAM_CHAT_ID   "-1001234567890"

// ── MQTT broker (same broker as the dispenser) ───────────────────────────────
#define MQTT_HOST          "your-broker.example.com"
#define MQTT_PORT          8883
#define MQTT_USE_TLS       true
#define MQTT_USER          "pillpilot-cam"
#define MQTT_PASS          "broker-password"

// ── Snapshot endpoint HTTP Basic auth (its OWN credentials) ──────────────────
#define CAM_HTTP_USER      "viewer"
#define CAM_HTTP_PASS      "change-me-different-from-dispenser"
