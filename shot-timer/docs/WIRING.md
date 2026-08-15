# Wiring

Board: **ESP32-S3-DevKitC-1**. Pin numbers are GPIO numbers and live in one
place — [`firmware/include/config.h`](../firmware/include/config.h). Change them
there, not here.

## Pin map

| Function | GPIO | Notes |
|---|---|---|
| I2S BCLK (SCK) | 15 | Shared by both microphones |
| I2S WS (LRCL) | 16 | Shared by both microphones |
| I2S DIN (SD) | 17 | Shared by both microphones |
| Mic A L/R | — | **Tie to GND** — mic A transmits in the left slot |
| Mic B L/R | — | **Tie to 3V3** — mic B transmits in the right slot |
| I2C SDA | 8 | ESP32-S3 default, so `Wire` needs no persuading |
| I2C SCL | 9 | |
| Piezo + | 4 | LEDC channel; piezo − to GND |
| Button START | 5 | To GND, internal pull-up |
| Button UP | 6 | To GND, internal pull-up |
| Button DOWN | 7 | To GND, internal pull-up |
| Battery sense | 1 | ADC1, via a 100 k / 100 k divider from VBAT |
| LIS3DH INT1 | 14 | Optional accelerometer; interrupt only, data is on I2C |
| Activity LED | 21 | Through 330 Ω to GND |

Avoid GPIO 0, 19, 20, 43, 44, 45, 46 and the 26–32 range on the S3 — they are
strapping pins, USB, or wired to internal flash/PSRAM.

## Microphone array

Two microphones share one I2S bus. They are told apart by their L/R pin, not by
wiring — so the second microphone costs three solder joints and no GPIO.

```
  mic A (left)                ESP32-S3               mic B (right)
  ────────────                ────────               ─────────────
  VDD ─────────────────────── 3V3 ─────────────────────────── VDD
  GND ─────────────────────── GND ─────────────────────────── GND
  L/R ─── GND                                        3V3 ─── L/R
  SCK ─────────────────────── GPIO 15 ─────────────────────── SCK
   WS ─────────────────────── GPIO 16 ─────────────────────── WS
   SD ─────────────────────── GPIO 17 ─────────────────────── SD
```

**One microphone works too.** The right slot then reads as silence, the firmware
detects that no second microphone is present, and the direction gate fails open
— no rejection, and no false rejection either. The web UI says which state it is
in.

### Geometry is the whole feature

The direction gate measures the difference in arrival time between the two
microphones, so how you mount them *is* the algorithm:

- **120 mm apart** by default, on a **horizontal line**, both facing the
  shooter. Set `micSpacingMm` to what you actually built — measure between the
  two **ports**, not the breakout boards.
- Wider spacing means more samples of difference per degree, so a finer gate. It
  also means a bigger box. 120 mm gives 17 samples across the full 0–90° range
  at 48 kHz, which is enough for a 25° acceptance window.
- Both ports must see the same acoustic environment. One behind a screw boss and
  one in open air will correlate badly and the gate will fail open.
- **Point the array at the shooter**, not downrange. That puts your muzzle blast
  at 0° and the neighbouring bays out at 40–80°, which is exactly the separation
  the gate needs.

### Both microphones

- **Keep them away from the buzzer port**, on the opposite face if you can. The
  firmware mutes detection during a beep, and the further apart they are, the
  shorter that mute window can safely be.
- **Do not seal them in.** A MEMS mic needs a port to the outside. A 1–2 mm hole
  with a scrap of acoustic mesh behind it is right; a layer of felt also helps
  the clipping situation described in [`BOM.md`](BOM.md).

## Accelerometer (optional)

For impulse / dry-fire detection. It shares the display's I2C bus, so it costs
one extra wire.

```
LIS3DH           ESP32-S3
──────           ────────
VIN      ─────── 3V3
GND      ─────── GND
SCL      ─────── GPIO 9      (shared with the OLED)
SDA      ─────── GPIO 8      (shared with the OLED)
INT1     ─────── GPIO 14     ← this is what carries the timing
SA0      ─────── float/GND (0x18) or 3V3 (0x19); both are probed
```

The firmware probes both addresses at boot. If nothing answers `WHO_AM_I` with
`0x33`, impulse detection reports itself unavailable and the acoustic path is
unaffected — a build without this part is a supported configuration, not a
fault.

**Mounting is the whole feature.** The sensor must be mechanically coupled to
the firearm to feel a trigger break; on a belt it will feel nothing. Either
mount the timer on the gun, or put the LIS3DH on a short cable as a separate
puck. See [`DETECTION.md`](DETECTION.md).

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
`VBAT_DIVIDER_RATIO` in `config.h` is the compensating factor.

The firmware reads it: oversampled, smoothed, and mapped through a real LiPo
discharge curve rather than a linear one. Below 2.5 V it reports "no battery"
rather than 0%, so a board on USB does not display a fictional flat cell.

## First power-up checklist

1. `pio device monitor` — you should see `shot-timer v… ready on …`.
2. No OLED? The log says `no OLED found — running headless`; check the 0x3C
   address and the SDA/SCL pair.
3. `FATAL: microphone/I2S init failed` blinks the LED once a second. A timer
   that cannot hear is not a timer, so it refuses to pretend otherwise.
4. Open the web UI's Settings tab and watch the level meter. Tap a microphone
   port — the bar should jump and settle. If it sits pegged, a mic is wired to
   the wrong slot or an L/R pin is floating.
5. If a LIS3DH is fitted, the boot log says `LIS3DH at 0x18` (or `0x19`). Tap
   the sensor sharply and "Impulse events" in the Detector panel should climb.
6. Check the **Detector** panel. With both microphones fitted, "Second
   microphone" should read *detected*. Clap once directly in front of the array:
   "Last angle" should read near 0° and "Last correlation" above 50%. Clap from
   one side and the angle should swing out. If it does not, `micSpacingMm` does
   not match the hardware, or the ports are not on the axis you think they are.
