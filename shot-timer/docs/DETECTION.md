# Shot detection

Everything in this document is implemented in
[`firmware/src/ShotDetector.cpp`](../firmware/src/ShotDetector.cpp).

## The problem

A shot timer has exactly one hard job: decide *when* a bang happened, to a
hundredth of a second, in an environment full of other bangs. Two things have to
be right, and they are separate problems:

1. **Discrimination** — was that a shot, or the bay next door, or wind on the
   microphone, or the buzzer?
2. **Timing** — given that it was a shot, at what instant did it arrive?

Most naive implementations conflate them, threshold on a block of samples, and
timestamp with `millis()` when the block was processed. That gets you ±10 ms of
jitter, which is the same order as the split you are trying to measure.

## Signal chain

```
INMP441 (I2S, 24-bit)
   │  32 kHz, mono, left slot
   ▼
DMA block (256 frames = 8 ms) ──▶ prio-10 task
   │
   ├── envelope follower  (instant attack, ~15 ms release)
   ├── noise floor        (~500 ms EMA, frozen during events)
   ├── threshold          max(floor × ratio, absolute floor)
   ├── blanking           ignore N ms after each trigger
   └── mute window        drop anything during a beep
   │
   ▼
queue of int64 esp_timer microsecond timestamps ──▶ TimerApp
```

### Envelope follower

Per sample: `env = max(|s|, env + (|s| − env) × release)`.

Attack is instant on purpose. The envelope crosses the threshold on the *exact
sample* the transient arrives, which is what makes the onset timestamp mean
something. A smoothed attack would add a systematic delay proportional to how
loud the shot was — quiet shots would time late, loud shots early, and your
splits would depend on your ammunition.

Release is ~15 ms: long enough to ride over the ringing tail of a muzzle blast,
short enough to be back down before the next shot of a 0.15 s split.

### Adaptive noise floor

A ~500 ms exponential moving average of the envelope, updated **only** while
nothing is triggering. That last part matters: if the floor tracked during a
string, six fast shots would walk it upward and the last shots would fall under
the threshold and go uncounted.

The floor is what lets one sensitivity setting work in a quiet indoor bay and on
a windy outdoor range.

### Threshold

```
threshold = max(noiseFloor × ratio, absoluteFloor)
```

`sensitivity` (1–10) sets both terms:

| Sensitivity | Ratio over floor | Absolute floor (of 2²³) | Intended for |
|---|---|---|---|
| 1 | 40× | 600 000 | Centrefire, muzzle blast only, busy range |
| 5 | 24× | 60 000 | Default: pistol at an outdoor bay |
| 8 | 12× | 10 000 | Suppressed, .22, airsoft |
| 10 | 4× | 3 000 | Dry fire — a trigger click at a metre |

The ratio term rejects a rising ambient level. The absolute term stops the
detector chasing its own noise in a silent room, where the floor approaches
zero and *any* ratio would trigger.

### Blanking

After each trigger, further triggers are suppressed for `blankingMs`
(default 60 ms). This is what stops a single shot registering three times off
the muzzle blast, the wall echo, and the ejected case. 60 ms is the value
commercial timers converge on; it also puts a hard ceiling of ~16 shots/second
on what the timer can resolve, which is well beyond any trigger finger.

Blanking is applied whether or not the timer is armed, so the floor tracking
stays consistent between strings.

### Beep muting

The buzzer is a few centimetres from the microphone and is, by design, the
loudest thing in the system. Without suppression every string would open with a
phantom shot at t = 0.

`TimerApp` mutes the detector for the tone duration plus 40 ms, on both the
start beep and every par beep. **A shot fired inside that window is not
recorded.** Par beeps are deliberately short (120 ms) and higher-pitched
(3.4 kHz) to keep that window small and to be distinguishable by ear.

## Timing

This is the part that is easy to get subtly wrong.

The naive approach is `esp_timer_get_time()` at the moment the detector decides a
shot happened. That timestamp includes however long the task waited to be
scheduled after the DMA block completed — hundreds of microseconds normally,
milliseconds if something else was busy. That jitter lands directly in the
reported split.

Instead, shot times are computed from the **I2S sample counter**:

```
t(sample i) = anchor + i × (1 000 000 / 32 000) µs
```

The I2S peripheral clocks samples at a fixed rate regardless of what the CPU is
doing, so the index of the sample that crossed the threshold *is* the time, with
31.25 µs resolution and no scheduling jitter at all.

`anchor` — the `esp_timer` time of sample 0 — is maintained by a slow servo.
After each block:

```
error   = now − predictedBlockEnd
anchor += clamp(error, ±2 ms) / 64
```

The `/64` means one late wake-up moves the anchor by under 2% of the error, so
jitter averages out, while genuine drift between the I2S clock and `esp_timer`
(both crystal-derived, so parts per million) is still tracked over a long
session. An error beyond ±50 ms means a real discontinuity — a DMA overrun, a
long critical section — and forces a hard re-anchor rather than a slow crawl.

### What this buys you, and what it does not

**Relative timing** — the splits between shots in a string — is good to well
under a millisecond. That is the number that actually matters: a split is a
difference, and any constant offset cancels out.

**Absolute timing** — beep to first shot — carries a constant, uncalibrated
offset from:

- acoustic travel time (≈ 2.9 ms per metre from muzzle to microphone),
- microphone group delay (a few hundred microseconds),
- the servo's slight positive bias, since `now` is always sampled after the DMA
  block truly ended.

Together these are on the order of a few milliseconds — smaller than the
hundredth of a second the timer displays, but not zero. `micOffsetMs` in
settings adds a fixed correction if you ever characterise it against a reference
timer. It ships at 0 because guessing at a calibration is worse than admitting
there isn't one.

## Tuning it in the field

1. Open the web UI's **Settings** tab and watch the level meter with the range
   live. The green bar is the instantaneous envelope; the amber line is the
   adaptive floor.
2. Set sensitivity so the bar sits well below full during normal range noise and
   pegs on your own shots. Start at 5 and work down if you get phantom shots
   from neighbouring bays, up if your suppressed .22 is being missed.
3. If a single shot registers twice, raise `blankingMs`. Indoor bays with hard
   walls sometimes need 80–100 ms.
4. Point the microphone at the shooter, not downrange. You want the muzzle
   blast, and you want the neighbouring bays off-axis.
