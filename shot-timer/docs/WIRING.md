# Wiring

Board: **ESP32-S3-DevKitC-1**. Pin numbers are GPIO numbers and live in one
place — [`firmware/include/config.h`](../firmware/include/config.h). Change them
there, not here.

## Pin map

| Function | GPIO | Notes |
|---|---|---|
| I2S mic BCLK (SCK) | 15 | |
| I2S mic WS (LRCL) | 16 | |
| I2S mic DIN (SD) | 17 | |
| I2S mic L/R | — | **Tie to GND.** The firmware reads the left slot only |
| I2C SDA | 8 | ESP32-S3 default, so `Wire` needs no persuading |
| I2C SCL | 9 | |
| Piezo + | 4 | LEDC channel; piezo − to GND |
| Button START | 5 | To GND, internal pull-up |
| Button UP | 6 | To GND, internal pull-up |
| Button DOWN | 7 | To GND, internal pull-up |
| Battery sense | 1 | ADC1, via a 100 k / 100 k divider from VBAT |
| Activity LED | 21 | Through 330 Ω to GND |

Avoid GPIO 0, 19, 20, 43, 44, 45, 46 and the 26–32 range on the S3 — they are
strapping pins, USB, or wired to internal flash/PSRAM.

## Microphone

```
INMP441          ESP32-S3
────────         ────────
VDD      ─────── 3V3
GND      ─────── GND
L/R      ─────── GND        ← left slot; the firmware assumes this
WS       ─────── GPIO 16
SCK      ─────── GPIO 15
SD       ─────── GPIO 17
```

Physical placement matters more than the wiring:

- **Point the microphone at the shooter**, not downrange. You want the muzzle
  blast; you want the neighbouring bays off-axis.
- **Keep it away from the buzzer port**, on the opposite face if you can. The
  firmware mutes detection during a beep, and the further apart they are, the
  shorter that mute window can safely be.
- **Do not seal it in.** A MEMS mic needs a port to the outside. A 1–2 mm hole
  with a scrap of acoustic mesh behind it is right; a layer of felt also helps
  the clipping situation described in [`BOM.md`](BOM.md).

## Buzzer

```
GPIO 4 ──────┬──── piezo +
             │
        (optional) 100 Ω in series to tame the current spike
piezo − ───────── GND
```

A piezo is capacitive, so the LEDC edge produces a current spike. If the 3V3
rail sags enough to reset the board when the buzzer fires — it happens on thin
breadboard wiring — add 100 Ω in series and 100 µF across the rail near the
sounder.

See [`BOM.md`](BOM.md) on why the enclosure cavity matters more than any of this.

## Buttons

All three go to GND, with `INPUT_PULLUP` handling the rest. No external
resistors, no debounce hardware — 25 ms of software debounce and a 700 ms
long-press threshold are in `Buttons.cpp`.

Make START physically distinguishable: bigger, or a different colour, or set
apart. It gets pressed with the timer held one-handed, often without looking.

## Battery sense

```
VBAT ──[100k]──┬──[100k]── GND
               │
            GPIO 1
```

The divider halves the voltage so a 4.2 V cell reads 2.1 V, inside ADC1's range.
`VBAT_DIVIDER_RATIO` in `config.h` is the compensating factor. Note that the
firmware maps the pin but does not yet read it — that is
[`ROADMAP.md`](ROADMAP.md) phase 1.4.

## First power-up checklist

1. `pio device monitor` — you should see `shot-timer v0.1.0 ready on …`.
2. No OLED? The log says `no OLED found — running headless`; check the 0x3C
   address and the SDA/SCL pair.
3. `FATAL: microphone/I2S init failed` blinks the LED once a second. A timer
   that cannot hear is not a timer, so it refuses to pretend otherwise.
4. Open the web UI's Settings tab and watch the level meter. Tap the microphone
   port — the bar should jump and settle. If it sits pegged, the mic is wired
   to the wrong slot or L/R is floating.
