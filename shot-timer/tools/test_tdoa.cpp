// Host-side tests for the direction gate's maths.
//
//   cd tools && ./run_tests.sh
//
// The detector cannot be debugged on a range — by the time you notice the gate
// is rejecting your own shots, you have burned fifty rounds finding out. So the
// part with sign conventions and circular indexing in it gets tested here,
// against synthetic signals with a known delay.
#include "../firmware/src/Tdoa.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr size_t RING = 1024;
constexpr int WINDOW = 128;
constexpr int MAX_LAG = 32;
constexpr int TDOA_PRE = 32;  // must match config.h
constexpr float SAMPLE_RATE = 48000.0f;
constexpr float SPEED_OF_SOUND = 343.0f;
constexpr float SPACING_M = 0.120f;

int failures = 0;

void check(bool ok, const char* what) {
  printf("%s  %s\n", ok ? "  ok  " : "  FAIL", what);
  if (!ok) failures++;
}

void checkEq(int got, int want, const char* what) {
  const bool ok = got == want;
  printf("%s  %s (got %d, want %d)\n", ok ? "  ok  " : "  FAIL", what, got, want);
  if (!ok) failures++;
}

// A gunshot-ish transient: near-instant rise, exponential decay, plus a little
// noise so the correlation has something other than a single spike to lock to.
double transient(int n) {
  if (n < 0) return 0.0;
  const double envelope = exp(-n / 40.0);
  const double tone = sin(n * 0.7) + 0.5 * sin(n * 1.9 + 0.3);
  return envelope * tone;
}

// Fills both rings with the same transient, with channel B delayed by
// `delaySamples` (positive = B hears it later = source nearer A).
void fill(int16_t* a, int16_t* b, int onsetAt, int delaySamples, double gainB,
          double noise = 0.0) {
  srand(12345);
  for (size_t i = 0; i < RING; i++) {
    const double na = noise * ((rand() % 2000) / 1000.0 - 1.0);
    const double nb = noise * ((rand() % 2000) / 1000.0 - 1.0);
    const double va = transient(static_cast<int>(i) - onsetAt) + na;
    const double vb = gainB * transient(static_cast<int>(i) - onsetAt - delaySamples) + nb;
    a[i] = static_cast<int16_t>(va * 8000.0);
    b[i] = static_cast<int16_t>(vb * 8000.0);
  }
}

void testKnownDelays() {
  printf("\ncross-correlation recovers a known delay\n");
  static int16_t a[RING], b[RING];
  const int onset = 300;

  for (int want : {0, 1, -1, 5, -5, 16, -16, 25, -25}) {
    fill(a, b, onset, want, 1.0);
    const LagResult r = estimateLag(a, b, RING, onset - TDOA_PRE, RING, WINDOW, MAX_LAG);
    char msg[96];
    snprintf(msg, sizeof(msg), "delay %+d samples (confidence %u)", want, r.confidence);
    checkEq(r.lag, want, msg);
    check(r.confidence > 50, "  and correlates strongly");
  }
}

void testAmplitudeIndependence() {
  printf("\nlag is unaffected by the near microphone being louder\n");
  static int16_t a[RING], b[RING];
  // A source close to mic A is genuinely louder there. The lag must not care.
  for (double gain : {1.0, 0.5, 0.25}) {
    fill(a, b, 300, 8, gain);
    const LagResult r = estimateLag(a, b, RING, 300 - TDOA_PRE, RING, WINDOW, MAX_LAG);
    char msg[96];
    snprintf(msg, sizeof(msg), "channel B at %.0f%% amplitude", gain * 100);
    checkEq(r.lag, 8, msg);
  }
}

void testNoiseTolerance() {
  printf("\nlag survives additive noise\n");
  static int16_t a[RING], b[RING];
  fill(a, b, 300, 10, 1.0, 0.05);
  const LagResult r = estimateLag(a, b, RING, 300 - TDOA_PRE, RING, WINDOW, MAX_LAG);
  checkEq(r.lag, 10, "5% noise");
  check(r.confidence > 40, "  confidence still usable");
}

