// String history on LittleFS.
//
// Append-only NDJSON — one string per line, oldest first. Appending a run is
// O(1) regardless of how much history there is, which is what makes a large
// history practical: the previous rewrite-the-whole-file approach cost a full
// copy per string and burned flash in proportion to how much you had stored.
//
// When the log outgrows its byte budget the oldest half is dropped in one
// compaction pass, so the amortised cost stays near zero.
#pragma once

#include <Arduino.h>
#include <stdint.h>

#include <functional>

class StringRun;

class Storage {
 public:
  bool begin();

  // Appends the run, compacting first if the log is at its budget.
  bool save(const StringRun& run);
  bool clear();

  // Calls `fn` with each stored line (a complete JSON object), **oldest
  // first** — the order they are on disk. Callers that want newest-first
  // reverse it themselves; on a device this size, streaming in file order and
  // reversing in the browser is the cheap way round.
  // Stops early if `fn` returns false.
  void forEachLine(const std::function<bool(const String&)>& fn) const;

  // The one stored line whose "id" matches, or an empty String.
  String findById(uint32_t id) const;

  uint32_t count() const { return count_; }
  uint32_t bytesUsed() const { return bytes_; }
  uint32_t nextId() { return ++lastId_; }

 private:
  bool compact();
  void rescan();

  uint32_t lastId_ = 0;
  uint32_t count_ = 0;
  uint32_t bytes_ = 0;
};

extern Storage storage;
