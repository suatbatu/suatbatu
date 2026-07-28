// Scheduler.h — dose schedule with RTC-based due detection and a fired-guard.
#pragma once
#include <Arduino.h>
#include "Storage.h"
#include "config.h"

struct Dose {
  uint8_t hour   = 8;
  uint8_t minute = 0;
  uint8_t dowMask = 0x7F;   // bit0=Sun .. bit6=Sat; 0x7F = every day
  bool    enabled = true;
  char    label[24] = "Doz";
};

class Scheduler {
public:
  void begin(Storage* storage);
  void load();

  // JSON get/set for the web UI and MQTT. setJson validates and persists.
  void getJson(String& out);
  bool setJson(const String& in);

  // Returns the index of a dose due now and not yet fired within its grace
  // window, else -1. Reboot-safe: an overdue-but-unfired dose still fires.
  int dueDose(time_t now, uint16_t graceMinutes);

  void markFired(int idx, time_t when);

  // For the UI: fill the soonest upcoming dose and its epoch. False if none.
  bool nextDose(time_t now, Dose& out, time_t& whenEpoch);

  int  count() const { return n_; }
  Dose get(int i) const { return (i >= 0 && i < n_) ? doses_[i] : Dose{}; }

private:
  time_t scheduledEpochToday(time_t now, const Dose& d);
  void   saveFired();
  void   loadFired();

  Storage* storage_ = nullptr;
  Dose     doses_[MAX_DOSES];
  int      n_ = 0;
  time_t   fired_[MAX_DOSES] = {0};
};
