// secrets.example.h — copy to secrets.h and fill in. secrets.h is git-ignored.
//
//   cp include/secrets.example.h include/secrets.h
//
// NOTE: WEB_ADMIN_PASS here is only the *initial seed* used on first boot. The
// device stores a salted SHA-256 hash in NVS and you change it from the web UI.
#pragma once

// ── Wi-Fi ────────────────────────────────────────────────────────────────────
#define WIFI_SSID          "your-2.4GHz-ssid"
#define WIFI_PASS          "your-wifi-password"

// ── Web admin (seed credentials) ─────────────────────────────────────────────
#define WEB_ADMIN_USER     "admin"
#define WEB_ADMIN_PASS     "change-me-to-a-long-unique-passphrase"

// ── Telegram ─────────────────────────────────────────────────────────────────
// Create a bot with @BotFather; add it to a PRIVATE family group; get the
// group's numeric chat id (e.g. via @RawDataBot). Restrict commands to it.
#define TELEGRAM_BOT_TOKEN        "123456789:AA...your-token"
#define TELEGRAM_CHAT_ID          "-1001234567890"   // family group
#define TELEGRAM_ALLOWED_CHAT_ID  "-1001234567890"   // only accept cmds from here

// ── MQTT broker ──────────────────────────────────────────────────────────────
#define MQTT_HOST          "your-broker.example.com"
#define MQTT_PORT          8883          // 8883 TLS, 1883 plain
#define MQTT_USE_TLS       true
#define MQTT_USER          "pillpilot"
#define MQTT_PASS          "broker-password"

// ── TLS (optional overrides) ─────────────────────────────────────────────────
// By default (config.h TLS_USE_BUNDLE=1) certs are verified against the built-in
// Mozilla root bundle — nothing to set here. Only if you set TLS_USE_BUNDLE=0
// do you need to pin Telegram's root, e.g. fetch it with:
//   openssl s_client -showcerts -connect api.telegram.org:443 </dev/null
// #define TELEGRAM_ROOT_CA "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"
//
// For a self-signed MQTT broker, pin its CA to verify it (otherwise the bundle
// is used). Leave undefined for a broker with a publicly-trusted cert:
// #define MQTT_CA_CERT     "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"
