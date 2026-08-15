#include "Settings.h"

#include <Preferences.h>
#include <esp_random.h>
#include <math.h>

#include <algorithm>

#include "Tdoa.h"

// Optional. Lets a fresh device come up with a web password already set, since
// there is no way to type one in on three buttons and the web UI refuses to
// start without one. See docs/API.md and include/secrets.example.h.
#if __has_include("secrets.h")
#include "secrets.h"
#endif

Settings settings;

namespace {
Preferences prefs;
constexpr char NS[] = "shottimer";
constexpr char KEY_BLOB[] = "cfg";
// Bump when the struct layout changes so old blobs are discarded instead of
// being reinterpreted as garbage.
constexpr uint32_t BLOB_VERSION = 3;

struct Blob {
  uint32_t version;
  Settings s;
};

template <typename T>
T clampTo(T v, T lo, T hi) {
  return std::min(std::max(v, lo), hi);
}

void copyStr(char* dst, size_t cap, const char* src) {
  if (!src) return;
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

// The shipped profiles are starting points, not gospel — the numbers come from
// the physics of what each source sounds like, and docs/DETECTION.md explains
// how to correct them with a level meter and a real gun.
void seedProfiles(Profile* p) {
  struct Seed {
    const char* name;
    uint8_t sensitivity;
    uint16_t refractoryMs;
    uint8_t echoRejectDb;
    uint16_t echoDecayMs;
  };
  static const Seed seeds[MAX_PROFILES] = {
      {"Pistol", 5, 25, 10, 120},      // centrefire handgun, outdoor bay
      {"Rifle", 4, 20, 10, 140},       // louder still, and echoes harder
      {"Suppressed", 8, 25, 8, 100},   // no muzzle blast, just the action
      {"Rimfire", 8, 20, 8, 100},      // quiet, sharp, fast
      {"Airsoft/CO2", 9, 20, 6, 80},   // barely above room noise
      {"Dry fire", 10, 30, 4, 60},     // a trigger click at a metre
  };
  for (uint8_t i = 0; i < MAX_PROFILES; i++) {
    p[i] = Profile();
    copyStr(p[i].name, PROFILE_NAME_LEN, seeds[i].name);
    p[i].sensitivity = seeds[i].sensitivity;
    p[i].refractoryMs = seeds[i].refractoryMs;
    p[i].echoRejectDb = seeds[i].echoRejectDb;
    p[i].echoDecayMs = seeds[i].echoDecayMs;
  }
}
}  // namespace

void Settings::load() {
  prefs.begin(NS, true);
  Blob blob{};
  size_t got = prefs.getBytes(KEY_BLOB, &blob, sizeof(blob));
  prefs.end();

  const bool fresh = (got != sizeof(blob) || blob.version != BLOB_VERSION);
  if (!fresh) {
    *this = blob.s;
  } else {
    reset();
  }

  // Bootstrap credentials seed an unconfigured device only. Anything set from
  // the web UI afterwards wins, so rebuilding with secrets.h still in place
  // does not silently reset a password you changed.
#ifdef BOOTSTRAP_WEB_PASS
  if (webPass[0] == '\0') {
    copyStr(webPass, sizeof(webPass), BOOTSTRAP_WEB_PASS);
#ifdef BOOTSTRAP_WEB_USER
    copyStr(webUser, sizeof(webUser), BOOTSTRAP_WEB_USER);
#endif
    save();
  }
#endif
#ifdef BOOTSTRAP_WIFI_SSID
  if (wifiSsid[0] == '\0') {
    copyStr(wifiSsid, sizeof(wifiSsid), BOOTSTRAP_WIFI_SSID);
    copyStr(wifiPass, sizeof(wifiPass), BOOTSTRAP_WIFI_PASS);
    save();
  }
#endif

  if (fresh) save();
}

void Settings::save() const {
  Blob blob{BLOB_VERSION, *this};
  prefs.begin(NS, false);
  prefs.putBytes(KEY_BLOB, &blob, sizeof(blob));
  prefs.end();
}

void Settings::reset() {
  *this = Settings();
  seedProfiles(profiles);
}

// Sensitivity is a single knob over two thresholds: how far above the running
// noise floor a transient has to rise, and an absolute floor that stops the
// detector from chasing its own noise in a silent room.
float Settings::triggerRatio() const {
  // 1 -> 40x over the floor (muzzle blast only), 10 -> 4x (a dry-fire click).
  const float s = clampTo<float>(profile().sensitivity, 1.0f, 10.0f);
  return 40.0f - (s - 1.0f) * 4.0f;
}

float Settings::absoluteFloor() const {
  // 24-bit samples, so full scale is 8388608. A gunshot clips the mic outright;
  // a dry-fire click at 1 m is four to five orders of magnitude quieter.
  static const float table[10] = {
      600000.0f, 350000.0f, 200000.0f, 110000.0f, 60000.0f,
      33000.0f,  18000.0f,  10000.0f,  5500.0f,   3000.0f,
  };
  return table[clampTo<int>(profile().sensitivity, 1, 10) - 1];
}

int Settings::maxLagSamples() const {
  // Single source of truth with the detector, and host-tested — see
  // src/Tdoa.h and tools/test_tdoa.cpp.
  return maxLagForAngle(profile().maxOffAxisDeg, micSpacingMm / 1000.0f, AUDIO_SAMPLE_RATE,
                        SPEED_OF_SOUND_MPS, TDOA_MAX_LAG);
}

uint32_t Settings::nextStartDelayMs() const {
  switch (delayMode) {
    case DELAY_INSTANT:
      return 0;
    case DELAY_FIXED:
      return delayFixedMs;
    case DELAY_RANDOM:
    default: {
      const uint32_t lo = std::min(delayMinMs, delayMaxMs);
      const uint32_t hi = std::max(delayMinMs, delayMaxMs);
      if (hi <= lo) return lo;
      // esp_random() is the hardware RNG — the delay must not be predictable,
      // or the shooter learns to anticipate the beep and the times are fiction.
      return lo + (esp_random() % (hi - lo + 1));
    }
  }
}

void Settings::toJson(JsonObject out) const {
  out["activeProfile"] = activeProfile;
  JsonArray ps = out["profiles"].to<JsonArray>();
  for (uint8_t i = 0; i < MAX_PROFILES; i++) {
    JsonObject p = ps.add<JsonObject>();
    p["name"] = profiles[i].name;
    p["sensitivity"] = profiles[i].sensitivity;
    p["refractoryMs"] = profiles[i].refractoryMs;
    p["echoRejectDb"] = profiles[i].echoRejectDb;
    p["echoDecayMs"] = profiles[i].echoDecayMs;
    p["directionGate"] = profiles[i].directionGate;
    p["maxOffAxisDeg"] = profiles[i].maxOffAxisDeg;
  }

  out["delayMode"] = static_cast<uint8_t>(delayMode);
  out["delayMinMs"] = delayMinMs;
  out["delayMaxMs"] = delayMaxMs;
  out["delayFixedMs"] = delayFixedMs;
  out["beepFreqHz"] = beepFreqHz;
  out["beepMs"] = beepMs;
  out["beepVolume"] = beepVolume;
  out["parEnabled"] = parEnabled;
  out["parCount"] = parCount;
  JsonArray par = out["parMs"].to<JsonArray>();
  for (uint8_t i = 0; i < MAX_PAR_TIMES; i++) par.add(parMs[i]);
  out["micSpacingMm"] = micSpacingMm;
  out["micOffsetMs"] = micOffsetMs;
  out["displayFlipped"] = displayFlipped;
  out["displayContrast"] = displayContrast;
  out["autoDim"] = autoDim;
  out["autoStopSec"] = autoStopSec;
  out["autoSave"] = autoSave;
  out["bleEnabled"] = bleEnabled;
  out["bleName"] = bleName;
  out["wifiSsid"] = wifiSsid;
  out["webUser"] = webUser;
  // Passwords are write-only over the API; report whether one is set instead.
  out["wifiPassSet"] = wifiPass[0] != '\0';
  out["webPassSet"] = webPass[0] != '\0';
  out["maxLagSamples"] = maxLagSamples();
  out["version"] = FW_VERSION;
}

bool Settings::applyJson(JsonObjectConst in) {
  bool ok = true;

  if (in["activeProfile"].is<uint8_t>()) {
    const uint8_t idx = in["activeProfile"];
    if (idx < MAX_PROFILES)
      activeProfile = idx;
    else
      ok = false;
  }

  // Profiles are addressed by index so a client can PATCH exactly one without
  // shipping the whole array back and racing the on-device menu.
  if (in["profile"].is<JsonObjectConst>()) {
    JsonObjectConst p = in["profile"];
    const uint8_t idx = p["index"] | activeProfile;
    if (idx >= MAX_PROFILES) {
      ok = false;
    } else {
      Profile& t = profiles[idx];
      if (p["name"].is<const char*>()) copyStr(t.name, PROFILE_NAME_LEN, p["name"]);
      if (p["sensitivity"].is<uint8_t>())
        t.sensitivity = clampTo<uint8_t>(p["sensitivity"], 1, 10);
      if (p["refractoryMs"].is<uint16_t>())
        t.refractoryMs = clampTo<uint16_t>(p["refractoryMs"], 10, 500);
      if (p["echoRejectDb"].is<uint8_t>())
        t.echoRejectDb = clampTo<uint8_t>(p["echoRejectDb"], 0, 40);
      if (p["echoDecayMs"].is<uint16_t>())
        t.echoDecayMs = clampTo<uint16_t>(p["echoDecayMs"], 10, 1000);
      if (p["directionGate"].is<bool>()) t.directionGate = p["directionGate"];
      if (p["maxOffAxisDeg"].is<uint8_t>())
        t.maxOffAxisDeg = clampTo<uint8_t>(p["maxOffAxisDeg"], 5, 89);
    }
  }

  if (in["delayMode"].is<uint8_t>()) {
    const uint8_t m = in["delayMode"];
    if (m <= DELAY_INSTANT)
      delayMode = static_cast<StartDelayMode>(m);
    else
      ok = false;
  }
  if (in["delayMinMs"].is<uint16_t>()) delayMinMs = clampTo<uint16_t>(in["delayMinMs"], 0, 30000);
  if (in["delayMaxMs"].is<uint16_t>()) delayMaxMs = clampTo<uint16_t>(in["delayMaxMs"], 0, 30000);
  if (delayMaxMs < delayMinMs) std::swap(delayMinMs, delayMaxMs);
  if (in["delayFixedMs"].is<uint16_t>())
    delayFixedMs = clampTo<uint16_t>(in["delayFixedMs"], 0, 30000);

  if (in["beepFreqHz"].is<uint16_t>()) beepFreqHz = clampTo<uint16_t>(in["beepFreqHz"], 500, 6000);
  if (in["beepMs"].is<uint16_t>()) beepMs = clampTo<uint16_t>(in["beepMs"], 50, 2000);
  if (in["beepVolume"].is<uint8_t>()) beepVolume = clampTo<uint8_t>(in["beepVolume"], 1, 10);

  if (in["parEnabled"].is<bool>()) parEnabled = in["parEnabled"];
  if (in["parCount"].is<uint8_t>()) parCount = clampTo<uint8_t>(in["parCount"], 1, MAX_PAR_TIMES);
  if (in["parMs"].is<JsonArrayConst>()) {
    JsonArrayConst arr = in["parMs"];
    uint8_t i = 0;
    for (JsonVariantConst v : arr) {
      if (i >= MAX_PAR_TIMES) break;
      parMs[i++] = clampTo<uint16_t>(v.as<uint16_t>(), 0, 60000);
    }
  }

  if (in["micSpacingMm"].is<uint16_t>())
    micSpacingMm = clampTo<uint16_t>(in["micSpacingMm"], 20, 400);
  if (in["micOffsetMs"].is<int16_t>()) micOffsetMs = clampTo<int16_t>(in["micOffsetMs"], -200, 200);

  if (in["displayFlipped"].is<bool>()) displayFlipped = in["displayFlipped"];
  if (in["displayContrast"].is<uint8_t>()) displayContrast = in["displayContrast"];
  if (in["autoDim"].is<bool>()) autoDim = in["autoDim"];

  if (in["autoStopSec"].is<uint16_t>())
    autoStopSec = clampTo<uint16_t>(in["autoStopSec"], 0, 600);
  if (in["autoSave"].is<bool>()) autoSave = in["autoSave"];

  if (in["bleEnabled"].is<bool>()) bleEnabled = in["bleEnabled"];
  if (in["bleName"].is<const char*>()) {
    const char* n = in["bleName"];
    // Refuse a name no client will match rather than accept it and leave the
    // owner wondering why PractiScore cannot see the timer.
    String upper(n);
    upper.toUpperCase();
    if (upper.startsWith("COMMANDER") || upper.startsWith("AMG LAB COMM"))
      copyStr(bleName, sizeof(bleName), n);
    else
      ok = false;
  }

  if (in["wifiSsid"].is<const char*>()) copyStr(wifiSsid, sizeof(wifiSsid), in["wifiSsid"]);
  if (in["wifiPass"].is<const char*>()) copyStr(wifiPass, sizeof(wifiPass), in["wifiPass"]);
  if (in["webUser"].is<const char*>()) copyStr(webUser, sizeof(webUser), in["webUser"]);
  if (in["webPass"].is<const char*>()) {
    const char* p = in["webPass"];
    // Refuse to set a web password so short it is not worth having.
    if (strlen(p) >= 8 || strlen(p) == 0)
      copyStr(webPass, sizeof(webPass), p);
    else
      ok = false;
  }

  return ok;
}
