# Mechanical design contract

**You design and model the physical parts.** This document only pins down the
interface the firmware assumes, so your mechanism and the code agree. As long as
your design satisfies these points, the firmware doesn't care how it looks.

## The mechanism the firmware expects

A **rotating carousel**: a disc of `SLOTS` compartments turning under a fixed
lid that has exactly **one opening**, plus a **drop chute** to an external cup.

```
        fixed lid (one opening only)
                 │
   ┌───────┬─────▼─────┬───────┐
   │ slot  │  SLOT AT  │ slot  │   ← only the slot under the opening
   │  n-1  │  OPENING  │  n+1  │     is ever reachable  ⇒ geometry = the "lock"
   └───────┴─────┬─────┴───────┘
                 ▼ drop chute
             ┌───────┐
             │  cup  │ ← IR beam across chute; switch under cup
             └───────┘
```

## Requirements

1. **Single access opening.** Only the compartment currently indexed to the
   opening may be reachable. Every other compartment stays covered. This is what
   prevents double-dosing — it must be true mechanically, not in software.

2. **`SLOTS` compartments, evenly spaced.** The firmware advances exactly
   `360° / SLOTS` per dose. Set `SLOTS` identically in
   `firmware/dispenser/include/config.h` (`CAROUSEL_SLOTS`) and in your model.
   A slot must hold the **largest single dose** (all pills for one time) with
   room to fall freely.

3. **A homing mark at slot 0.** Mount a small magnet on the disc so the
   **hall sensor** (see `docs/WIRING.md`) triggers when compartment 0 is at the
   opening. On boot the firmware rotates until it sees this mark to establish
   absolute position. Without it, position is lost on every reboot.

4. **A drop path past the IR break-beam.** Pills must fall through a chute where
   the beam can see them, so a dose can be confirmed as actually dispensed.
   Avoid ledges where a pill can hang up before the beam.

5. **A cup that triggers the "taken" switch** (or an easily pressed button
   beside it). Lifting/emptying the cup — or pressing the button — is how the
   person confirms the dose was collected.

6. **Openable by hand for refill and emergencies.** The uncle reloads weekly;
   and if the electronics fail, a human must still be able to open it and get
   the medicine out. **Never make a box that can trap the pills.**

7. **Camera framing.** If using the camera node, provide a mount that frames the
   **cup / drop area** — the pills — not the room. See `docs/SECURITY.md`.

## Parameters to keep in sync

| Your model | Firmware (`config.h`) | Meaning |
|---|---|---|
| number of compartments | `CAROUSEL_SLOTS` | must match exactly |
| gear ratio / steps per full turn | `STEPS_PER_REV` | steps for a 360° disc rotation |
| homing magnet at slot 0 | `PIN_HALL_HOME` | index reference |
| motor direction | `CAROUSEL_DIR` | flip if it rotates the wrong way |

## Suggestions (non-binding)

- Print compartment-side parts in **PETG**; use a food-safe cup/insert for
  direct pill contact.
- Give compartments a slight taper toward the drop hole so pills don't wedge.
- A light detent or low-backlash hub keeps slot alignment repeatable; the
  hall-sensor homing corrects accumulated error each boot regardless.
