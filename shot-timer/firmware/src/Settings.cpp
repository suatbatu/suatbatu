#include "Settings.h"

#include <Preferences.h>
#include <esp_random.h>

#include <algorithm>

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
constexpr uint32_t BLOB_VERSION = 1;

struct Blob {
  uint32_t version;
  Settings s;
};

template <typename T>
T clampTo(T v, T lo, T hi) {
  return std::min(std::max(v, lo), hi);
}
}  // namespace

void Settings::load() {
  prefs.begin(NS, true);
  Blob blob{};
  size_t got = prefs.getBytes(KEY_BLOB, &blob, sizeof(blob));
  prefs.end();

  if (got == sizeof(blob) && blob.version == BLOB_VERSION) {
    *this = blob.s;
  } else {
    reset();
  }

  // Bootstrap credentials seed an unconfigured device only. Anything set from
  // the web UI afterwards wins, so rebuilding with secrets.h still in place
  // does not silently reset a password you changed.
#ifdef BOOTSTRAP_WEB_PASS
  if (webPass[0] == '\0') {
    strncpy(webPass, BOOTSTRAP_WEB_PASS, sizeof(webPass) - 1);
    webPass[sizeof(webPass) - 1] = '\0';
#ifdef BOOTSTRAP_WEB_USER
    strncpy(webUser, BOOTSTRAP_WEB_USER, sizeof(webUser) - 1);
    webUser[sizeof(webUser) - 1] = '\0';
#endif
    save();
  }
#endif
#ifdef BOOTSTRAP_WIFI_SSID
  if (wifiSsid[0] == '\0') {
    strncpy(wifiSsid, BOOTSTRAP_WIFI_SSID, sizeof(wifiSsid) - 1);
    strncpy(wifiPass, BOOTSTRAP_WIFI_PASS, sizeof(wifiPass) - 1);
    wifiSsid[sizeof(wifiSsid) - 1] = '\0';
    wifiPass[sizeof(wifiPass) - 1] = '\0';
    save();
  }
#endif

  if (got != sizeof(blob) || blob.version != BLOB_VERSION) save();
}

void Settings::save() const {
  Blob blob{BLOB_VERSION, *this};
  prefs.begin(NS, false);
  prefs.putBytes(KEY_BLOB, &blob, sizeof(blob));
  prefs.end();
}

void Settings::reset() { *this = Settings(); }

// Sensitivity is a single knob over two thresholds: how far above the running
// noise floor a transient has to rise, and an absolute floor that stops the
// detector from chasing its own noise in a silent room.
float Settings::triggerRatio() const {
  // 1 -> 40x over the floor (muzzle blast only), 10 -> 4x (a dry-fire click).
  const float s = clampTo<float>(sensitivity, 1.0f, 10.0f);
  return 40.0f - (s - 1.0f) * 4.0f;
}

float Settings::absoluteFloor() const {
  // 24-bit samples, so full scale is 8388608. A gunshot clips the mic outright;
  // a dry-fire click at 1 m is four to five orders of magnitude quieter.
  static const float table[10] = {
      600000.0f, 350000.0f, 200000.0f, 110000.0f, 60000.0f,
      33000.0f,  18000.0f,  10000.0f,  5500.0f,   3000.0f,
  };
  return table[clampTo<int>(sensitivity, 1, 10) - 1];
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
  out["delayMode"] = static_cast<uint8_t>(delayMode);
  out["delayMinMs"] = delayMinMs;
  out["delayMaxMs"] = delayMaxMs;
  out["delayFixedMs"] = delayFixedMs;
  out["beepFreqHz"] = beepFreqHz;
  out["beepMs"] = beepMs;
  out["beepVolume"] = beepVolume;
  out["sensitivity"] = sensitivity;
  out["blankingMs"] = blankingMs;
  out["micOffsetMs"] = micOffsetMs;
  out["parEnabled"] = parEnabled;
  out["parCount"] = parCount;
  JsonArray par = out["parMs"].to<JsonArray>();
  for (uint8_t i = 0; i < MAX_PAR_TIMES; i++) par.add(parMs[i]);
  out["autoStopSec"] = autoStopSec;
  out["autoSave"] = autoSave;
  out["wifiSsid"] = wifiSsid;
  out["webUser"] = webUser;
  // Passwords are write-only over the API; report whether one is set instead.
  out["wifiPassSet"] = wifiPass[0] != '\0';
  out["webPassSet"] = webPass[0] != '\0';
  out["version"] = FW_VERSION;
}

namespace {
void copyStr(char* dst, size_t cap, const char* src) {
  if (!src) return;
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}
}  // namespace

bool Settings::applyJson(JsonObjectConst in) {
  bool ok = true;

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

  if (in["sensitivity"].is<uint8_t>()) sensitivity = clampTo<uint8_t>(in["sensitivity"], 1, 10);
  if (in["blankingMs"].is<uint16_t>()) blankingMs = clampTo<uint16_t>(in["blankingMs"], 20, 500);
  if (in["micOffsetMs"].is<int16_t>()) micOffsetMs = clampTo<int16_t>(in["micOffsetMs"], -200, 200);

  if (in["parEnabled"].is<bool>()) parEnabled = in["parEnabled"];
  if (in["parCount"].is<uint8_t>())
    parCount = clampTo<uint8_t>(in["parCount"], 1, MAX_PAR_TIMES);
  if (in["parMs"].is<JsonArrayConst>()) {
    JsonArrayConst arr = in["parMs"];
    uint8_t i = 0;
    for (JsonVariantConst v : arr) {
      if (i >= MAX_PAR_TIMES) break;
      parMs[i++] = clampTo<uint16_t>(v.as<uint16_t>(), 0, 60000);
    }
  }

  if (in["autoStopSec"].is<uint16_t>())
    autoStopSec = clampTo<uint16_t>(in["autoStopSec"], 0, 600);
  if (in["autoSave"].is<bool>()) autoSave = in["autoSave"];

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
