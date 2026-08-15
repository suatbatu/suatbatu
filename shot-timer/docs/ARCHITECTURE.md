# Architecture

## The one rule

**Nothing that can block ever sits between the microphone and a timestamp.**

Everything else in this design follows from that. A web client re-rendering, a
LittleFS write, an I2C frame to the OLED — all of them take milliseconds, and a
millisecond of delay in the audio path is a millisecond of error in a split.

So the audio path is a FreeRTOS task at priority 10, pinned to core 1, that owns
the I2S channel and does nothing but read samples and push `int64` timestamps
into a queue. It does not allocate. It does not touch the filesystem, the
display, or the network. The Arduino `loop()` — priority 1 — drains that queue
whenever it gets around to it, and *when* it gets around to it does not matter,
because the timestamp was computed from the sample index, not from the moment
the queue was drained.

## Modules

| File | Responsibility |
|---|---|
| `ShotDetector` | Stereo I2S, envelope follower, adaptive threshold, echo guard, direction gate, and the sample clock. The only thing that decides a shot happened. See [`DETECTION.md`](DETECTION.md). |
| `Tdoa.h` | The direction-finding maths, deliberately free of Arduino and ESP-IDF headers so it can be tested on a host. `Settings` and `ShotDetector` both call it, so the geometry has one definition. |
| `TimerApp` | The state machine that *is* the timer: start delay → beep → open string → par beeps → close. Both the buttons and the web UI drive it through `requestStart()` / `requestStop()`, so a string started from a phone behaves identically to one started on the device. |
| `StringRun` | One string: the beep time, the shot offsets, and the derived numbers (first, splits, total, best/worst). Pure data, no I/O. |
| `Settings` | Everything tunable, persisted to NVS as a versioned blob. Detection parameters live in one of six **profiles** rather than at top level, so switching guns is one control instead of four. Also owns the mapping from a single `sensitivity` knob to the detector's two thresholds. |
| `Storage` | String history on LittleFS as append-only NDJSON, compacted when it reaches its byte budget. Appending is O(1) no matter how much history there is, which is what makes a large history practical on flash. |
| `Buzzer` | LEDC square wave into a piezo. Non-blocking: `beep()` returns the tone's start timestamp, `tick()` stops it. |
| `Display` | SSD1306 rendering and the on-device settings menu. |
| `Buttons` | Debounce, short/long press. |
| `Net` | SoftAP-first Wi-Fi. |
| `WebInterface` | REST + WebSocket. See [`API.md`](API.md). |
| `main.cpp` | Wiring only: maps three buttons onto `TimerApp`, pumps the loop. |

## State machine

```
        ┌────────────────────────────────────────────┐
        │                                            │
        ▼                  start                     │
    ┌───────┐  ────────────────────────▶ ┌───────────┐
    │ Ready │                            │ Countdown │
    └───────┘  ◀──────────────────────── └───────────┘
        ▲             stop (aborted)           │
        │                                      │ delay elapsed
        │ start                                │  → BEEP, t=0
        │                                      ▼
    ┌────────┐   stop / auto-stop timeout  ┌─────────┐
    │ Review │ ◀───────────────────────────│ Running │
    └────────┘                             └─────────┘
```

`Menu` hangs off `Ready` and `Review` and is deliberately unreachable while a
string is open — nobody should be able to change the sensitivity mid-string.

Two details worth knowing:

- **The beep is t = 0**, and it is the *actual* hardware timestamp returned by
  `Buzzer::beep()`, not the moment the delay was scheduled to expire.
- **Pressing start mid-string means abort**, not "start another". Aborting from
  `Countdown` throws the string away; stopping from `Running` keeps it.

## Data flow for one string

```
  button / POST /api/start
        │
        ▼
  TimerApp: random delay from esp_random()
        │  (never displayed — see DETECTION.md)
        ▼
  Buzzer::beep() ──▶ returns beepAtUs ──▶ StringRun::begin()
        │
        ├──▶ ShotDetector::muteUntil(beepEnd + 40 ms)
        └──▶ ShotDetector::arm()
                 │
                 │  each onset that clears the threshold,
                 │  the echo guard and the direction gate
                 ▼
             queue<int64 µs>
                 │
                 ▼
  TimerApp::loop() ──▶ StringRun::addShot()  ──▶ WebSocket "shot" event
                                              └─▶ OLED
        │
        │  stop, or autoStopSec of silence
        ▼
  StringRun::end() ──▶ Storage::save() ──▶ WebSocket "end" event
```

## Why an event push rather than polling

A running string produces information at irregular, unpredictable instants. Any
poll interval is either laggy or wasteful, and on a device where the network
stack shares a core with everything else, "wasteful" costs real time. So shots
are pushed over a WebSocket the moment they land, and a 5 Hz `tick` keeps the
browser's sweeping clock anchored between them. The browser only ever
*interpolates* between device-supplied numbers; it never computes one itself.

## Concurrency notes

- The shot queue is the only channel from the audio task to everything else.
- The direction gate defers each candidate by one DMA block, because correlating
  needs post-roll that does not exist when the onset fires. The reported time
  still comes from the onset sample, so the latency costs no accuracy — but it
  does mean a shot appears in the UI ~5 ms after it happened.
- `armed_`, `envelope_` and `noiseFloor_` are single 32-bit words written by the
  audio task and read elsewhere for display only; a torn read is not possible on
  this target and would not matter if it were.
- The mute deadline is 64-bit, so it *is* protected by a spinlock — a torn
  64-bit read would produce a nonsense deadline and either mute forever or not
  at all.
- Web handlers run on the AsyncTCP task, **not** the Arduino loop, so they never
  drive the state machine directly. `POST /api/start` sets a flag that
  `TimerApp::loop()` consumes on its next pass; the transition — sounding the
  buzzer, opening a string, writing flash — always happens in one task. The HTTP
  response therefore reports the state *before* the request took effect, and the
  WebSocket reports the transition a few milliseconds later.
- `GET /api/status` does read the open string from the web task while the loop
  may be appending to it. The worst case is a snapshot one shot out of date,
  which is what a status endpoint is for; nothing tears, because the shot array
  and count are plain 32-bit words.
