# Target: SG Timer 2 class

The bar for this project is the **Shooters Global SG Timer 2** — the most
capable shot timer on the market at roughly $330 — **excluding wireless
charging**, which is convenience rather than capability and costs board area we
would rather spend on the battery.

That is a deliberately hard target. It means beating the CED7000 and the AMG
Commander on detection quality, not just matching them on features.

## Scorecard

| SG Timer 2 does | We do | Status |
|---|---|---|
| Smart sensor: isolates *your* shots on a busy range | Two-microphone cross-correlation direction gate, fails open on low confidence | ✅ **built** (host-tested; needs field validation) |
| Echo filtering | Decaying per-shot guard scaled to that shot's own peak, not a fixed blanking window | ✅ **built** |
| Detects AEG, CO2, suppressed, rimfire, dry fire | Sensitivity 1–10 over a ratio *and* an absolute floor | ✅ built, ⚠️ thresholds not yet measured against real guns |
| Custom profiles per caliber/scenario | Six named, editable profiles carrying all four detection parameters | ✅ **built** |
| Sun-readable display, auto-backlight via light sensor | 128×64 OLED, contrast control, idle auto-dim, 180° flip for top-of-belt | 🟡 no ambient sensor yet |
| Session history, 1 000 000 shots | Append-only log, compacted at 1.2 MB, ~2 000 strings, CSV export | 🟡 large, not *that* large |
| Remote control from the phone | Full web UI over a WebSocket | ✅ built |
| 110 dB acoustic amplifier, loudest made | Firmware drives the piezo correctly at a configurable resonance | ❌ **acoustics unbuilt — the biggest gap** |
| ~100 h battery | Battery *monitoring* built; power management not addressed | 🟡 |
| Water and dust resistant, survives drops | No enclosure yet | ❌ |
| Bluetooth + NFC, PractiScore integration | AMG Commander BLE dialect over Nordic UART, plus Wi-Fi + WebSocket | 🟡 **built, never tested against a client** |
| Video sync with shot data overlaid | Record in-page (anchored on the beep) or load a clip and mark t=0; tappable shot markers on a timeline | ✅ **built** — in-page capture needs HTTPS, the file path does not |
| Custom drills + community library | Eight named drills with par schedules, expected round counts, and per-drill trends | ✅ **built** (no community library) |
| Wireless charging | — | 🚫 **out of scope by decision** |

## Phase 1 — the physical timer (the gap that matters most now)

The firmware is ahead of the hardware. Everything here is soldering and
printing, and none of it can be simulated.

1. **Get loud.** A $130 PACT is louder than this will be on a breadboard, and
   loudness is not a nice-to-have when the shooter is wearing ear protection.
   Piezo *sounder* in a Helmholtz cavity, driven at the resonance the cavity
   actually has (sweep `beepFreqHz` with the box closed). Target 100 dB at
   30 cm, **measured** with an SPL meter and recorded in `BOM.md`. Consider a
   step-up driver: at 3.3 V a piezo is far below its datasheet output.
2. **Build the two-microphone array.** The firmware is ready and the geometry is
   tested; what is missing is two ports 120 mm apart on a horizontal line,
   pointed at the shooter, acoustically isolated from the buzzer port. Set
   `micSpacingMm` to what you actually built, not what you intended.
3. **Enclosure, in both orientations.** Front-mount (CED7000 style) and
   top-mount (PACT style). The firmware flip already exists; the print does not.
   Belt clip, START distinguishable by feel, USB-C reachable without
   disassembly.
4. **Battery and charging.** 18650 or 2000 mAh LiPo, TP4056, and wire the
   divider already mapped to `PIN_VBAT_ADC`. Show state of charge.
5. **Weather and drop tolerance.** Gaskets, labyrinth ports, and a case that
   survives gravel. SG Timer owners rate this highly and it is pure mechanical
   work.

## Phase 2 — validate what is built

The detector is written and unit-tested. It has never heard a gun.

