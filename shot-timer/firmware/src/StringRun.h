// A "string" is one run: the start beep, every shot detected after it, and the
// numbers a shooter actually reads off a timer — first shot, splits, total.
#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

#include "config.h"

class StringRun {
 public:
  void begin(uint32_t id, int64_t beepAtUs);
  // Stamped at the start of the string. The name is stored alongside the index
  // so the history stays self-describing after a drill is renamed or replaced.
  void setDrill(uint8_t index, const char* name, uint8_t expectedShots);
  void addShot(int64_t atUs);
  void end();

  bool isOpen() const { return open_; }
  uint32_t id() const { return id_; }
  uint8_t count() const { return count_; }
  bool overflowed() const { return overflowed_; }
  uint8_t expectedShots() const { return expectedShots_; }
  // True only when the drill said how many rounds to expect and we got a
  // different number — the timer stays quiet when it has nothing to say.
  bool shotCountMismatch() const { return expectedShots_ > 0 && count_ != expectedShots_; }

  // Milliseconds from the start of the beep. Index is 0-based.
  uint32_t shotMs(uint8_t i) const { return i < count_ ? shots_[i] : 0; }
  uint32_t firstShotMs() const { return count_ ? shots_[0] : 0; }
  uint32_t totalMs() const { return count_ ? shots_[count_ - 1] : 0; }
  // Split into shot i, i.e. the gap between shot i-1 and shot i. Shot 0's
  // "split" is its draw time, which is what every commercial timer shows too.
  uint32_t splitMs(uint8_t i) const;
  uint32_t bestSplitMs() const;
  uint32_t worstSplitMs() const;

  int64_t beepAtUs() const { return beepAtUs_; }
  // Wall time since the beep, for the live display while a string is open.
  uint32_t elapsedMs(int64_t nowUs) const;

  void toJson(JsonObject out, bool includeShots = true) const;

 private:
  uint32_t id_ = 0;
  int64_t beepAtUs_ = 0;
  uint32_t shots_[MAX_SHOTS_PER_STRING] = {0};
  uint8_t count_ = 0;
  bool open_ = false;
  bool overflowed_ = false;
  uint8_t drillIndex_ = 0;
  uint8_t expectedShots_ = 0;
  char drillName_[DRILL_NAME_LEN] = "";
};
