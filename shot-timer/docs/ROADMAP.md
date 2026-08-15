# What the good timers do, and how we get there

A survey of the five timers people actually buy, what each is loved for, and
what that means for this project. Sources are listed at the end.

## The field

| Timer | ≈ Price | Why people buy it |
|---|---|---|
| **AMG Lab Commander** | $180 | Official USPSA timer. Clear LCD, up to 3 par times, 105 dB buzzer, 10 mic sensitivity levels, ~17 days on 4×AAA, Bluetooth into the PractiScore apps, genuinely one-handed. |
| **CED7000 (Gen 2)** | $140 | The RO default since 2006. Pocketable, 110+ dB buzzer that cuts through a busy range. Universally understood. Its weakness is the front-mounted display — you unclip it or rotate your wrist to read it. |
| **PACT Club Timer III** | $130 | The value workhorse. Very loud, and a **top-mounted display** you can read by glancing down while it stays on your belt. |
| **Competition Electronics Pro Timer BT** | $160–190 | Bluetooth and a deep feature set aimed at match staff. |
| **Shooters Global SG Timer 2** | $330 | The technical high-water mark. 2.6" 172 PPI front OLED readable in direct sun with a light sensor for auto-backlight, "smart sensor" that isolates *your* shots on a busy indoor range and handles suppressed/CO2/AEG/dry fire, echo filtering, custom acoustic amplifier (loudest made), ~100 h battery with wireless charging, water/dust resistant, BLE + NFC, and an app doing video sync with shot data overlaid, custom drills, and a million-shot history. |

## The features people actually praise, scored against us

| Loved feature | Who is known for it | Where we stand |
|---|---|---|
| Random start delay, fixed & instant modes | all | ✅ Done, from the hardware RNG, and the remaining delay is never displayed |
| First shot, splits, total, review scroll | all | ✅ Done, on device and in the browser |
| 10 sensitivity levels | AMG Commander | ✅ Done — 1–10 over both a ratio and an absolute floor |
| Up to 3–4 par times | AMG Commander | ✅ Done in firmware (4 slots, distinct pitch); only par 1 is editable on the device menu |
| Echo filtering | SG Timer 2 | 🟡 Partial — fixed blanking window; no echo *classification* |
| Suppressed / .22 / airsoft / dry fire | SG Timer 2 | 🟡 The sensitivity table reaches that far in theory; unverified in the field |
| Isolating your shots on a busy range | SG Timer 2 | ❌ **The biggest single gap.** We threshold on level alone |
| 105–110 dB buzzer | CED7000, SG Timer 2 | ❌ Firmware drives a piezo correctly; the acoustics are unbuilt |
| Top-mounted / sun-readable display | PACT, SG Timer 2 | 🟡 OLED is sharp but small; no rotation setting, no ambient sensor |
| Bluetooth → PractiScore | AMG, CE, SG | ❌ We have Wi-Fi + WebSocket instead |
| Video sync with shot overlay | SG Timer 2 | ❌ |
| Custom profiles per firearm/scenario | SG Timer 2 | ❌ One global settings blob |
| Huge session history | SG Timer 2 | 🟡 50 strings + CSV export |
| Days-to-weeks of battery | AMG (17 d), SG (100 h) | ❌ Not addressed. An ESP32-S3 with Wi-Fi up is a different animal from an MSP430 on AAAs |
| Water/dust resistance, survives drops | SG Timer 2 | ❌ No enclosure yet |
| One-handed operation | AMG Commander | ✅ Three buttons, no combos required |

## Plan

### Phase 1 — make it a real timer you can take to a range
The firmware is done; this phase is about the physical thing.

1. **Get loud.** The single biggest gap between this and a $130 timer. Drive a
   proper piezo *sounder* (not a raw disc) at its resonant frequency inside a
   Helmholtz cavity in the enclosure. Target 100 dB at 30 cm. Measure it with a
   phone SPL meter and record the number in `docs/BOM.md` — an unmeasured claim
   is worthless.
2. **Enclosure and mounting.** Belt clip, buttons reachable one-handed, mic port
   away from the buzzer port. Both a front-mounted and a **top-mounted** (PACT
   style) print, since the top mount is the thing PACT owners praise most.
3. **Display rotation setting.** One line — `U8G2_R0` vs `U8G2_R2` — that makes
   the top-mount print usable. Cheapest win on this list.
