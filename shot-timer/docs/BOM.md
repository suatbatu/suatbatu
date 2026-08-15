# Bill of materials

Prices are rough 2026 hobbyist single-unit, for orientation only.

## Core

| Part | Notes | ≈ Cost |
|---|---|---|
| **ESP32-S3-DevKitC-1** (N8R2 or N16R8) | The partition table assumes **8 MB flash**. Adjust `partitions.csv` for a 4 MB board. S3 over classic ESP32 for the cleaner I2S peripheral and the native USB serial. | $8–12 |
| **2 × I2S MEMS microphone** — see the note below | INMP441 breakouts are the cheap default. Both sit on one bus; the second one costs three solder joints and no GPIO | $6–12 |
| **SSD1306 128×64 OLED**, I2C, 0.96" | White or yellow/blue; any 0x3C module | $4 |
| **Piezo sounder**, 3–5 V, ≥ 95 dB @ 10 cm | A *sounder*, not a bare disc — see below | $2–4 |
| 3 × momentary tactile buttons, 12 mm | One large for START | $2 |
| LiPo 1200–2500 mAh + TP4056 charger, or 18650 + holder | | $8 |
| 2 × 100 kΩ resistors | Battery sense divider | — |
| LED + 330 Ω | Activity indicator | — |
| Enclosure, belt clip | See [`../hardware/README.md`](../hardware/README.md) | — |

## The microphone is the interesting choice

A gunshot at the shooter's position is **150–165 dB SPL**. Essentially no
hobbyist MEMS microphone survives that linearly:

| Part | Acoustic overload point | Reality |
|---|---|---|
| INMP441 | ~120 dB SPL | Clips hard on every centrefire shot |
| ICS-43434 | ~120 dB SPL | Same |
| TDK T5837 | ~133 dB SPL | Better, still clipping |

**This is fine, and the firmware is designed around it.** The detector thresholds
on an envelope and times the *onset*; it never needs an undistorted waveform. A
clipped transient still has a perfectly well-defined leading edge, which is the
only thing being measured.

Where it matters is the roadmap's spectral discrimination work — telling your
shot from the next bay over by its *shape* needs a signal that has a shape. If
you intend to go that way, start with a high-AOP part (T5837) and put an
acoustic resistance — a small port, a layer of felt — in front of it.

Recommendation: **two INMP441 to get running.** Buy a matched pair from the
same batch if you can — the direction gate correlates the two channels against
each other, and while it is amplitude-independent by design, two parts with very
different frequency responses will correlate less strongly and push more
candidates below the confidence floor (where the gate fails open and stops
rejecting anything).

Move to the T5837 if you later want spectral discrimination on top of the
directional gate: telling your shot from another by its *shape* needs a signal
that has a shape, and a clipped one does not.

## The buzzer is the other one

Commercial timers hit **105–110 dB**, and that number is not vanity: you are
competing with ear protection and a busy range. A bare piezo disc soldered to
two wires will not get close, no matter what the firmware does.

What actually gets you there:

- A **piezo sounder** with a built-in resonant cavity, not a raw disc.
- Driving it at **its** resonant frequency — usually 2.7–4.0 kHz. `beepFreqHz`
  defaults to 2700; sweep it and pick the loudest, they vary by part.
- A **Helmholtz cavity** in the enclosure with a port opposite the microphone.
  This is worth more decibels than any electrical change.
- Optionally a step-up driver. At 3.3 V a piezo is well below its rated output;
  most datasheet SPL figures assume 12 V or more.

Measure the result with a phone SPL meter at 30 cm and write the number down.
An unmeasured loudness claim is worth nothing.

## Optional

| Part | For | ≈ Cost |
|---|---|---|
| **LIS3DH accelerometer breakout** | Impulse / dry-fire detection. Shares the display's I2C bus; one extra wire for INT1. Only useful mounted **on the firearm** — see [`DETECTION.md`](DETECTION.md) | $5 |
| Ambient light sensor (BH1750) | Automatic contrast, the last piece of the SG Timer's display story | $3 |
| Piezo step-up driver / transformer | Getting past ~95 dB | $3 |
