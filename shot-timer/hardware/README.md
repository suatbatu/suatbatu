# Hardware

No enclosure files here yet — this directory is the plan and the constraints, so
that whoever models it (including future us) does not have to re-derive them.

## What the box has to do

In rough order of how much it affects whether the thing is usable:

1. **Be loud.** The enclosure is part of the buzzer. A piezo sounder in a sealed
   box is quiet; the same sounder over a tuned cavity with a port is
   dramatically louder. This is the difference between a timer you can hear over
   ear protection and one you cannot. See [`../docs/BOM.md`](../docs/BOM.md).
2. **Keep the microphone port away from the buzzer port.** Opposite faces if
   possible. The firmware mutes detection while the beep sounds, and shots
   fired inside that window are lost — the further apart the ports, the shorter
   that window can safely be.
3. **Clip to a belt, and be readable there.** This is the thing PACT owners
   praise and CED7000 owners complain about.
4. **Be operable one-handed, without looking.** START distinguishable by feel.
5. **Survive the range.** Dropped on gravel, rained on, packed in a bag.

## Two display orientations

Worth printing both:

- **Front-mounted** (CED7000 style) — read it held in the hand, RO fashion.
- **Top-mounted** (PACT style) — read it by glancing down while it stays clipped
  to your belt. This is the one people rave about.

The top mount needs the OLED rotated 180°, which is a one-word change in
`Display.cpp` (`U8G2_R0` → `U8G2_R2`). Making it a setting is
[`../docs/ROADMAP.md`](../docs/ROADMAP.md) phase 1.3.

## Acoustic notes

- Microphone port: 1–2 mm hole, acoustic mesh behind it, no cavity in front.
  Never seal a MEMS mic in — it needs a path to the outside air.
- Buzzer port: sized against the sounder's resonant frequency. Sweep
  `beepFreqHz` with the box closed and pick the loudest; the cavity shifts the
  peak away from the bare sounder's rated frequency.
- Both ports want a rain baffle. A short labyrinth costs a decibel or two and
  buys weather tolerance.

## Layout constraints

- The mic sits as far from the piezo as the box allows, ideally with the PCB
  between them.
- Buttons on the face you can reach with the thumb of the hand holding it.
- USB-C reachable without disassembly — it is how you flash and charge.
- Battery is the biggest single volume; plan around it first.

## Prior art worth measuring against

| | Size | Weight | Notes |
|---|---|---|---|
| CED7000 | pocketable, front display | very light | The size everyone else is judged against |
| PACT Club Timer III | larger, top display | light | The readable-on-the-belt one |
| SG Timer 2 | 2.6" screen, water/dust resistant | heavier | Wireless charging, survives drops |

Getting inside CED7000 dimensions on a first print is unlikely — an ESP32-S3
DevKitC and an 18650 are simply bigger than a purpose-built PCB and 4 AAAs. Aim
for usable and loud first, small later.
