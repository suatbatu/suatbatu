// Storage.h — thin wrapper over NVS (Preferences) for durable state.
#pragma once
#include <Arduino.h>
#include <Preferences.h>

class Storage {
public:
  void begin();

  // Schedule (JSON string)
  String loadScheduleJson();
  void   saveScheduleJson(const String& json);

  // Per-slot last-fired epochs (JSON array), the double-dose guard
  String loadFiredJson();
  void   saveFiredJson(const String& json);

  // Carousel absolute slot index
  int  loadSlot();
  void saveSlot(int slot);

  // Web admin credentials
  bool hasAuth();
  bool loadAuth(String& user, String& saltHex, String& hashHex);
  void saveAuth(const String& user, const String& saltHex, const String& hashHex);

  // Event ring buffer (JSON array string)
  String loadEventsJson();
  void   saveEventsJson(const String& json);

  // Telegram getUpdates offset (avoid replaying commands across reboots)
  long loadTgOffset();
  void saveTgOffset(long offset);

private:
  Preferences prefs_;
};
