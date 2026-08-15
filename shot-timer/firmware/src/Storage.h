// String history on LittleFS.
//
// Stored as NDJSON — one string per line, newest first. That keeps every
// operation streaming: saving a run copies lines through a temp file and
// renames, and serving the history never holds the whole history in RAM. A
// flat JSON array would be tidier on disk and much worse on a 320 KB heap.
#pragma once

#include <Arduino.h>
#include <stdint.h>

#include <functional>

class StringRun;

class Storage {
 public:
  bool begin();

  // Prepends the run and trims to MAX_STORED_STRINGS. Returns false on I/O error.
  bool save(const StringRun& run);
  bool clear();

  // Calls `fn` with each stored line (a complete JSON object), newest first.
  // Stops early if `fn` returns false.
  void forEachLine(const std::function<bool(const String&)>& fn) const;

  // The one stored line whose "id" matches, or an empty String.
  String findById(uint32_t id) const;

  uint32_t count() const { return count_; }
  uint32_t nextId() { return ++lastId_; }

 private:
  uint32_t lastId_ = 0;
  uint32_t count_ = 0;

  void rescan();
};

extern Storage storage;
