// Everything a shooter tunes between strings, persisted to NVS.
#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

#include "config.h"

enum StartDelayMode : uint8_t {
  DELAY_RANDOM = 0,  // uniform in [delayMinMs, delayMaxMs] — the competition default
  DELAY_FIXED = 1,   // always delayFixedMs
  DELAY_INSTANT = 2  // beep on the button press, for drills where you self-start
};

struct Settings {
  // --- start signal ---
  StartDelayMode delayMode = DELAY_RANDOM;
  uint16_t delayMinMs = 1000;
  uint16_t delayMaxMs = 4000;
  uint16_t delayFixedMs = 3000;

  // --- buzzer ---
  uint16_t beepFreqHz = 2700;  // piezo sounders resonate around here; loudest per volt
  uint16_t beepMs = 300;
  uint8_t beepVolume = 10;  // 1..10, scales LEDC duty

  // --- detection ---
  uint8_t sensitivity = 5;  // 1 (only muzzle blast) .. 10 (dry fire, hand claps)
  uint16_t blankingMs = 60; // ignore window after a shot, kills echo double-counts
  int16_t micOffsetMs = 0;  // calibration: added to every shot time, see docs/DETECTION.md

  // --- par times ---
  bool parEnabled = false;
  uint8_t parCount = 1;
  uint16_t parMs[MAX_PAR_TIMES] = {3000, 0, 0, 0};

  // --- string handling ---
  uint16_t autoStopSec = 30;  // stop a string after this much silence; 0 = manual only
  bool autoSave = true;

  // --- network ---
  char wifiSsid[33] = "";
  char wifiPass[65] = "";
  char webUser[17] = "shooter";
  char webPass[33] = "";  // empty = web UI refuses to start, see Net.cpp

  void load();
  void save() const;
  void reset();

  void toJson(JsonObject out) const;
  // Applies only the keys present in `in`; returns false if a value was rejected.
  bool applyJson(JsonObjectConst in);

  // Detector thresholds derived from `sensitivity`.
  float triggerRatio() const;
  float absoluteFloor() const;

  uint32_t nextStartDelayMs() const;
};

extern Settings settings;
