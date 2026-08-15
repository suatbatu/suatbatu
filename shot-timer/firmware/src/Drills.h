// Named drills: a par schedule plus an expected round count.
//
// Kept separate from Settings, in its own NVS key with its own version, so
// that editing the drill format does not wipe a shooter's profiles and
// thresholds — and vice versa.
#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

#include "config.h"

struct Drill {
  char name[DRILL_NAME_LEN] = "Free";
  // 0 means "this drill imposes no par", in which case the timer falls back to
  // whatever the Par settings say. That is what makes the default "Free" drill
  // behave exactly as the timer did before drills existed.
  uint8_t parCount = 0;
  uint16_t parMs[MAX_PAR_TIMES] = {0, 0, 0, 0};
  // 0 means "unspecified" — the timer will not comment on the round count.
  uint8_t expectedShots = 0;
};

class Drills {
 public:
  void load();
  void save() const;
  void reset();  // re-seed the shipped set

  uint8_t active() const { return active_ < MAX_DRILLS ? active_ : 0; }
  void setActive(uint8_t i) { active_ = i < MAX_DRILLS ? i : 0; }
  void cycleActive(int8_t delta);

  Drill& at(uint8_t i) { return drills_[i < MAX_DRILLS ? i : 0]; }
  const Drill& at(uint8_t i) const { return drills_[i < MAX_DRILLS ? i : 0]; }
  Drill& current() { return at(active()); }
  const Drill& current() const { return at(active()); }

  void toJson(JsonObject out) const;
  bool applyJson(JsonObjectConst in);

 private:
  Drill drills_[MAX_DRILLS];
  uint8_t active_ = 0;
};

extern Drills drills;