4. **Battery + charging.** 18650 or a 2000 mAh LiPo with a charger board; wire
   the divider already mapped to `PIN_VBAT_ADC` and show the state of charge.
5. **Field-verify the detection table.** Take it out, run the sensitivity ladder
   against pistol / rifle / suppressed / dry fire, and correct the numbers in
   `Settings::absoluteFloor()` with real data instead of estimates.

### Phase 2 — close the accuracy gap
6. **Calibrate the absolute offset.** Run this timer beside a CED7000 or
   Commander on the same string; the mean difference in first-shot time is
   `micOffsetMs`. Until that is measured, our beep-to-first-shot number carries
   an unquantified few milliseconds of error and the README says so.
7. **Echo classification.** Replace the fixed blanking window with a decaying
   threshold: after a trigger, require any new event to exceed a bar that starts
   high and decays over ~150 ms. A true second shot clears it; a wall reflection
   of the first does not. This lets the blanking window shrink, which raises the
   maximum resolvable rate of fire.
8. **Battery life.** Light-sleep between strings with the detector task still
   running, and drop the Wi-Fi radio when no client is connected. Realistically
   this buys a long day, not the Commander's 17 days — say so rather than
   claiming parity.

### Phase 3 — the SG Timer's headline trick
9. **Reject the neighbouring bay.** This is the feature that justifies the SG
   Timer's price, and the honest options are:
   - **Two microphones.** Two I2S mics 10–15 cm apart give a time-difference of
     arrival; anything off the shooter's axis gets dropped. The ESP32-S3's I2S
     peripheral handles a stereo pair natively and the detector already
     timestamps by sample index, which is exactly what TDOA needs. This is the
     approach with the best accuracy-per-effort.
   - **Spectral gating.** Your own muzzle blast has a characteristic rise time
     and spectral tilt that a bay 15 m away, filtered by distance and air, does
     not. An ESP-DSP FFT on the 8 ms block around each onset, scored against a
     profile captured during a calibration string.
   - Do the two-mic version first; add spectral scoring as a second vote.
10. **Custom profiles.** Once the detector has more knobs than one slider,
    per-firearm profiles in NVS stop being a luxury.

### Phase 4 — the ecosystem
11. **BLE + PractiScore.** The AMG/CE/SG protocol is what PractiScore Log talks
    to. Emulating it would let this timer drop into an existing workflow — the
    single highest-leverage software feature left, and it needs the wire format
    reverse-engineered from the PractiScore community docs first.
12. **Video sync.** We already timestamp every shot against a monotonic clock.
    Recording a phone video and overlaying shot markers is a web-UI feature, not
    a firmware one: capture with `getUserMedia`, anchor on the beep event, and
    render the splits over the playback timeline.
13. **Drills.** Par-time sequences (Bill drill, El Presidente, 1R1) stored as
    named schedules, since the par machinery already accepts four beeps.

## Where the effort actually pays

If only three things get built: **the buzzer acoustics** (phase 1.1), **the
top-mount enclosure with display rotation** (1.2–1.3), and **the two-microphone
shot isolation** (9). The first two are what separate a breadboard from a timer
you would clip to a belt; the third is the one feature a $330 timer has that a
$130 timer does not.

---

Sources:
[Lynx Defense](https://lynxdefense.com/best-shot-timers/) ·
[Gun University](https://gununiversity.com/best-shot-timers/) ·
[American Firearms](https://americanfirearms.org/best-shot-timers/) ·
[Rifle Configurator](https://www.rifleconfigurator.com/guides/best-shot-timer) ·
[AMG Lab](https://www.amg-lab.com/) ·
[Ben Stoeger Pro Shop — AMG Lab](https://benstoegerproshop.com/amg-lab/) ·
[TFB: SG Timer 2 review](https://www.thefirearmblog.com/blog/tfb-review-shooters-global-sg-timer-2-this-beeper-is-a-keeper-44821324) ·
[Shooters Global](https://timer.shooters.global/product/sg-timer-2/) ·
[USA Carry: SG Timer 2 & GO](https://www.usacarry.com/sg-timer-2-go-shooters-global-review/) ·
[PractiScore community](https://community.practiscore.com/t/amg-commander-shot-timer-and-bluetooth-connection/653)
