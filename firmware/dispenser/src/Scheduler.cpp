#include "Scheduler.h"
#include <ArduinoJson.h>
#include <time.h>

void Scheduler::begin(Storage* storage) {
  storage_ = storage;
}

void Scheduler::load() {
  String json = storage_->loadScheduleJson();
  if (!setJson(json)) {
    // Seed a sane default if nothing valid is stored.
    n_ = 2;
    doses_[0] = Dose{8,  0, 0x7F, true, "Sabah"};
    doses_[1] = Dose{20, 0, 0x7F, true, "Aksam"};
    String out; getJson(out);
    storage_->saveScheduleJson(out);
  }
  loadFired();
}

void Scheduler::getJson(String& out) {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n_; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["hour"]    = doses_[i].hour;
    o["minute"]  = doses_[i].minute;
    o["dowMask"] = doses_[i].dowMask;
    o["enabled"] = doses_[i].enabled;
    o["label"]   = doses_[i].label;
  }
  serializeJson(doc, out);
}

bool Scheduler::setJson(const String& in) {
  JsonDocument doc;
  if (deserializeJson(doc, in) != DeserializationError::Ok) return false;
  if (!doc.is<JsonArray>()) return false;

  JsonArray arr = doc.as<JsonArray>();
  int i = 0;
  for (JsonObject o : arr) {
    if (i >= MAX_DOSES) break;
    Dose d;
    d.hour    = o["hour"]    | 8;
    d.minute  = o["minute"]  | 0;
    d.dowMask = o["dowMask"] | 0x7F;
    d.enabled = o["enabled"] | true;
    const char* lbl = o["label"] | "Doz";
    strlcpy(d.label, lbl, sizeof(d.label));
    if (d.hour > 23 || d.minute > 59) return false;   // reject invalid times
    doses_[i++] = d;
  }
  n_ = i;
  if (storage_) storage_->saveScheduleJson(in);
  return true;
}

time_t Scheduler::scheduledEpochToday(time_t now, const Dose& d) {
  struct tm lt;
  localtime_r(&now, &lt);
  lt.tm_hour = d.hour;
  lt.tm_min  = d.minute;
  lt.tm_sec  = 0;
  return mktime(&lt);
}

int Scheduler::dueDose(time_t now, uint16_t graceMinutes) {
  struct tm lt;
  localtime_r(&now, &lt);
  uint8_t todayBit = (1 << lt.tm_wday);      // tm_wday: 0=Sun

  for (int i = 0; i < n_; i++) {
    const Dose& d = doses_[i];
    if (!d.enabled) continue;
    if (!(d.dowMask & todayBit)) continue;

    time_t sched = scheduledEpochToday(now, d);
    time_t windowEnd = sched + (time_t)graceMinutes * 60;

    // Due if we're inside [sched, sched+grace] and this slot hasn't fired for
    // today's occurrence yet.
    if (now >= sched && now <= windowEnd && fired_[i] < sched) {
      return i;
    }
  }
  return -1;
}

void Scheduler::markFired(int idx, time_t when) {
  if (idx < 0 || idx >= MAX_DOSES) return;
  fired_[idx] = when;
  saveFired();
}

bool Scheduler::nextDose(time_t now, Dose& out, time_t& whenEpoch) {
  time_t best = 0;
  int bestIdx = -1;
  for (int day = 0; day < 8; day++) {          // search up to a week ahead
    time_t base = now + (time_t)day * 86400;
    struct tm lt; localtime_r(&base, &lt);
    uint8_t bit = (1 << lt.tm_wday);
    for (int i = 0; i < n_; i++) {
      if (!doses_[i].enabled || !(doses_[i].dowMask & bit)) continue;
      struct tm t = lt;
      t.tm_hour = doses_[i].hour; t.tm_min = doses_[i].minute; t.tm_sec = 0;
      time_t e = mktime(&t);
      if (e <= now) continue;
      if (bestIdx < 0 || e < best) { best = e; bestIdx = i; }
    }
    if (bestIdx >= 0) break;
  }
  if (bestIdx < 0) return false;
  out = doses_[bestIdx];
  whenEpoch = best;
  return true;
}

void Scheduler::saveFired() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n_; i++) arr.add((long)fired_[i]);
  String out; serializeJson(doc, out);
  storage_->saveFiredJson(out);
}

void Scheduler::loadFired() {
  String json = storage_->loadFiredJson();
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return;
  if (!doc.is<JsonArray>()) return;
  int i = 0;
  for (JsonVariant v : doc.as<JsonArray>()) {
    if (i >= MAX_DOSES) break;
    fired_[i++] = (time_t)v.as<long>();
  }
}
