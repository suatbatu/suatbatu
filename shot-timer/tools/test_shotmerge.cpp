// Host-side tests for the two-source merge rule.
//
//   cd tools && ./run_tests.sh
//
// With both the microphone and the accelerometer armed, one physical shot
// produces two candidates. A wrong answer here either doubles every shot in the
// string or silently swallows a genuine fast split — and either failure only
// shows up after a range trip. So it gets tested on a desk.
#include "../firmware/src/ShotMerge.h"

#include <cstdio>

namespace {

// Matches SHOT_MERGE_WINDOW_US in config.h.
constexpr int64_t WINDOW = 30000;
constexpr int64_t MS = 1000;

int failures = 0;

void check(bool ok, const char* what) {
  printf("%s  %s\n", ok ? "  ok  " : "  FAIL", what);
  if (!ok) failures++;
}

// Feeds a sequence of candidate timestamps through the rule and returns how
// many were accepted — which is exactly the shot count the string would show.
int countAccepted(const int64_t* candidates, int n) {
  MergeState s;
  int accepted = 0;
  for (int i = 0; i < n; i++) {
    if (!shouldAcceptShot(s, candidates[i], WINDOW)) continue;
    noteAcceptedShot(s, candidates[i]);
    accepted++;
  }
  return accepted;
}

void testSingleSource() {
  printf("\none source, ordinary strings\n");
  const int64_t first[] = {1180 * MS};
  check(countAccepted(first, 1) == 1, "a lone shot is accepted");

  // A six-shot Bill Drill at a realistic 0.22 s cadence.
  int64_t bill[6];
  for (int i = 0; i < 6; i++) bill[i] = (1200 + 220 * i) * MS;
  check(countAccepted(bill, 6) == 6, "six shots at 0.22 s splits all count");

  // The fastest splits a human produces are around 0.12 s — well clear of the
  // window, and the window would be wrong if they were not.
  int64_t fast[5];
  for (int i = 0; i < 5; i++) fast[i] = (1000 + 120 * i) * MS;
  check(countAccepted(fast, 5) == 5, "0.12 s splits all count");
}

void testDuplicateSuppression() {
  printf("\nthe same shot seen by both sources counts once\n");
  // Recoil reaches the accelerometer essentially instantly; the muzzle blast
  // reaches the microphone a few milliseconds later.
  const int64_t pair[] = {1000 * MS, 1004 * MS};
  check(countAccepted(pair, 2) == 1, "impulse then acoustic 4 ms later");

  const int64_t reversed[] = {1004 * MS, 1000 * MS};
  check(countAccepted(reversed, 2) == 1,
        "and the same when they arrive out of time order");

  // Six shots, each detected twice, must still be six.
  int64_t doubled[12];
  for (int i = 0; i < 6; i++) {
    doubled[i * 2] = (1200 + 220 * i) * MS;
    doubled[i * 2 + 1] = (1200 + 220 * i) * MS + 5 * MS;
  }
  check(countAccepted(doubled, 12) == 6, "a six-shot string detected twice over");
}

void testWindowEdges() {
  printf("\nthe window boundary\n");
  const int64_t justInside[] = {0, WINDOW - 1};
  check(countAccepted(justInside, 2) == 1, "1 us inside the window merges");

  const int64_t exactlyAt[] = {0, WINDOW};
  check(countAccepted(exactlyAt, 2) == 2, "exactly at the window is a new shot");

  const int64_t justOutside[] = {0, WINDOW + 1};
  check(countAccepted(justOutside, 2) == 2, "1 us outside the window is a new shot");
}

void testOutOfOrderDoesNotWalkBackwards() {
  printf("\nstale arrivals cannot walk the window backwards\n");
  // The acoustic path defers by a DMA block, so a burst can arrive newest
  // first. The reference must not drift older and let a duplicate through.
  MergeState s;
  const int64_t newest = 1000 * MS;
  shouldAcceptShot(s, newest, WINDOW);
  noteAcceptedShot(s, newest);
  // A stale candidate is rejected...
  const int64_t stale = 990 * MS;
  check(!shouldAcceptShot(s, stale, WINDOW), "a stale duplicate is rejected");
  noteAcceptedShot(s, stale);  // even if noted, it must not move the reference
  check(s.lastAcceptedUs == newest, "the reference stays at the newest timestamp");
  // ...and a real next shot 25 ms after the *newest* is still merged, not
  // wrongly accepted because the reference slipped backwards.
  check(!shouldAcceptShot(s, newest + 25 * MS, WINDOW),
        "25 ms after the newest still merges");
}

void testSourceSelection() {
  printf("\nprofile source selection\n");
  check(acousticEnabled(ShotSource::Acoustic), "acoustic profile uses the mic");
  check(!impulseEnabled(ShotSource::Acoustic), "acoustic profile ignores the IMU");
  check(!acousticEnabled(ShotSource::Impulse), "impulse profile ignores the mic");
  check(impulseEnabled(ShotSource::Impulse), "impulse profile uses the IMU");
  check(acousticEnabled(ShotSource::Both) && impulseEnabled(ShotSource::Both),
        "both means both");
}

}  // namespace

int main() {
  printf("shot-timer merge rule tests\n");
  testSingleSource();
  testDuplicateSuppression();
  testWindowEdges();
  testOutOfOrderDoesNotWalkBackwards();
  testSourceSelection();

  printf("\n%s\n", failures == 0 ? "all tests passed" : "TESTS FAILED");
  return failures == 0 ? 0 : 1;
}
