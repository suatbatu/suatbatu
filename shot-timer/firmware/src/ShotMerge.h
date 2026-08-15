// Merging two shot sources into one string.
//
// With both the microphone and the accelerometer armed, one physical shot
// produces two candidates a few milliseconds apart. Deciding which of those is
// a second shot and which is the same shot heard twice is a small amount of
// logic with a large blast radius — a wrong answer either doubles every shot in
// the string or silently swallows a genuine fast split.
//
// So it lives here: no Arduino headers, pure functions, tested on a host.
// See tools/test_shotmerge.cpp.
#pragma once

#include <stdint.h>

enum class ShotSource : uint8_t {
  Acoustic = 0,  // microphone only — what every commercial timer does
  Impulse = 1,   // accelerometer only — the dry-fire answer
  Both = 2,      // either source may register a shot, merged
};

struct MergeState {
  int64_t lastAcceptedUs = 0;
  bool haveAccepted = false;

  void reset() {
    lastAcceptedUs = 0;
    haveAccepted = false;
  }
};

// Should `candidateUs` be recorded as a new shot?
//
// The rule is deliberately "first arrival wins": a candidate within
// `mergeWindowUs` of the last accepted shot is treated as the same physical
// event detected twice, and dropped. Holding candidates back to prefer the
// more accurate source would buy a millisecond of timestamp quality at the cost
// of delaying every shot by the window — a bad trade on a device whose whole
// job is to feel instant.
//
// Because the acoustic path defers by one DMA block while the accelerometer
// interrupt is immediate, candidates can arrive out of *time* order. A
// candidate that is merely older than the last accepted one is still within the
// window, so the absolute difference is what matters, not the sign.
inline bool shouldAcceptShot(const MergeState& state, int64_t candidateUs,
                             int64_t mergeWindowUs) {
  if (!state.haveAccepted) return true;
  int64_t delta = candidateUs - state.lastAcceptedUs;
  if (delta < 0) delta = -delta;
  return delta >= mergeWindowUs;
}

inline void noteAcceptedShot(MergeState& state, int64_t candidateUs) {
  // Keep the newest timestamp as the reference so a burst of stale arrivals
  // cannot walk the window backwards and let a duplicate through.
  if (!state.haveAccepted || candidateUs > state.lastAcceptedUs) {
    state.lastAcceptedUs = candidateUs;
  }
  state.haveAccepted = true;
}

// Is a given source live under this profile?
inline bool acousticEnabled(ShotSource s) {
  return s == ShotSource::Acoustic || s == ShotSource::Both;
}
inline bool impulseEnabled(ShotSource s) {
  return s == ShotSource::Impulse || s == ShotSource::Both;
}

inline const char* shotSourceName(ShotSource s) {
  switch (s) {
    case ShotSource::Acoustic: return "acoustic";
    case ShotSource::Impulse: return "impulse";
    case ShotSource::Both: return "both";
  }
  return "?";
}
