// Everything a shooter tunes, persisted to NVS.
//
// Detection settings live in a *profile*, not at top level: a suppressed .22
// and an unsuppressed rifle need different thresholds, and retuning four
// numbers every time you change guns is how a timer ends up permanently set to
// whatever worked last month.
#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

#include "ShotMerge.h"
#include "config.h"

enum StartDelayMode : uint8_t {
  DELAY_RANDOM = 0,  // uniform in [delayMinMs, delayMaxMs] — the competition default
  DELAY_FIXED = 1,   // always delayFixedMs
  DELAY_INSTANT = 2  // beep on the button press, for drills where you self-start
};

// One firearm, or one scenario.
struct Profile {
  char name[PROFILE_NAME_LEN] = "Pistol";

  uint8_t sensitivity = 5;  // 1 (only muzzle blast) .. 10 (dry fire, hand claps)

  // Echo rejection. After a shot, a decaying guard sits above the normal
  // threshold: a reflection of that shot is well below its peak and is
  // swallowed, while a genuine next shot at comparable level gets through.
  uint16_t refractoryMs = 25;  // hard floor, set by physical rate of fire
  uint8_t echoRejectDb = 10;   // guard starts this far below the shot's peak
  uint16_t echoDecayMs = 120;  // and decays with this time constant

  // Direction gate. With two microphones, reject anything that did not come
  // from the shooter's axis — this is what keeps the next bay out of your
  // splits. Ignored when only one microphone is fitted.
  bool directionGate = false;
  uint8_t maxOffAxisDeg = 25;

  // Which sensor decides a shot happened. Impulse mode needs a LIS3DH coupled
  // to the firearm; see docs/DETECTION.md before selecting it.
  ShotSource shotSource = ShotSource::Acoustic;
  uint8_t impulseThreshold = 8;  // 1 (slide slam only) .. 10 (trigger break)
};

struct Settings {
  // --- profiles ---
  Profile profiles[MAX_PROFILES];
  uint8_t activeProfile = 0;

  // --- start signal ---
  StartDelayMode delayMode = DELAY_RANDOM;
  uint16_t delayMinMs = 1000;
  uint16_t delayMaxMs = 4000;
  uint16_t delayFixedMs = 3000;

  // --- buzzer ---
  uint16_t beepFreqHz = 2700;  // piezo sounders resonate around here; loudest per volt
  uint16_t beepMs = 300;
  uint8_t beepVolume = 10;  // 1..10, scales LEDC duty

  // --- par times ---
  bool parEnabled = false;
  uint8_t parCount = 1;
  uint16_t parMs[MAX_PAR_TIMES] = {3000, 0, 0, 0};

  // --- microphone array (a property of how you built it) ---
  uint16_t micSpacingMm = 120;
  int16_t micOffsetMs = 0;  // calibration, see docs/DETECTION.md

  // --- display ---
  bool displayFlipped = false;  // 180 degrees, for a top-of-belt mount
  uint8_t displayContrast = 255;
  bool autoDim = true;  // dim while idle to save the panel and the battery

  // --- string handling ---
  uint16_t autoStopSec = 30;  // stop a string after this much silence; 0 = manual only
  bool autoSave = true;

  // --- bluetooth ---
  // Off by default: Wi-Fi and BLE share one radio on the S3, and the timer
  // should not pay a coexistence cost nobody has measured yet.
  bool bleEnabled = false;
  // Must start with "COMMANDER" or "AMG LAB COMM" for name-filtering clients
  // (PractiScore and friends) to find it. See docs/BLE_PROTOCOL.md.
  char bleName[25] = "Commander shot-timer";

  // --- network ---
  char wifiSsid[33] = "";
  char wifiPass[65] = "";
  char webUser[17] = "shooter";
  char webPass[33] = "";  // empty = web UI refuses to start, see WebInterface.cpp

  void load();
  void save() const;
  void reset();

  Profile& profile() { return profiles[activeProfile < MAX_PROFILES ? activeProfile : 0]; }
  const Profile& profile() const {
    return profiles[activeProfile < MAX_PROFILES ? activeProfile : 0];
  }

  void toJson(JsonObject out) const;
  // Applies only the keys present in `in`; returns false if a value was rejected.
  bool applyJson(JsonObjectConst in);

  // Detector thresholds derived from the active profile's sensitivity.
  float triggerRatio() const;
  float absoluteFloor() const;
  // Largest inter-microphone lag, in samples, that still counts as "in front".
  int maxLagSamples() const;

  uint32_t nextStartDelayMs() const;
};

extern Settings settings;
