#include "Drills.h"

#include <Preferences.h>

#include <algorithm>

Drills drills;

namespace {
Preferences prefs;
constexpr char NS[] = "shottimer";
constexpr char KEY_BLOB[] = "drills";
constexpr uint32_t BLOB_VERSION = 1;

struct Blob {
  uint32_t version;
  uint8_t active;
  Drill drills[MAX_DRILLS];
};

void copyStr(char* dst, size_t cap, const char* src) {
  if (!src) return;
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

template <typename T>
T clampTo(T v, T lo, T hi) {
  return std::min(std::max(v, lo), hi);
}

// The shipped set. Par times are the conventional goals for each drill at a
// competent-but-not-elite level, which is what makes them useful as a target
// rather than as a formality — adjust them to your own standard.
struct Seed {
  const char* name;
  uint8_t parCount;
  uint16_t par0;
  uint8_t shots;
};
const Seed SEEDS[] = {
    // "Free" must stay first and stay par-less: it is the default, and it is
    // what makes the timer behave exactly as it did before drills existed.
    {"Free", 0, 0, 0},
    {"Bill Drill", 1, 2000, 6},     // 6 shots, one target, from the holster
    {"El Presidente", 1, 10000, 12}, // 6 / reload / 6 across three targets
    {"Doubles", 1, 1500, 2},        // two shots, one sight picture
    {"Failure Drill", 1, 2500, 3},  // two body, one head
    {"1-Reload-1", 1, 4000, 2},     // the reload is the drill
};
constexpr uint8_t SEED_COUNT = sizeof(SEEDS) / sizeof(SEEDS[0]);

}  // namespace

void Drills::reset() {
  for (uint8_t i = 0; i < MAX_DRILLS; i++) {
    drills_[i] = Drill();
    if (i < SEED_COUNT) {
      copyStr(drills_[i].name, DRILL_NAME_LEN, SEEDS[i].name);
      drills_[i].parCount = SEEDS[i].parCount;
      drills_[i].parMs[0] = SEEDS[i].par0;
      drills_[i].expectedShots = SEEDS[i].shots;
    } else {
      // Spare slots, named so they are obviously free rather than broken.
      snprintf(drills_[i].name, DRILL_NAME_LEN, "Custom %u", i - SEED_COUNT + 1);
    }
  }
  active_ = 0;
}

void Drills::load() {
  prefs.begin(NS, true);
  Blob blob{};
  const size_t got = prefs.getBytes(KEY_BLOB, &blob, sizeof(blob));
  prefs.end();

  if (got == sizeof(blob) && blob.version == BLOB_VERSION) {
    active_ = blob.active < MAX_DRILLS ? blob.active : 0;
    for (uint8_t i = 0; i < MAX_DRILLS; i++) drills_[i] = blob.drills[i];
  } else {
    reset();
    save();
  }
}

void Drills::save() const {
  Blob blob{};
  blob.version = BLOB_VERSION;
  blob.active = active_;
  for (uint8_t i = 0; i < MAX_DRILLS; i++) blob.drills[i] = drills_[i];
  prefs.begin(NS, false);
  prefs.putBytes(KEY_BLOB, &blob, sizeof(blob));
  prefs.end();
}

void Drills::cycleActive(int8_t delta) {
  const int next = (static_cast<int>(active()) + MAX_DRILLS + (delta > 0 ? 1 : -1)) % MAX_DRILLS;
  active_ = static_cast<uint8_t>(next);
}

void Drills::toJson(JsonObject out) const {
  out["activeDrill"] = active();
  JsonArray arr = out["drills"].to<JsonArray>();
  for (uint8_t i = 0; i < MAX_DRILLS; i++) {
    JsonObject d = arr.add<JsonObject>();
    d["name"] = drills_[i].name;
    d["parCount"] = drills_[i].parCount;
    JsonArray par = d["parMs"].to<JsonArray>();
    for (uint8_t p = 0; p < MAX_PAR_TIMES; p++) par.add(drills_[i].parMs[p]);
    d["expectedShots"] = drills_[i].expectedShots;
  }
}

bool Drills::applyJson(JsonObjectConst in) {
  bool ok = true;

  if (in["activeDrill"].is<uint8_t>()) {
    const uint8_t i = in["activeDrill"];
    if (i < MAX_DRILLS)
      active_ = i;
    else
      ok = false;
  }

  // Addressed by index, like profiles, so a client can edit one drill without
  // shipping the whole set back and racing the device menu.
  if (in["drill"].is<JsonObjectConst>()) {
    JsonObjectConst d = in["drill"];
    const uint8_t i = d["index"] | active();
    if (i >= MAX_DRILLS) {
      ok = false;
    } else {
      Drill& t = drills_[i];
      if (d["name"].is<const char*>()) copyStr(t.name, DRILL_NAME_LEN, d["name"]);
      if (d["expectedShots"].is<uint8_t>())
        t.expectedShots = clampTo<uint8_t>(d["expectedShots"], 0, MAX_SHOTS_PER_STRING);
      if (d["parMs"].is<JsonArrayConst>()) {
        JsonArrayConst arr = d["parMs"];
        uint8_t n = 0;
        for (JsonVariantConst v : arr) {
          if (n >= MAX_PAR_TIMES) break;
          t.parMs[n++] = clampTo<uint16_t>(v.as<uint16_t>(), 0, 60000);
        }
        for (uint8_t p = n; p < MAX_PAR_TIMES; p++) t.parMs[p] = 0;
      }
      if (d["parCount"].is<uint8_t>()) {
        t.parCount = clampTo<uint8_t>(d["parCount"], 0, MAX_PAR_TIMES);
      } else if (d["parMs"].is<JsonArrayConst>()) {
        // Deriving the count from the times saves every client from having to
        // keep the two in step.
        uint8_t used = 0;
        for (uint8_t p = 0; p < MAX_PAR_TIMES; p++) {
          if (t.parMs[p] > 0) used = p + 1;
        }
        t.parCount = used;
      }
    }
  }

  return ok;
}
