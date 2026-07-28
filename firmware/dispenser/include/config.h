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