void testMissingSecondMic() {
  printf("\na missing second microphone is reported, not guessed at\n");
  static int16_t a[RING], b[RING];
  fill(a, b, 300, 0, 1.0);
  memset(b, 0, sizeof(b));  // unwired right slot reads as silence
  const LagResult r = estimateLag(a, b, RING, 300 - TDOA_PRE, RING, WINDOW, MAX_LAG);
  check(r.valid, "result is valid");
  check(r.energyB == 0.0f, "channel B energy is zero");
  check(r.confidence == 0, "confidence is zero, so the gate fails open");
}

void testWindowBounds() {
  printf("\nout-of-ring windows are refused rather than wrapped\n");
  static int16_t a[RING], b[RING];
  fill(a, b, 300, 0, 1.0);
  // Onset too new: the post-roll has not been captured yet.
  const LagResult tooNew = estimateLag(a, b, RING, 990, 1000, WINDOW, MAX_LAG);
  check(!tooNew.valid, "window past the newest sample");
  // Onset too old: it has already been overwritten in the ring.
  const LagResult tooOld = estimateLag(a, b, RING, 100, 2000, WINDOW, MAX_LAG);
  check(!tooOld.valid, "window older than the ring");
}

void testGeometry() {
  printf("\nangle and acceptance-lag geometry\n");
  // 120 mm at 48 kHz: a source at 90 degrees off-axis arrives 350 us late,
  // which is 16.8 samples.
  const int maxLag90 = maxLagForAngle(89, SPACING_M, SAMPLE_RATE, SPEED_OF_SOUND, MAX_LAG);
  check(maxLag90 >= 16 && maxLag90 <= 18, "full off-axis is ~17 samples");

  const int maxLag25 = maxLagForAngle(25, SPACING_M, SAMPLE_RATE, SPEED_OF_SOUND, MAX_LAG);
  check(maxLag25 >= 7 && maxLag25 <= 8, "25 degrees is ~7 samples");

  checkEq(lagToAngleDeg(0, SPACING_M, SAMPLE_RATE, SPEED_OF_SOUND), 0, "zero lag is dead ahead");

  const int a45 = lagToAngleDeg(12, SPACING_M, SAMPLE_RATE, SPEED_OF_SOUND);
  char msg[64];
  snprintf(msg, sizeof(msg), "12 samples reads as %d degrees", a45);
  check(a45 > 40 && a45 < 50, msg);

  // An impossible lag (noise, not a real source) saturates rather than
  // producing a NaN that would then compare false against every threshold.
  const int impossible = lagToAngleDeg(30, SPACING_M, SAMPLE_RATE, SPEED_OF_SOUND);
  check(impossible == 90, "impossible lag saturates at 90 degrees");
}

void testGateDecision() {
  printf("\nend to end: the gate keeps your shot and drops the next bay\n");
  static int16_t a[RING], b[RING];
  const int accept = maxLagForAngle(25, SPACING_M, SAMPLE_RATE, SPEED_OF_SOUND, MAX_LAG);

  // Your own shot: in front, so both mics hear it within a sample or two.
  fill(a, b, 300, 1, 0.95);
  LagResult mine = estimateLag(a, b, RING, 300 - TDOA_PRE, RING, WINDOW, MAX_LAG);
  check(abs(mine.lag) <= accept, "own shot (1 sample lag) accepted");

  // The bay to the side: 60 degrees off-axis is 14.5 samples at this spacing.
  fill(a, b, 300, 15, 0.7);
  LagResult neighbour = estimateLag(a, b, RING, 300 - TDOA_PRE, RING, WINDOW, MAX_LAG);
  check(abs(neighbour.lag) > accept, "neighbouring bay (15 sample lag) rejected");
}

}  // namespace

int main() {
  printf("shot-timer direction gate tests\n");
  testKnownDelays();
  testAmplitudeIndependence();
  testNoiseTolerance();
  testMissingSecondMic();
  testWindowBounds();
  testGeometry();
  testGateDecision();

  printf("\n%s\n", failures == 0 ? "all tests passed" : "TESTS FAILED");
  return failures == 0 ? 0 : 1;
}
