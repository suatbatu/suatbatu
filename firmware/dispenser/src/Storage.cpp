#include "Storage.h"

// A single NVS namespace holds all keys. Keys are short (NVS limit: 15 chars).
static const char* NS = "pillpilot";

void Storage::begin() {
  // Open once to ensure the namespace exists; individual ops reopen as needed.
  prefs_.begin(NS, false);
  prefs_.end();
}

String Storage::loadScheduleJson() {
  prefs_.begin(NS, true);
  String v = prefs_.getString("sched", "[]");
  prefs_.end();
  return v;
}

void Storage::saveScheduleJson(const String& json) {
  prefs_.begin(NS, false);
  prefs_.putString("sched", json);
  prefs_.end();
}

String Storage::loadFiredJson() {
  prefs_.begin(NS, true);
  String v = prefs_.getString("fired", "[]");
  prefs_.end();
  return v;
}

void Storage::saveFiredJson(const String& json) {
  prefs_.begin(NS, false);
  prefs_.putString("fired", json);
  prefs_.end();
}

int Storage::loadSlot() {
  prefs_.begin(NS, true);
  int v = prefs_.getInt("slot", 0);
  prefs_.end();
  return v;
}

void Storage::saveSlot(int slot) {
  prefs_.begin(NS, false);
  prefs_.putInt("slot", slot);
  prefs_.end();
}

bool Storage::hasAuth() {
  prefs_.begin(NS, true);
  bool has = prefs_.isKey("auth_hash");
  prefs_.end();
  return has;
}

bool Storage::loadAuth(String& user, String& saltHex, String& hashHex) {
  prefs_.begin(NS, true);
  if (!prefs_.isKey("auth_hash")) { prefs_.end(); return false; }
  user    = prefs_.getString("auth_user", "");
  saltHex = prefs_.getString("auth_salt", "");
  hashHex = prefs_.getString("auth_hash", "");
  prefs_.end();
  return true;
}

void Storage::saveAuth(const String& user, const String& saltHex, const String& hashHex) {
  prefs_.begin(NS, false);
  prefs_.putString("auth_user", user);
  prefs_.putString("auth_salt", saltHex);
  prefs_.putString("auth_hash", hashHex);
  prefs_.end();
}

String Storage::loadEventsJson() {
  prefs_.begin(NS, true);
  String v = prefs_.getString("events", "[]");
  prefs_.end();
  return v;
}

void Storage::saveEventsJson(const String& json) {
  prefs_.begin(NS, false);
  prefs_.putString("events", json);
  prefs_.end();
}

long Storage::loadTgOffset() {
  prefs_.begin(NS, true);
  long v = (long)prefs_.getLong("tg_offset", 0);
  prefs_.end();
  return v;
}

void Storage::saveTgOffset(long offset) {
  prefs_.begin(NS, false);
  prefs_.putLong("tg_offset", (int32_t)offset);
  prefs_.end();
}