6. **Field-validate the sensitivity table.** Run the ladder against pistol,
   rifle, suppressed, rimfire and dry fire; replace the estimated numbers in
   `Settings::absoluteFloor()` and the shipped profiles with measurements.
7. **Field-validate the direction gate.** Two shooters, adjacent bays, both
   firing. Count what the gate accepts and rejects against the truth. Tune the
   acceptance angle and the 35% confidence floor from that data, not from
   theory.
8. **Calibrate the absolute offset.** Run beside a CED7000 or Commander on the
   same string; the mean difference in first-shot time is `micOffsetMs`. Until
   this is done the beep-to-first-shot number carries a few milliseconds of
   unquantified error, and the README says so.

## Phase 3 — close the remaining SG features

9. **Battery life.** Monitoring is built (smoothed ADC, real LiPo curve, shown
   on the OLED and in the API); *management* is not. Light-sleep between strings
   with the detector task still running; drop the Wi-Fi radio when no client is
   connected — and now also weigh whether BLE and Wi-Fi should both be up. The honest
   expectation is a long day, not the SG's 100 hours — an ESP32-S3 with a radio
   is a different animal. Report what is measured.
10. **Ambient light sensor** (BH1750 on the existing I2C bus) driving contrast
    automatically, which is the last piece of the SG's display story that is not
    just panel choice.
11. ✅ **Bluetooth LE + PractiScore — built.** The wire format is documented in
    [`BLE_PROTOCOL.md`](BLE_PROTOCOL.md) from a public reference implementation,
    and the firmware serves it. What remains is not code: pair a phone with
    PractiScore Log and confirm the times match. Until that happens this is
    "built, unproven" and the scorecard says so.
12. ✅ **Video sync — built.** Both paths: `getUserMedia` + `MediaRecorder`
    anchored on the `beep` event where the browser allows it, and a load-a-file
    path with a one-tap manual anchor where it does not (which is the normal
    case on the device's own plain-HTTP access point). Serving the UI over
    HTTPS would make path A the default — a possible future step, and one that
    costs a certificate story on a device with no clock.
13. ✅ **Drills — built.** Eight named drills with par schedules, expected round
    counts, per-drill trend charts, and a drill stamp on every stored string. A
    shared community library is the only part not done, and it needs somewhere
    to share *to* — a decision for later.
14. **Bigger history.** If 2 000 strings is not enough, move from NDJSON to a
    packed binary record; the same partition would hold an order of magnitude
    more.

## What is deliberately not on this list

- **Wireless charging.** Excluded by decision. USB-C is not a hardship.
- **NFC pairing.** It exists on the SG Timer to make Bluetooth pairing painless.
  Our equivalent — join an access point, open a page — is already no worse.

## Where the remaining effort actually pays

Everything in phase 1, and it is now the *only* thing that pays. The software
side of this scorecard is largely done: detection, profiles, drills, history,
web UI and BLE are all built. Every one of them is unproven, and no further
firmware changes that. Build the array, build the enclosure, make it loud, pair
a phone — then let phase 2 tell you which of these numbers were wrong.

---

Sources for the comparison:
[Lynx Defense](https://lynxdefense.com/best-shot-timers/) ·
[Gun University](https://gununiversity.com/best-shot-timers/) ·
[Rifle Configurator](https://www.rifleconfigurator.com/guides/best-shot-timer) ·
[AMG Lab](https://www.amg-lab.com/) ·
[TFB: SG Timer 2 review](https://www.thefirearmblog.com/blog/tfb-review-shooters-global-sg-timer-2-this-beeper-is-a-keeper-44821324) ·
[Shooters Global](https://timer.shooters.global/product/sg-timer-2/) ·
[USA Carry: SG Timer 2 & GO](https://www.usacarry.com/sg-timer-2-go-shooters-global-review/) ·
[PractiScore community](https://community.practiscore.com/t/amg-commander-shot-timer-and-bluetooth-connection/653)
