# Prior art: the DELTA / MAK87 build

In June 2026 the same owner built a **different** shot timer — `MAK87_ShotTimer`,
branded DELTA — on a Mac with arduino-cli, plus a native iOS app. This project
is not that one, shares no code with it, and was written without knowledge of
it. Its handoff document surfaced afterwards.

That makes it unusually valuable: two independent designs of the same device,
which is the closest thing to a second opinion this project is going to get.
This file records what that comparison is worth. It is a findings document, not
a plan — anything adopted from here becomes a numbered item in
[`ROADMAP.md`](ROADMAP.md) or [`GAMEPLAN.md`](GAMEPLAN.md) first.

**The DELTA source is not available to this project.** It lives on a Mac, was
never committed to git, and none of its files can be read from here. Everything
below is from its handoff summary alone.

## 1. Where the two designs independently agreed

Convergence is evidence. Four decisions were made twice, separately, the same
way:

| Decision | Why it matters that it happened twice |
|---|---|
| **AMG emulation over a custom BLE protocol** | Both builds concluded PractiScore compatibility is the credibility gate. Ours is [`BLE_PROTOCOL.md`](BLE_PROTOCOL.md); theirs is a separate reverse-engineering effort reaching the same 14-byte centisecond frames over Nordic UART. Two independent derivations agreeing is the strongest signal either has — neither has been tested live. |
| **Second microphone on the same I2S bus, `L/R` → 3V3** | Identical wiring trick, arrived at separately. Ours implements the dual-mic direction gate; theirs reserved the slot for it. |
| **Detection core as a pure header shared with host tests** | They have `ShotDetectorCore.h` + 11 native sim scenarios; we have [`Tdoa.h`](../firmware/src/Tdoa.h) + 25 assertions. Same instinct: the part that cannot be debugged at a range gets tested on a desk. |
| **BLE advertisement overflows the 31-byte packet** | Their notes carry an "adv-overflow fix" as a known high-severity item. We hit the same constraint and put the service UUID in the advertisement with the name in the scan response. Independent corroboration that this is a real trap, not a theoretical one. |

## 2. What DELTA has that this project does not

Ranked by what they would actually be worth here. **None of these are committed
to** — they are candidates.

1. **IMU dry-fire detection (LIS3DH).** The strongest idea in the document.
   Dry fire is the case our acoustic detector handles worst — sensitivity 10
   means trusting a click barely above room noise, which is also where every
   false trigger lives. An accelerometer on the frame sidesteps the problem
   entirely: a trigger break is a mechanical event. This would make the
   "Dry fire" profile genuinely reliable rather than merely present, and it is
   additive — the detector keeps working, the IMU becomes a second opinion.
2. **OTA firmware update over Wi-Fi.** Our [`partitions.csv`](../firmware/partitions.csv)
   already reserves `app0` and `app1` — two 3 MB OTA slots — and nothing uses
   them. Once the device is inside a sealed, weather-resistant enclosure, USB-C
   access stops being convenient, and this stops being a nicety.
3. **Audio capture to LittleFS as WAV, replayed as regression fixtures.** Their
   phase-2 plan is: record real gunshots on the device, tune the presets against
   those files, then freeze the captures as test inputs. This is the
   *right* answer to our
   [`ROADMAP.md`](ROADMAP.md) phase 2 — it turns a one-off range day into a
   permanent test suite, so a future detector change can be checked against real
   audio instead of synthetic transients. Note that a host audio simulator was
   offered to the owner earlier and declined; this is the same idea arriving
   from a more convincing direction, and worth re-asking about after the first
   range day rather than before.
4. **Visual start signal.** Flash the display as the start cue. Obvious value
   for a shooter in serious ear protection, and near-free on our OLED. It would
   need the same treatment as the beep: the flash is `t = 0`, and the countdown
   still must not be visible before it.
5. **Multiple beep profiles.** Ours has one tone with frequency, length and
   volume. Theirs ships four. Cheap, and useful when two timers are running in
   adjacent bays and both shooters need to know whose beep just fired.

## 3. Where this project made the better call

Recorded so nobody "harmonises" them backwards later.

- **The advertised BLE name.** DELTA advertises `AMG LAB COMM <id>` — a
  competitor's trade name verbatim — and its own notes flag that as needing a
  legal review before shipping default-on. We advertise **`Commander shot-timer`**:
  it clears the same name filter clients use, while stating what the device
  actually is, and BLE is **off by default**. That is the safer position and it
  should stay. See [`BLE_PROTOCOL.md`](BLE_PROTOCOL.md).
- **It is in version control.** DELTA's first listed next step is `git init`; its
  handoff notes "nothing is committed anywhere". Two months of work sitting on
  one laptop is the single largest risk in that document.
- **Core assignment.** DELTA runs detection on core 0, which on the ESP32 is
  where the Wi-Fi and Bluetooth stacks live. We pin the detector to core 1
  alongside the Arduino loop, at priority 10 so it preempts it, leaving core 0
  to the radios. Now that BLE is also running, that separation matters more, not
  less — though it is worth *measuring* rather than assuming, once hardware
  exists.

## 4. Hardware notes worth carrying over

- **DELTA targets ESP32-WROOM-32 with a colour TFT** (2.4" ILI9341 or 2.0"
  ST7789); we target ESP32-S3 with a 128×64 OLED. Different devices, not
  competing revisions of one.
- **Partition warning, confirmed the hard way.** DELTA needed the `min_spiffs`
  scheme because *"BLE+WiFi wouldn't fit default"*, and still lands at 69% of it.
  We sit at 44.9% of a 3 MB app slot on 8 MB flash, so we have room — but
  [`BOM.md`](BOM.md)'s note about adapting `partitions.csv` for a 4 MB board is
  now a firm warning rather than a footnote: with both radios, a 4 MB board is
  tight.
- **Sourcing is Turkish.** DELTA's BOM totals ≈ ₺900–1,000 core, +₺385 power,
  +₺200–260 for the smart add-ons (second mic, LIS3DH, transducer). Our BOM is
  in USD and should probably carry a lira column, since that is where the parts
  are actually being bought.

## 5. The question this raises

There are now two shot timer projects by the same owner, each with things the
other lacks. That is a decision for the owner, not a technical problem to solve
quietly — recorded here so the next session asks rather than assumes:

- Is DELTA superseded by this build, a parallel track, or the branded product
  this one feeds into?
- If DELTA continues, does its **brand** (DELTA, cyan `#22D3C5` on graphite
  `#14181C`) apply here? Our web UI currently uses its own dark palette.
- Its native **iOS app** already decodes AMG frames. If that app is alive, our
  BLE layer has a ready-made test client — which would close the D4 checkpoint
  far faster than waiting on PractiScore.
