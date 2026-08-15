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
  // A temp file left behind means a compaction was interrupted by a power cut.
  // The original is still intact, so the partial copy is simply discarded.
  if (LittleFS.exists(TMP_PATH)) LittleFS.remove(TMP_PATH);
  rescan();
  return true;
}

void Storage::rescan() {
  count_ = 0;
  lastId_ = 0;
  bytes_ = 0;
  File f = LittleFS.open(PATH_STRINGS, "r");
  if (!f) return;
  bytes_ = f.size();
  while (f.available()) {
    const String line = f.readStringUntil('\n');
    if (line.length() < 2) continue;
    count_++;
    // Oldest first, so the last line seen carries the highest id.
    JsonDocument doc;
    if (!deserializeJson(doc, line)) {
      const uint32_t id = doc["id"] | 0u;
      if (id > lastId_) lastId_ = id;
    }
  }
  f.close();
}

// Drops the oldest half of the log. Called only when the budget is reached, so
// its cost is amortised over hundreds of strings.
bool Storage::compact() {
  const uint32_t keepFrom = count_ / 2;
  if (keepFrom == 0) return true;

  File in = LittleFS.open(PATH_STRINGS, "r");
  if (!in) return false;
  File out = LittleFS.open(TMP_PATH, "w");
  if (!out) {
    in.close();
    return false;
  }

  uint32_t index = 0;
  uint32_t kept = 0;
  uint32_t written = 0;
  while (in.available()) {
    const String line = in.readStringUntil('\n');
    if (line.length() < 2) continue;
    if (index++ < keepFrom) continue;
    out.print(line);
    out.print('\n');
    kept++;
    written += line.length() + 1;
  }
  in.close();
  out.close();

  LittleFS.remove(PATH_STRINGS);
  if (!LittleFS.rename(TMP_PATH, PATH_STRINGS)) {
    rescan();
    return false;
  }
  count_ = kept;
  bytes_ = written;
  return true;
}

bool Storage::save(const StringRun& run) {
  JsonDocument doc;
  run.toJson(doc.to<JsonObject>(), /*includeShots=*/true);

  if (bytes_ >= HISTORY_MAX_BYTES || count_ >= MAX_STORED_STRINGS) {
    if (!compact()) return false;
  }

  File out = LittleFS.open(PATH_STRINGS, "a");
  if (!out) return false;
  const size_t n = serializeJson(doc, out);
  out.print('\n');
  out.close();
  if (n == 0) return false;

  count_++;
  bytes_ += n + 1;
  if (run.id() > lastId_) lastId_ = run.id();
  return true;
}

bool Storage::clear() {
  LittleFS.remove(PATH_STRINGS);
  LittleFS.remove(TMP_PATH);
  count_ = 0;
  bytes_ = 0;
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
