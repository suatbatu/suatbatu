// PillPilot — dispenser node main.
//
// Wires together: Wi-Fi + time (RTC/NTP), Storage, Auth, Scheduler, Carousel,
// DoseSensors, Notifier, and the secured WebInterface. Runs the dispensing
// state machine described in docs/ARCHITECTURE.md.
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Wire.h>
#include <RTClib.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "secrets.h"
#include "Storage.h"
#include "Auth.h"
#include "Scheduler.h"
#include "Carousel.h"
#include "DoseSensors.h"
#include "Notifier.h"
#include "WebInterface.h"

// ── State machine ─────────────────────────────────────────────────────────────
enum class State { BOOT, IDLE, DISPENSING, WAIT_CONFIRM, FAULT };
static State state = State::BOOT;

static Storage      storage;
static Auth         auth;
static Scheduler    scheduler;
static Carousel     carousel;
static DoseSensors  sensors;
static Notifier     notifier;
static WebInterface web;
static RTC_DS3231   rtc;
static bool         rtcOk = false;

static int      activeDose   = -1;       // dose index currently being served
static time_t   waitStarted  = 0;
static time_t   lastRemind   = 0;
static char     activeLabel[24] = "Doz";
static bool     manualPending = false;   // web/MQTT-triggered test dispense

static const uint32_t WDT_TIMEOUT_S = 30;

// ── Time helpers ──────────────────────────────────────────────────────────────
static void syncClock() {
  Wire.begin(21, 22);
  rtcOk = rtc.begin();

  setenv("TZ", TZ_INFO, 1);
  tzset();

  // Seed system time from the RTC first, so schedules work even without Wi-Fi.
  if (rtcOk) {
    DateTime n = rtc.now();
    struct timeval tv = { .tv_sec = (time_t)n.unixtime(), .tv_usec = 0 };
    settimeofday(&tv, nullptr);
  }

  // If online, discipline both system clock and RTC from NTP.
  if (WiFi.status() == WL_CONNECTED) {
    configTzTime(TZ_INFO, NTP_SERVER, "time.google.com");
    struct tm tm;
    if (getLocalTime(&tm, 8000)) {
      time_t now = time(nullptr);
      if (rtcOk) rtc.adjust(DateTime((uint32_t)now));
    }
  }
}

static String isoLocal(time_t t) {
  struct tm lt; localtime_r(&t, &lt);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &lt);
  return String(buf);
}

// ── Status JSON for the web UI ────────────────────────────────────────────────
static String statusJson() {
  JsonDocument doc;
  const char* s = state == State::IDLE         ? "idle"
                : state == State::DISPENSING    ? "dispensing"
                : state == State::WAIT_CONFIRM  ? "waiting"
                : state == State::FAULT         ? "fault"
                : "boot";
  doc["state"]       = s;
  doc["slot"]        = carousel.currentSlot();
  doc["slots"]       = CAROUSEL_SLOTS;
  doc["time"]        = isoLocal(time(nullptr));
  doc["rtc"]         = rtcOk;
  doc["wifi"]        = WiFi.status() == WL_CONNECTED;
  doc["ip"]          = WiFi.localIP().toString();
  doc["mqtt"]        = notifier.mqttConnected();
  doc["camUrl"]      = CAM_NODE_URL;
  doc["deviceId"]    = DEVICE_ID;

  Dose nd; time_t when;
  if (scheduler.nextDose(time(nullptr), nd, when)) {
    doc["nextLabel"] = nd.label;
    doc["nextTime"]  = isoLocal(when);
  }
  String out; serializeJson(doc, out);
  return out;
}

// ── Dispense one dose (rotate + verify drop) ──────────────────────────────────
static bool doDispense(const char* label) {
  sensors.arm();
  sensors.led(true);
  carousel.advanceOneSlot();
  storage.saveSlot(carousel.currentSlot());

  // Wait for the IR beam to confirm a pill actually fell.
  uint32_t t0 = millis();
  while (millis() - t0 < DROP_DETECT_TIMEOUT_MS) {
    sensors.update();
    if (sensors.dropped()) {
      notifier.event("due", label, /*withPhoto=*/true);
      return true;
    }
    esp_task_wdt_reset();
    delay(10);
  }
  // No drop detected — treat as jam/empty. Fail safe: stop and alert.
  notifier.event("jam", label, /*withPhoto=*/true);
  return false;
}

