// config.h — ESP32-CAM (AI-Thinker) node. Non-secret config only.
#pragma once

#define DEVICE_ID        "pillpilot-01"                 // MUST match the dispenser
#define MDNS_HOSTNAME    "pillpilot-cam"                // http://pillpilot-cam.local/
#define TOPIC_CAM_CAPTURE  DEVICE_ID "/cam/capture"     // subscribe: capture request

// Onboard indicators
#define PIN_LED_RED      33      // active-LOW status LED on the AI-Thinker board
#define PIN_LED_FLASH    4       // bright flash LED — kept OFF by default

// Capture
#define JPEG_QUALITY     12      // lower = better quality, bigger file
#define USE_FLASH_ON_CAPTURE  false  // avoid blinding an elderly person at night

// TLS verification (Telegram + MQTT). See docs/SECURITY.md.
#define TLS_INSECURE    0    // 1 = skip verification (debug only)
#define TLS_USE_BUNDLE  1    // 1 = Mozilla CA bundle, 0 = pinned TELEGRAM_ROOT_CA

// ── AI-Thinker ESP32-CAM camera pin map ──────────────────────────────────────
#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27
#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      21
#define Y4_GPIO_NUM      19
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22
