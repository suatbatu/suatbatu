# Shot detection

Implemented in [`ShotDetector.cpp`](../firmware/src/ShotDetector.cpp) and
[`Tdoa.h`](../firmware/src/Tdoa.h). The geometry and correlation maths are
tested on a host — `cd tools && ./run_tests.sh`.

## The problem

A shot timer has one hard job: decide *when* a bang happened, to a hundredth of
a second, in an environment full of other bangs. Three separate questions hide
inside that:

1. **Is it loud enough?** — discrimination against range noise and wind.
2. **Is it a shot, or shot 1 coming back off a wall?** — echo rejection.
3. **Is it *your* shot, or the bay 15 m to your left?** — spatial rejection.
4. And given all that: **at what instant did it arrive?** — timing.

Cheap timers answer only (1) and get (4) wrong by an order of magnitude more
than they claim. The features that justify a $330 timer are (2) and (3).

## Signal chain

```
Two I2S MEMS microphones, one shared bus
   │  48 kHz stereo: mic A = left slot, mic B = right slot
   ▼
DMA block (256 frames = 5.33 ms) ──▶ prio-10 task
   │
   ├── ring buffer (1024 samples per channel, 16-bit)  ← for correlation
   ├── envelope follower on A   (instant attack, ~15 ms release)
   ├── noise floor              (~500 ms EMA, frozen during events)
   ├── threshold                max(floor × ratio, absolute floor)
   ├── refractory               hard minimum between shots
   └── echo guard               decaying bar set by the last shot's own peak
   │
   ▼
candidate onsets ──▶ (wait one block for post-roll) ──▶ cross-correlate A vs B
   │                                                        │
   │                                                   lag → angle
   │                                                        │
   └──────────────── direction gate ◀───────────────────────┘
                             │
                             ▼
        queue of int64 esp_timer microsecond timestamps ──▶ TimerApp
```

## 1. Threshold

Per sample: `env = max(|s|, env + (|s| − env) × release)`.

Attack is instant on purpose. The envelope crosses the threshold on the *exact
sample* the transient arrives, which is what makes the onset timestamp mean
something. A smoothed attack would add a delay proportional to how loud the shot
was — quiet shots timing late, loud shots early — and your splits would depend
on your ammunition.

Release is ~15 ms: long enough to ride over the ringing tail of a muzzle blast,
short enough to be back down before the next shot of a 0.15 s split.

The **noise floor** is a ~500 ms EMA of the envelope, updated *only* while
nothing is triggering. That last part matters: if the floor tracked during a
string, six fast shots would walk it upward and the last shots would fall under
the threshold and go uncounted.

```
threshold = max(noiseFloor × ratio, absoluteFloor)
```

The ratio term rejects a rising ambient level. The absolute term stops the
detector chasing its own noise in a silent room, where the floor approaches zero
and *any* ratio would trigger.

`sensitivity` (1–10) sets both:

| Sensitivity | Ratio over floor | Absolute floor (of 2²³) | Intended for |
|---|---|---|---|
| 1 | 40× | 600 000 | Centrefire, muzzle blast only, busy range |
| 4–5 | 28–24× | 110 000–60 000 | Pistol / rifle at an outdoor bay |
| 8 | 12× | 10 000 | Suppressed, rimfire |
| 9–10 | 8–4× | 5 500–3 000 | Airsoft, CO2, dry fire |

## 2. Echo rejection

A fixed blanking window — ignore everything for 60 ms after a shot — is the
common approach and it is a blunt one. It cannot tell a reflection at 80 ms from
a real shot at 80 ms, so it either lets echoes through or caps your rate of fire.

Instead, each shot raises a **decaying guard** derived from its own peak:

```
guard = peak × 10^(−echoRejectDb/20)      at the moment of the shot
guard × = decay                            every sample thereafter
```

A candidate must clear `max(threshold, guard)`. With the default 10 dB, a
reflection arriving 40 ms later at a third of the original amplitude is
swallowed, while a genuine second shot at comparable level passes immediately.
The guard is scaled by *this shot's* peak rather than an absolute number, so it
works the same for a .22 and a .308.

A short **refractory** period (default 25 ms) still exists underneath, because
no trigger finger runs faster than 40 shots/second and a single transient must
not re-trigger inside its own rise.

Indoor bays with hard walls want a larger `echoRejectDb`; a suppressed gun
outdoors wants a smaller one, because the shot's own peak is closer to its echo.

## 3. Direction gate — telling your shot from the next bay

This is the feature that separates a $130 timer from a $330 one, and the honest
way to do it is geometry, not guesswork.

Two microphones, **d = 120 mm apart** by default, both on one I2S bus (mic A
with L/R to GND, mic B with L/R to VDD — three shared wires, no extra GPIO). A
sound arriving at angle θ off the array's axis reaches the far microphone later
by `d·sin(θ)/c`. At 48 kHz that is:

