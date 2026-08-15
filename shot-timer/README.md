# shot-timer 🎯

A DIY **shooting-sports shot timer** on an ESP32-S3: random start beep,
two-microphone acoustic shot detection, split times, par beeps — plus a
phone-friendly web interface for running strings, reviewing them, and exporting
the data.

The target is **[SG Timer 2](docs/ROADMAP.md) class**, not CED7000 class: a
timer that can tell your shot from the bay next door, not just one that hears a
bang. Wireless charging is deliberately out of scope; everything else on that
feature list is either built or on the roadmap.

> ⚠️ This is a hobbyist range tool, not a certified match timer. Do not use it
> to score a sanctioned match. Read [_Honest status_](#honest-status) before you
> trust a split it reports.

---

## What it does

| | |
|---|---|
| **Random start delay** | Uniform 1.0–4.0 s by default, from the hardware RNG. Fixed and instant modes too. The remaining delay is never shown on the screen or published over the API — displaying it would hand back the start cue the randomness exists to remove. |
| **Direction gate** | Two microphones 120 mm apart, cross-correlated to measure which direction each bang came from. Off-axis sounds — the next bay — are rejected; anything the gate cannot confidently place is kept. |
| **Echo rejection** | Each shot raises a decaying guard scaled to *its own* peak, so a wall reflection is swallowed while a genuine fast split passes. Better than the fixed blanking window most timers use, because it does not cap your rate of fire. |
| **Impulse (dry-fire) detection** | An optional LIS3DH accelerometer as a second shot source, because a trigger break is a mechanical event and the microphone is worst at exactly that case. Per-profile: microphone, accelerometer, or both. **Only works with the sensor coupled to the firearm** — see [`docs/DETECTION.md`](docs/DETECTION.md). |
| **Profiles** | Six named, editable profiles (Pistol, Rifle, Suppressed, Rimfire, Airsoft/CO2, Dry fire), each carrying its own sensitivity, refractory, echo and direction settings. |
| **The numbers** | First-shot time, every split, total, best and worst split. Milliseconds throughout, displayed in hundredths like every other timer. |
| **Par times** | Up to four par beeps per string, at a distinct higher pitch so they cannot be mistaken for the start signal. |
| **Drills** | Eight named drills — Bill Drill, El Presidente, Doubles, Failure Drill, 1-Reload-1 all pre-seeded — each with its own par schedule and expected round count, plus a per-drill trend of your times. Every stored string is stamped with the drill it was run under. |
| **Battery** | Smoothed reading off the divider, mapped through a real LiPo discharge curve rather than a linear one, on the OLED footer and in the API. |
| **Review** | Scroll the whole string on the OLED, or open it in the web UI. |
| **History** | An append-only log on flash — roughly 2 000 strings — exportable as CSV. |
| **Web UI** | Live clock over a WebSocket, full settings, drills and trends, string history, detector diagnostics, CSV export. Password-protected. |
| **Display** | Contrast control, idle auto-dim, and a 180° flip so the same board works in a front-mount or a top-of-belt mount. |
| **Video sync** | Overlay your shots on video: record in the page and the beep anchors it automatically, or load a clip from your camera app and mark t=0 once. Tap a marker to jump to that shot. |
| **Bluetooth** | Speaks the AMG Lab Commander BLE dialect, so PractiScore and anything else that already talks to a Commander can talk to this. Off by default — see [`docs/BLE_PROTOCOL.md`](docs/BLE_PROTOCOL.md). |
| **Works at a range** | SoftAP by default — ranges do not have Wi-Fi. Joins your network if you configure one, falls back to its own AP if it is not there. |

## Architecture

```
                        ┌──────────────────────────────┐
                        │  Phone / laptop browser      │
                        │  live clock · history · CSV  │
                        └──────────────▲───────────────┘
                                       │ HTTP + WebSocket
                                       │ (SoftAP or your Wi-Fi)
        ┌──────────────────────────────┴───────────────────────────────┐
        │                        ESP32-S3                              │
        │                                                              │
        │   ┌──────────────┐  shot timestamps   ┌──────────────────┐   │
        │   │ ShotDetector │ ──── queue ──────▶ │     TimerApp     │   │
        │   │  prio-10 task│                    │  state machine   │   │
        │   │  I2S · 48 kHz│                    │  delay/beep/par  │   │
        │   │  threshold   │                    └───┬──────┬───────┘   │
        │   │  echo guard  │                        │      │           │
        │   │  direction   │           ┌────────────▼─┐  ┌─▼────────┐  │
        │   └──▲────────▲──┘           │ OLED display │  │ Storage  │  │
        │      │        │              │ 3 buttons    │  │ LittleFS │  │
        │   ┌──┴──┐  ┌──┴──┐           │ piezo buzzer │  └──────────┘  │
        │   │mic A│  │mic B│           └──────────────┘                │
        │   └─────┘  └─────┘  120 mm apart, one shared I2S bus         │
        └──────────────────────────────────────────────────────────────┘
```

The audio path is a dedicated FreeRTOS task at priority 10. It never allocates,
never touches the network, and never waits on the Arduino loop — so a busy web
client cannot cost you a split. Shot times are derived from the I2S **sample
counter**, not from when the DMA block happened to arrive; see
[`docs/DETECTION.md`](docs/DETECTION.md).

## Repository layout

```
firmware/          PlatformIO project (ESP32-S3, Arduino core 3.x)
  src/             Detector, timer state machine, display, storage, web server
    Tdoa.h         Direction-finding maths — no Arduino deps, host-testable
    ShotMerge.h    Two-source merge rule — no Arduino deps, host-testable
  data/            Web UI, uploaded to LittleFS
  include/         Pin map and limits
tools/
  test_tdoa.cpp      Host tests for the direction gate
  test_shotmerge.cpp Host tests for the two-source merge rule
  run_tests.sh
docs/
  ARCHITECTURE.md  How the pieces fit and why they are split that way
  DETECTION.md     The algorithms, their accuracy limits, and how to tune them
  BOM.md           Parts list
  WIRING.md        Pin map and build notes
  API.md           REST + WebSocket reference
  BLE_PROTOCOL.md  The AMG Commander dialect, and what is / isn't confirmed
  ROADMAP.md       The SG Timer 2 scorecard and what is left
  GAMEPLAN.md      The execution plan: workstreams, invariants, checkpoints
  PRIOR_ART.md     What the earlier DELTA/MAK87 build corroborates and offers
hardware/          Enclosure and mounting notes
```

## Quick start

```bash
cd firmware

# 1. Set the first web password. The web UI refuses to start without one, and
#    three buttons are not a keyboard, so it is seeded at build time.
cp include/secrets.example.h include/secrets.h
$EDITOR include/secrets.h          # secrets.h is gitignored

# 2. Build and flash
pio run -t upload

# 3. Upload the web UI to the LittleFS partition
pio run -t uploadfs

# 4. Watch it come up
pio device monitor
```

Then connect a phone to the `ShotTimer-XXXX` access point and open
`http://192.168.4.1`. Change the password from the Settings tab whenever you
like — the stored one always wins over `secrets.h`, so the build-time value only
ever seeds a fresh device.

The web interface being disabled without a password is deliberate: the device's
default network is an open access point, and an unauthenticated timer on an open
AP lets any passer-by fire the buzzer into the middle of someone's string. See
[`docs/API.md`](docs/API.md).

### Tests

```bash
cd tools && ./run_tests.sh
```

Host-side, no board and no toolchain download. Forty assertions over the two
pieces of logic that cannot be debugged at a range without burning ammunition
to discover they were wrong: the direction gate's correlation and geometry, and
the rule that decides whether two sensors saw two shots or one shot twice.

## Controls

Three buttons. `START` is the big one.

| Screen | `START` | `UP` | `DOWN` |
|---|---|---|---|
| Ready | start a string | sensitivity + (hold: menu) | sensitivity − |
| Counting down / running | stop | — | — |
| Review | new string | scroll up (hold: menu) | scroll down |
| Menu | next setting (hold: save & exit) | value + | value − |

The menu leads with **Profile**, because that is the setting that changes the
other settings and the one you touch when you swap guns between stages.

## Honest status

**Built and compile-verified. Never fired at.** The detection stack is
unit-tested against synthetic signals and has never heard a real gun. Treat
every threshold in the shipped profiles as a reasoned starting point, not a
measurement — [`docs/ROADMAP.md`](docs/ROADMAP.md) phase 2 is about replacing
them with real data.

Known limitations, none of them hidden:

- **Shots during a beep are not recorded.** The microphones hear the buzzer
  centimetres away far better than anything downrange, so detection is muted for
  the tone plus 40 ms. Every single-microphone-array timer makes this trade.
- **Impulse detection needs the sensor on the gun.** A timer clipped to your
  belt will not feel a trigger break in a gun held at arm's length, and no
  threshold setting changes that. Live-fire recoil does reach a body-worn
  timer, damped — which is why `Both` mode exists. With no LIS3DH fitted, a
  profile asking for it says so on the screen rather than recording nothing.
- **The direction gate needs two microphones.** With one fitted it detects that
  the second channel is silent and fails open — no rejection, no false
  rejection either. The UI says which state it is in rather than leaving you to
  guess.
- **It is not loud yet.** Commercial timers hit 105–110 dB. The firmware drives
  the piezo correctly; the enclosure acoustics that turn that into volume are
  unbuilt, and that is currently the biggest gap between this and a $130 timer.
- **Absolute latency is uncalibrated.** Split *differences* are good to well
  under a millisecond; the constant offset from acoustic travel and microphone
  group delay is not measured. `micOffsetMs` exists to trim it once you
  characterise it against a reference timer.
- **The BLE layer has never met a client.** It is written from a public
  reference implementation, compiles, and has not been tested against
  PractiScore or a phone. `docs/BLE_PROTOCOL.md` carries the verification
  checklist and marks exactly which fields are confirmed and which are not.
- **In-page video recording needs HTTPS.** The timer serves plain HTTP over its
  own access point, so browsers block `getUserMedia` there. The Video tab
  detects this, says so plainly, and gives you the load-a-file path instead —
  which works everywhere and produces the same overlay.
- **Video marker placement is tens of milliseconds, not sub-millisecond.** It
  depends on the browser aligning a WebSocket event with a video frame. Good for
  watching your draw; the numbers in the table are the device's and are
  unaffected.
- **Not a match timer.** See the warning at the top.

## Licence

MIT — see [`LICENSE`](LICENSE).
