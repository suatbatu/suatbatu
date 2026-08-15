#include "StringRun.h"

#include <algorithm>

#include "Settings.h"

void StringRun::begin(uint32_t id, int64_t beepAtUs) {
  id_ = id;
  beepAtUs_ = beepAtUs;
  count_ = 0;
  open_ = true;
  overflowed_ = false;
}

void StringRun::addShot(int64_t atUs) {
  if (!open_) return;
  if (count_ >= MAX_SHOTS_PER_STRING) {
    overflowed_ = true;
    return;
  }
  // The detector can hand us a shot captured microseconds before the beep was
  // scheduled (a startle shot, or a loud noise inside the DMA block that
  // straddles t=0). Clamp instead of wrapping the unsigned subtraction.
  int64_t deltaUs = atUs - beepAtUs_;
  if (deltaUs < 0) deltaUs = 0;

  int32_t ms = static_cast<int32_t>((deltaUs + 500) / 1000) + settings.micOffsetMs;
  if (ms < 0) ms = 0;
  shots_[count_++] = static_cast<uint32_t>(ms);
}

void StringRun::end() { open_ = false; }

uint32_t StringRun::elapsedMs(int64_t nowUs) const {
  const int64_t d = nowUs - beepAtUs_;
  return d < 0 ? 0 : static_cast<uint32_t>(d / 1000);
}

uint32_t StringRun::splitMs(uint8_t i) const {
  if (i >= count_) return 0;
  return i == 0 ? shots_[0] : shots_[i] - shots_[i - 1];
}

uint32_t StringRun::bestSplitMs() const {
  // Shot 0 is a draw, not a split — comparing it against shot-to-shot splits
  // would make every string's "best split" the draw on a one-shot string.
  if (count_ < 2) return 0;
  uint32_t best = UINT32_MAX;
  for (uint8_t i = 1; i < count_; i++) best = std::min(best, splitMs(i));
  return best;
}

uint32_t StringRun::worstSplitMs() const {
  if (count_ < 2) return 0;
  uint32_t worst = 0;
  for (uint8_t i = 1; i < count_; i++) worst = std::max(worst, splitMs(i));
  return worst;
}

void StringRun::toJson(JsonObject out, bool includeShots) const {
  out["id"] = id_;
  out["count"] = count_;
  out["firstMs"] = firstShotMs();
  out["totalMs"] = totalMs();
  out["bestSplitMs"] = bestSplitMs();
  out["worstSplitMs"] = worstSplitMs();
  if (overflowed_) out["overflowed"] = true;
  if (includeShots) {
    JsonArray arr = out["shots"].to<JsonArray>();
    for (uint8_t i = 0; i < count_; i++) arr.add(shots_[i]);
  }
}