| Off-axis angle | Arrival difference | Samples |
|---|---|---|
| 0° (dead ahead) | 0 µs | 0 |
| 25° | 148 µs | 7 |
| 45° | 247 µs | 12 |
| 90° (fully to the side) | 350 µs | 17 |

**48 kHz is chosen for exactly this reason.** At 32 kHz the whole 0–90° range
would span 11 samples and the gate would be too coarse to be useful.

The lag is measured by **cross-correlating** a 128-sample window around the
onset (32 samples before, 96 after) across ±32 lags, then normalising the peak
by `sqrt(Ea·Eb)` to get a 0–100 confidence. Correlation rather than comparing
two independent onset detections, because the nearer microphone is genuinely
louder and an amplitude-dependent threshold crossing would bias the estimate —
[`test_tdoa.cpp`](../tools/test_tdoa.cpp) checks the lag is unchanged with the
second channel at 100%, 50% and 25% amplitude.

This is why the decision is deferred by one DMA block: correlation needs
post-roll that does not exist yet when the onset fires. **The reported shot time
still comes from the onset sample**, so the extra few milliseconds of latency
cost no accuracy at all.

### It fails open, always

The gate rejects a shot only when it is *confident* the sound came from
off-axis. It passes the shot through whenever:

- the profile has `directionGate` off,
- no second microphone is detected (channel B is silent),
- the correlation window fell outside the ring buffer,
- or the correlation confidence is below 35%.

Dropping a real shot is a far worse failure than admitting a neighbour's. A
missed shot corrupts the string and the shooter may not notice until they read
the splits; an extra shot is obvious immediately.

The UI shows the rejection counters for the same reason — without them, a
correctly working gate and a broken microphone look identical from the
shooter's side.

## 4. Timing

The naive approach is `esp_timer_get_time()` at the moment the detector decides
a shot happened. That timestamp includes however long the task waited to be
scheduled after the DMA block completed — hundreds of microseconds normally,
milliseconds if something else was busy. That jitter lands directly in the
reported split.

Instead, shot times are computed from the **I2S sample counter**:

```
t(sample i) = anchor + i × (1 000 000 / 48 000) µs
```

The I2S peripheral clocks samples at a fixed rate regardless of what the CPU is
doing, so the index of the sample that crossed the threshold *is* the time, with
20.8 µs resolution and no scheduling jitter.

`anchor` — the `esp_timer` time of sample 0 — is maintained by a slow servo:

```
error   = now − predictedBlockEnd
anchor += clamp(error, ±2 ms) / 64
```

The `/64` means one late wake-up moves the anchor by under 2% of the error, so
jitter averages out, while genuine drift between the I2S clock and `esp_timer`
(both crystal-derived, so parts per million) is still tracked. An error beyond
±50 ms means a real discontinuity — a DMA overrun, a long critical section — and
forces a hard re-anchor rather than a slow crawl.

### What this buys, and what it does not

**Relative timing** — the splits between shots — is good to well under a
millisecond. That is the number that matters: a split is a difference, and any
constant offset cancels.

**Absolute timing** — beep to first shot — carries a constant, uncalibrated
offset from acoustic travel (≈ 2.9 ms per metre), microphone group delay, and
the servo's slight positive bias. A few milliseconds in total: smaller than the
hundredth of a second displayed, but not zero. `micOffsetMs` applies a fixed
correction if you characterise it against a reference timer. It ships at 0
because guessing at a calibration is worse than admitting there isn't one.

## Beep muting

The buzzer is centimetres from the microphones and is, by design, the loudest
thing in the system. Detection is muted for the tone plus 40 ms, on the start
beep and every par beep. **A shot fired inside that window is not recorded.**
Par beeps are deliberately short (120 ms) and higher-pitched (3.4 kHz) to keep
that window small and to be distinguishable by ear.

## Profiles

All four detection parameters — sensitivity, refractory, echo rejection, and the
direction gate — live in a profile, not in global settings. Six ship by default
(Pistol, Rifle, Suppressed, Rimfire, Airsoft/CO2, Dry fire) and all of them are
editable and renameable.

The shipped numbers are reasoned starting points, not measurements. Correct them
with a real gun.

## Tuning it in the field

1. Open **Settings** and watch the level meter with the range live. Green is the
   instantaneous envelope, amber is the adaptive floor.
2. Set sensitivity so the bar sits well below full during normal range noise and
   pegs on your own shots.
3. Fire a string. Check the **Detector** panel: `accepted` should equal your
   round count. If `rejected as echo` is climbing, lower `echoRejectDb`. If
   `rejected as off-axis` is climbing on your own shots, widen the acceptance
   angle or check that the microphones are level and pointed at the shooter.
4. `last angle` should read near 0° for your own shots. If it does not, your
   microphone spacing setting does not match the hardware, or the two ports are
   not on the axis you think they are.
5. Point the array **at the shooter**, not downrange, with the two microphone
   ports on a horizontal line. That puts your muzzle blast at 0° and the
   neighbouring bays out at 40–80°, which is exactly the separation the gate
   needs.
