# shot-timer 🎯

A DIY **shooting-sports shot timer** on an ESP32-S3: random start beep, acoustic
shot detection, split times, par beeps — plus a phone-friendly web interface for
running strings, reviewing them, and exporting the data.

It does the job a CED7000 or a PACT Club Timer does on the belt, and then hands
you the numbers in a browser instead of a four-digit LCD.

> ⚠️ This is a hobbyist range tool, not a certified match timer. Do not use it
> to score a sanctioned match. Read [_Honest limitations_](#honest-limitations)
> before you trust a split it reports.

---

## What it does

| | |
|---|---|
| **Random start delay** | Uniform 1.0–4.0 s by default, from the hardware RNG. Fixed and instant modes too. The remaining delay is never shown on the screen or published over the API — displaying it would hand back the start cue the randomness exists to remove. |
| **Acoustic shot detection** | I2S MEMS microphone, per-sample envelope follower with an adaptive noise floor, 10 sensitivity levels, and a configurable blanking window that kills echo double-counts. |
| **The numbers** | First-shot time, every split, total time, best and worst split. Milliseconds throughout, displayed in hundredths like every other timer. |
| **Par times** | Up to four par beeps per string, at a distinct higher pitch so they cannot be mistaken for the start signal. |
| **Review** | Scroll the whole string on the OLED, or open it in the web UI. |
| **History** | The last 50 strings on flash, survivng power loss, exportable as CSV. |
| **Web UI** | Live clock over a WebSocket, full settings, string history, CSV export. Password-protected. |
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
        │   │  I2S · 32 kHz│                    │  delay/beep/par  │   │
        │   └──────▲───────┘                    └───┬──────┬───────┘   │
        │          │                                │      │           │
        │     ┌────┴────┐              ┌────────────▼─┐  ┌─▼────────┐  │
        │     │ MEMS mic│              │ OLED display │  │ Storage  │  │
        │     └─────────┘              │ 3 buttons    │  │ LittleFS │  │
        │                              │ piezo buzzer │  └──────────┘  │
        │                              └──────────────┘                │
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
  data/            Web UI, uploaded to LittleFS
  include/         Pin map and limits
docs/
  ARCHITECTURE.md  How the pieces fit and why they are split that way
  DETECTION.md     The acoustic algorithm, timing accuracy, and how to tune it
  BOM.md           Parts list
  WIRING.md        Pin map and build notes
  API.md           REST + WebSocket reference
  ROADMAP.md       What the commercial timers do well, and the plan to match it
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

## Controls

Three buttons. `START` is the big one.

| Screen | `START` | `UP` | `DOWN` |
|---|---|---|---|
| Ready | start a string | sensitivity + (hold: menu) | sensitivity − |
| Counting down / running | stop | — | — |
| Review | new string | scroll up (hold: menu) | scroll down |
| Menu | next setting (hold: save & exit) | value + | value − |

## Honest limitations

- **Shots during a beep are not recorded.** The microphone hears the buzzer
  centimetres away far better than anything downrange, so detection is muted for
  the tone plus 40 ms. Every single-microphone timer makes this trade.
- **A neighbouring bay can trigger it.** The detector thresholds on level, not on
  where a sound came from. Sensitivity and the adaptive noise floor help;
  they do not make it immune. The SG Timer's "smart sensing" is the thing to
  beat here — see [`docs/ROADMAP.md`](docs/ROADMAP.md).
- **Loudness is a hardware problem.** Commercial timers hit 105–110 dB. A bare
  piezo on a breadboard will not, and you will not hear it over ear protection.
  The enclosure and transducer choice matter more than the firmware here.
- **Absolute latency is uncalibrated.** Split *differences* are good to well
  under a millisecond; the constant offset from acoustic travel time and
  microphone group delay is not measured. `micOffsetMs` exists to trim it if you
  ever characterise it against a reference timer.
- **Not a match timer.** See the warning at the top.

## Licence

MIT — see [`LICENSE`](LICENSE).
