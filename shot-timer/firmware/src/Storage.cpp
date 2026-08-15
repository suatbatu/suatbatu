#include "Storage.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "StringRun.h"
#include "config.h"

Storage storage;

namespace {
constexpr char TMP_PATH[] = "/strings.tmp";
}

bool Storage::begin() {
  if (!LittleFS.begin(true)) return false;
  rescan();
  return true;
}

void Storage::rescan() {
  count_ = 0;
  lastId_ = 0;
  File f = LittleFS.open(PATH_STRINGS, "r");
  if (!f) return;
  while (f.available()) {
    const String line = f.readStringUntil('\n');
    if (line.length() < 2) continue;
    count_++;
    // Newest first, so the first line carries the highest id.
    if (lastId_ == 0) {
      JsonDocument doc;
      if (!deserializeJson(doc, line)) lastId_ = doc["id"] | 0;
    }
  }
  f.close();
}

bool Storage::save(const StringRun& run) {
  JsonDocument doc;
  run.toJson(doc.to<JsonObject>(), /*includeShots=*/true);

  File out = LittleFS.open(TMP_PATH, "w");
  if (!out) return false;

  serializeJson(doc, out);
  out.print('\n');

  // Carry over the previous history, dropping the oldest once we are full.
  uint32_t kept = 1;
  File in = LittleFS.open(PATH_STRINGS, "r");
  if (in) {
    while (in.available() && kept < MAX_STORED_STRINGS) {
      const String line = in.readStringUntil('\n');
      if (line.length() < 2) continue;
      out.print(line);
      out.print('\n');
      kept++;
    }
    in.close();
  }
  out.close();

  LittleFS.remove(PATH_STRINGS);
  if (!LittleFS.rename(TMP_PATH, PATH_STRINGS)) {
    rescan();
    return false;
  }
  count_ = kept;
  if (run.id() > lastId_) lastId_ = run.id();
  return true;
}

bool Storage::clear() {
  LittleFS.remove(PATH_STRINGS);
  LittleFS.remove(TMP_PATH);
  count_ = 0;
  // Deliberately not resetting lastId_: reusing ids a client may still be
  // holding causes more confusion than a gap in the numbering.
  return true;
}

void Storage::forEachLine(const std::function<bool(const String&)>& fn) const {
  File f = LittleFS.open(PATH_STRINGS, "r");
  if (!f) return;
  while (f.available()) {
    const String line = f.readStringUntil('\n');
    if (line.length() < 2) continue;
    if (!fn(line)) break;
  }
  f.close();
}

String Storage::findById(uint32_t id) const {
  String found;
  const String needle = String("\"id\":") + id;
  forEachLine([&](const String& line) {
    // Cheap pre-filter, then confirm by parsing so "id":12 cannot match "id":123.
    if (line.indexOf(needle) < 0) return true;
    JsonDocument doc;
    if (deserializeJson(doc, line)) return true;
    if ((doc["id"] | 0u) == id) {
      found = line;
      return false;
    }
    return true;
  });
  return found;
}
