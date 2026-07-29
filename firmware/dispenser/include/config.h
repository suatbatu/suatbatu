// config.h — pin map, mechanical constants, and behavioural tuning.
// Non-secret configuration only. Credentials live in secrets.h.
#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  Identity & time
// ─────────────────────────────────────────────────────────────────────────────
#define DEVICE_ID        "pillpilot-01"   // unique per unit; used in MQTT topics
#define MDNS_HOSTNAME    "pillpilot"       // http://pillpilot.local/
#define TZ_INFO          "<+03>-3"         // Turkey (UTC+3, no DST)
#define NTP_SERVER       "pool.ntp.org"
#define CAM_NODE_URL     "http://pillpilot-cam.local"  // camera node (UI links here)

// ─────────────────────────────────────────────────────────────────────────────
//  Motor driver selection
// ─────────────────────────────────────────────────────────────────────────────
#define DRIVER_ULN2003   0   // 28BYJ-48 unipolar (v1 default)
#define DRIVER_A4988     1   // NEMA-17 + A4988/DRV8825 (v2)
#define MOTOR_DRIVER     DRIVER_ULN2003

// 28BYJ-48: 4096 half-steps / rev (with 1/64 gearbox). NEMA-17: 200*microstep.
#if MOTOR_DRIVER == DRIVER_ULN2003
  #define STEPS_PER_REV  4096L
#else
  #define STEPS_PER_REV  3200L   // 200 full-steps * 16 microsteps — tune to yours
#endif

// ─────────────────────────────────────────────────────────────────────────────
//  Carousel mechanics — MUST match your physical design (hardware/README.md)
// ─────────────────────────────────────────────────────────────────────────────
#define CAROUSEL_SLOTS   7      // compartments on the disc
#define CAROUSEL_DIR     1      // 1 or 0 — flip if it turns the wrong way
#define HOME_ON_BOOT     true   // seek slot 0 via hall sensor at startup

// ─────────────────────────────────────────────────────────────────────────────
//  Pin map (ESP32 DevKit-C). See docs/WIRING.md.
// ─────────────────────────────────────────────────────────────────────────────
#if MOTOR_DRIVER == DRIVER_ULN2003
  #define PIN_IN1        19
  #define PIN_IN2        18
  #define PIN_IN3        5
  #define PIN_IN4        17
#else
  #define PIN_STEP       18
  #define PIN_DIR        19
  #define PIN_EN         5      // active-low enable
#endif

#define PIN_HALL_HOME    34     // hall homing sensor (input-only pin)
#define PIN_IR_DROP      35     // IR break-beam across chute (input-only pin)
#define PIN_TAKEN_BTN    32     // cup switch / "taken" button, INPUT_PULLUP to GND
#define PIN_BUZZER       25
#define PIN_STATUS_LED   26
#define PIN_VBAT_SENSE   33     // optional battery voltage divider (ADC)

// Sensor logic levels (invert here if your hardware is active-high/low)
#define HALL_ACTIVE_LOW  true   // A3144 pulls LOW when magnet present
#define IR_BEAM_BROKEN_LOW true  // beam broken -> LOW on most receivers
#define TAKEN_ACTIVE_LOW true    // button to GND with pullup

// ─────────────────────────────────────────────────────────────────────────────
//  Behaviour
// ─────────────────────────────────────────────────────────────────────────────
#define DOSE_GRACE_MINUTES     45   // window to confirm "taken" before MISSED
#define REMIND_EVERY_MINUTES   10   // re-buzz + re-notify cadence while waiting
#define DROP_DETECT_TIMEOUT_MS 4000 // wait for the IR beam after a rotation
#define MAX_DOSES              12    // schedule capacity
#define MAX_EVENTS             30    // event-log ring buffer size

// ─────────────────────────────────────────────────────────────────────────────
//  Battery monitoring (optional 18650 UPS on PIN_VBAT_SENSE)
// ─────────────────────────────────────────────────────────────────────────────
#define VBAT_ENABLED           true
#define VBAT_DIVIDER_RATIO     2.0f    // (R1+R2)/R2 of the sense divider
#define VBAT_WARN_MV           3500    // Li-ion "getting low"
#define VBAT_CRITICAL_MV       3300    // Li-ion "act now"
#define VBAT_FULL_MV           4200    // for percent estimate
#define VBAT_EMPTY_MV          3200
#define VBAT_CHECK_INTERVAL_MS (60UL * 1000UL)

// ─────────────────────────────────────────────────────────────────────────────
//  Web / auth
// ─────────────────────────────────────────────────────────────────────────────
#define WEB_PORT               80
#define MAX_SESSIONS           5
#define SESSION_TTL_SECONDS    (60 * 60 * 12)  // 12h
#define AUTH_MAX_ATTEMPTS      5
#define AUTH_LOCKOUT_MS        (60UL * 1000UL) // lock 60s after too many fails

// ─────────────────────────────────────────────────────────────────────────────
//  MQTT topics (built from DEVICE_ID)
// ─────────────────────────────────────────────────────────────────────────────
#define TOPIC_EVENT        DEVICE_ID "/event"        // publish taken/missed/jam
#define TOPIC_STATUS       DEVICE_ID "/status"       // publish periodic status
#define TOPIC_CMD          DEVICE_ID "/cmd"          // subscribe: dispense/schedule
#define TOPIC_CAM_CAPTURE  DEVICE_ID "/cam/capture"  // publish: ask cam for a photo

// ─────────────────────────────────────────────────────────────────────────────
//  Telegram command polling
// ─────────────────────────────────────────────────────────────────────────────
#define TELEGRAM_POLL_INTERVAL_MS  3000   // getUpdates cadence
#define DOZ_CONFIRM_SECONDS        60     // /doz must be confirmed with /doz_onay

// ─────────────────────────────────────────────────────────────────────────────
//  TLS certificate verification (Telegram + MQTT). See docs/SECURITY.md.
//  Default: validate against the arduino-esp32 built-in Mozilla root bundle
//  (rotation-proof). Set TLS_USE_BUNDLE 0 to pin TELEGRAM_ROOT_CA from secrets.h.
// ─────────────────────────────────────────────────────────────────────────────
#define TLS_INSECURE    0    // 1 = skip verification (debug only — NOT for prod)
#define TLS_USE_BUNDLE  1    // 1 = Mozilla CA bundle, 0 = pinned TELEGRAM_ROOT_CA