// ── Command handler (MQTT) ────────────────────────────────────────────────────
static void onCommand(const String& action, const String& data) {
  if (action == "dispense") {
    manualPending = true;
  } else if (action == "schedule" && data.length()) {
    scheduler.setJson(data);
  } else if (action == "home") {
    carousel.home();
  }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  esp_task_wdt_config_t wdt = { .timeout_ms = WDT_TIMEOUT_S * 1000,
                                .idle_core_mask = 0, .trigger_panic = true };
  esp_task_wdt_init(&wdt);
  esp_task_wdt_add(nullptr);

  if (!LittleFS.begin(true)) Serial.println("[fs] LittleFS mount failed");

  storage.begin();
  sensors.begin();
  carousel.begin(&sensors);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(250); esp_task_wdt_reset();
  }
  if (MDNS.begin(MDNS_HOSTNAME)) MDNS.addService("http", "tcp", WEB_PORT);

  syncClock();

  auth.begin(&storage, WEB_ADMIN_USER, WEB_ADMIN_PASS);
  scheduler.begin(&storage);
  scheduler.load();
  notifier.begin(&storage);
  notifier.setCommandHandler(onCommand);

  // Home the carousel so absolute slot position is known.
  if (HOME_ON_BOOT) {
    if (!carousel.home()) {
      state = State::FAULT;
      notifier.event("jam", "Baslangic (homing) hatasi", true);
    }
  } else {
    carousel.setSlot(storage.loadSlot());
  }

  WebHooks hooks;
  hooks.statusJson   = statusJson;
  hooks.scheduleJson = []() { String s; scheduler.getJson(s); return s; };
  hooks.setSchedule  = [](const String& j) { return scheduler.setJson(j); };
  hooks.dispenseNow  = []() { manualPending = true; return true; };
  hooks.jog          = [](int steps) {
    // Small manual nudge for alignment/testing. Positive = forward.
    if (steps == 0 || abs(steps) > STEPS_PER_REV) return false;
    // Reuse advanceOneSlot fractionally via direct step is out of scope here;
    // jog is implemented as whole-slot moves for safety.
    int slots = steps > 0 ? 1 : -1;
    if (slots > 0) carousel.advanceOneSlot();
    else carousel.setSlot(carousel.currentSlot() - 1);
    storage.saveSlot(carousel.currentSlot());
    return true;
  };
  hooks.home       = []() { return carousel.home(); };
  hooks.eventsJson = []() { return notifier.eventsJson(); };
  web.begin(&auth, hooks);

  if (state != State::FAULT) state = State::IDLE;
  notifier.event("boot", String("PillPilot acildi — ") + WiFi.localIP().toString(), false);
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void loop() {
  esp_task_wdt_reset();
  sensors.update();
  notifier.loop();

  time_t now = time(nullptr);

  switch (state) {
    case State::IDLE: {
      // Manual/test dispense first.
      if (manualPending) {
        manualPending = false;
        strlcpy(activeLabel, "Test", sizeof(activeLabel));
        state = doDispense(activeLabel) ? State::WAIT_CONFIRM : State::FAULT;
        waitStarted = now; lastRemind = now;
        break;
      }
      // Scheduled dose?
      int idx = scheduler.dueDose(now, DOSE_GRACE_MINUTES);
      if (idx >= 0) {
        activeDose = idx;
        strlcpy(activeLabel, scheduler.get(idx).label, sizeof(activeLabel));
        scheduler.markFired(idx, now);        // guard BEFORE moving (no re-fire)
        state = doDispense(activeLabel) ? State::WAIT_CONFIRM : State::FAULT;
        waitStarted = now; lastRemind = now;
      }
      break;
    }

    case State::WAIT_CONFIRM: {
      sensors.led((millis() / 500) % 2);      // blink while waiting
      if (sensors.takenPressed()) {
        sensors.led(false); sensors.buzz(false);
        notifier.event("taken", activeLabel, false);
        activeDose = -1;
        state = State::IDLE;
        break;
      }
      // Periodic reminder buzz + nudge.
      if (now - lastRemind >= (time_t)REMIND_EVERY_MINUTES * 60) {
        lastRemind = now;
        sensors.beep(300);
        notifier.telegram(String("⏰ ") + activeLabel + " bekliyor — henuz alinmadi");
      }
      // Grace window elapsed -> MISSED.
      if (now - waitStarted >= (time_t)DOSE_GRACE_MINUTES * 60) {
        sensors.led(false); sensors.buzz(false);
        notifier.event("missed", activeLabel, /*withPhoto=*/true);
        activeDose = -1;
        state = State::IDLE;
      }
      break;
    }

    case State::FAULT: {
      // Fail-safe: stop, alert every few minutes, wait for a human (re-home
      // via web/MQTT clears it). Do NOT keep rotating.
      static time_t lastFaultBeep = 0;
      sensors.led((millis() / 200) % 2);
      if (now - lastFaultBeep >= 300) {
        lastFaultBeep = now;
        sensors.beep(120);
      }
      if (sensors.takenPressed()) {           // ack: attempt recovery
        if (carousel.home()) { state = State::IDLE; sensors.led(false); }
      }
      break;
    }

    default:
      state = State::IDLE;
      break;
  }

  delay(5);
}
