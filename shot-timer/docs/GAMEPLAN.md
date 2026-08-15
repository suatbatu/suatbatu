# Gameplan

The working plan for the next stretch of this project. Written as a handoff:
any session (human or Claude) should be able to pick this up, read it top to
bottom, and know what to do next and what not to touch.

**Target:** SG Timer 2 class, minus wireless charging. See
[`ROADMAP.md`](ROADMAP.md) for the feature scorecard; this document is the
*execution* plan.

**Decisions already made** (do not re-litigate without the owner asking):

- Hardware **is being built** — plan hardware steps with owner checkpoints.
- Active software tracks, chosen by the owner, **in this order**:
  **A (hardware) → B (drills) → D (BLE + PractiScore) → C (video sync)**.
  (A host audio simulator was offered and not chosen; do not build it unless
  asked.)
- **CI is in**, at `.github/workflows/ci.yml`.
- The project moves to a **standalone GitHub repo** once the owner creates it
  (the CI token cannot — repo creation returns 403).

---

## Current state (v0.2.0, verified 2026-08-15)

What exists and how it was verified:

| Piece | State | Verified by |
|---|---|---|
| Full firmware: detector, state machine, storage, display, buttons, Wi-Fi, web server | built | `pio run` — RAM 16.4%, flash 38.9% |
| Two-mic TDOA direction gate, echo guard, 6 profiles | built | 25 host assertions in `tools/test_tdoa.cpp` |
| Web UI: live WS clock, history, settings, diagnostics, CSV | built | `node --check`, HTML parse |
| LittleFS image | builds | `pio run -t buildfs` |
| Battery monitoring (A6) | built | compiles; **reading unverified against a meter** |
| CI: host tests, web lint, firmware + LittleFS build, size report | written | runs once the project is its own repo |
| Drills engine (workstream B) | built | compiles; chart palette validated against the UI surface |
| Docs: architecture, detection, API, wiring, BOM, roadmap | current | reviewed against code this session |
| **Field behaviour** | **unproven** | nothing — it has never heard a gun |

The honest gaps, in order of importance: enclosure acoustics (loudness),
physical mic array, battery, field validation of every threshold, then the
three software tracks below.

---

## Step 0 — move to the standalone repo

Blocked on the owner: **create an empty repo on GitHub** (suggested name
`shot-timer`, no README/license — the project brings its own) and tell the
session its name.

Then, from `suatbatu/suatbatu` on `claude/shot-timer-repo-s9mp7v`:

```bash
# preserve the subdirectory's history as a standalone lineage
git subtree split -P shot-timer -b shot-timer-standalone
# attach the new repo to the session first (add_repo, access: push), then:
git push https://github.com/suatbatu/shot-timer.git shot-timer-standalone:main
```

After the push: verify the tree and both commits arrived, set `main` as
default, and do all further work in a fresh clone of the standalone repo.
Leave the branch in `suatbatu/suatbatu` as-is (historical record).

## Working in this environment (read before building)

Hard-won operational knowledge; ignoring it costs an hour each time:

1. **PlatformIO's registry is blocked** (403 via the proxy). Two consequences:
   - `tool-scons` cannot be fetched. Fix: `pip install scons==4.8.1`, copy the
     `SCons` package into `~/.platformio/packages/tool-scons/` with a stub
     `scons.py` (`import SCons.Script; SCons.Script.main()`), a `package.json`
     (`{"name":"tool-scons","version":"4.40801.0"}`), and a `.piopm` file
     (copy the shape from any installed package's `.piopm`).
   - Registry `lib_deps` fail. Everything is therefore pinned to **git tags**
     in `platformio.ini` — keep it that way. One trap remains:
     ESPAsyncWebServer's own `library.json` declares a registry dependency on
     AsyncTCP. After the first install, delete the `dependencies` key from
     `.pio/libdeps/shottimer/ESPAsyncWebServer/library.json` or the build
     dies with `HTTPClientError`.
2. **GitHub is MCP-only** — no `gh` CLI. Repo creation and admin are the
   owner's job; pushes work over git with the session credentials once the
   repo is attached via `add_repo`.
3. **Git reads of public repos work** (`git ls-remote`, clone) even when the
   API answers 404/denied — use git to check tags, not the API.
4. Historical quirk: this session built in `/home/user/shot-timer` and
   committed a copy under `suatbatu/suatbatu/shot-timer/`. After step 0,
   **build directly in the standalone clone** and let that duplication die.

## Verification gates (every commit, no exceptions)

```bash
cd tools && ./run_tests.sh        # host tests — must end "all tests passed"
cd firmware && pio run            # firmware compiles
pio run -t buildfs                # web UI fits and builds
node --check data/app.js          # after any web UI change
```

Report RAM/flash numbers in the commit message when they move meaningfully.
Nothing gets called "done" in a commit message or chat that was not compiled
and tested. Field claims ("detects X", "rejects Y") are forbidden until the
owner has confirmed them on hardware — say "built, unproven" instead.

## Invariants (design decisions that must survive future edits)

1. **Nothing that can block sits between the microphone and a timestamp.**
   The detector task never allocates, never touches network/FS/display.
2. **Shot times come from the I2S sample counter**, never from task wake time.
3. **The remaining countdown is never displayed or published.** Not on the
   OLED, not in any API field, not over BLE. It is the start cue the random
   delay exists to remove.
4. **The direction gate fails open** on every uncertainty (one mic, low
   correlation confidence, window outside ring). A dropped real shot is worse
   than an accepted neighbour's.
5. **Rejection counters stay user-visible** — a working gate and a broken mic
   must be distinguishable from the shooter's side.
6. **Passwords are write-only** over every interface; the web server refuses
   to start without one.
7. **Web handlers never drive the state machine directly** — they queue flags
   the main loop consumes (`postStart`/`postStop` pattern).
8. **Settings clamp rather than reject**, and profile writes are addressed by
   index so clients can't clobber each other.

---

## Workstream A — hardware bring-up (owner in the loop)

The firmware is ahead of the hardware; this is now the critical path. Ordered
so each step gives a testable result before the next.

| # | Step | Firmware side | Owner checkpoint — report back |
|---|---|---|---|
| A1 | Flash a bare DevKitC, no peripherals | none — should boot headless | serial shows `shot-timer v0.2.0 ready`, `no OLED found` warning |
| A2 | OLED + 3 buttons | none | menu navigable, screen flip works from menu |
| A3 | **One** microphone (mic A, L/R→GND) | none | level meter jumps on a clap; `FATAL: microphone/I2S init failed` never appears |
| A4 | Piezo on GPIO 4 | none | start beep fires; no phantom shot at t=0 (beep mute working) |
| A5 | **Second** microphone (L/R→3V3), 120 mm apart on a bar | none | Detector panel: "Second microphone: detected"; clap ahead → angle ≈ 0°, confidence > 50%; clap from the side → angle swings out |
| A6 | Battery + divider on GPIO 1 | ✅ **done** — smoothed ADC, LiPo discharge curve, OLED footer, `/api/status`, web badge | reported voltage matches a multimeter within ~0.1 V |
| A7 | Enclosure v1, both mounts (front + top) | none | buttons usable one-handed; flip setting matches the top mount |
| A8 | Buzzer cavity + `beepFreqHz` sweep | none | **measured** SPL at 30 cm with a phone meter, number written into `BOM.md` |
| A9 | Range day 1: pistol, one shooter | none | `accepted` == round count per string; splits plausible vs a reference timer if available |
| A10 | Range day 2: two shooters, adjacent bays, gate on | tune from data | gate accepts own shots, rejects neighbour's; note the angles/confidences it reported |
| A11 | Replace estimated thresholds with measured ones | update `absoluteFloor()` table + shipped profiles + `micOffsetMs` | — |

A6 was the only new firmware in this stream and is now written. Everything
remaining in A needs the owner and a soldering iron. A9–A11 are what turns
"built, unproven" into "validated" — until then the README's honest-status
section stays as it is.

## Workstream B — drills engine

Smallest of the three tracks, pure firmware+UI, builds directly on the par
machinery. Runs while workstream A waits on parts and range days.

- **B1 — model.** A drill = name + par schedule (up to `MAX_PAR_TIMES` beeps)
  + expected shot count + notes. Store as NDJSON on LittleFS (same pattern as
  strings — append-only is overkill here, a single small JSON file of ≤20
  drills is fine). Ship the classics pre-seeded: Bill Drill (6 shots, 2.0 s
  par), El Presidente (12 shots, 10.0 s), 1R1 (2 shots), Doubles, Blank
  (no par).
- **B2 — firmware.** `activeDrill` in Settings; starting a string under a
  drill applies its par schedule and stamps the drill id into the saved
  `StringRun` JSON. Keep `StringRun` itself drill-agnostic — one added field.
- **B3 — web UI.** A Drills tab: pick/edit drills, start a string under one,
  and per-drill history filtered from the existing strings log — trend of
  first shot / total / best split over sessions. No new storage needed; it is
  a filter over data already saved.
- **B4 — device UI.** Menu item to pick the active drill by name. Nothing
  fancier on 128×64.
- ✅ **Done (v0.3.0).** B1–B4 all built: eight drills in their own NVS key,
  par precedence resolved in `TimerApp::parSchedule()`, drill name+index+
  expected-count stamped onto every stored string, a Drills tab with editor
  and a validated two-series trend chart, a `Drill` menu item on the device,
  and a `drill` column in the CSV export. Owner checkpoint outstanding: run
  one on real hardware.

## Workstream C — video sync (browser-only)

The SG Timer 2's headline app feature, and it needs zero firmware: every shot
is already timestamped and pushed over the WebSocket.

- **C1 — capture.** In the Live tab: `getUserMedia` (rear camera) +
  `MediaRecorder`. Record start ≈ `performance.now()` anchor; the `beep`
  event provides t=0 in the same clock the `tick` events already use.
- **C2 — overlay.** After `end`, a review player: video with shot markers on
  a timeline strip (draw + splits labelled), tap a marker to seek. Marker
  time = video time of anchor + `atMs`.
- **C3 — honesty note in the UI.** Browser event-to-frame alignment is tens
  of milliseconds, not the sub-ms of the detector — fine for "watch your
  draw", not for scoring. Say so on the player.
- **C4 — storage.** Keep it session-local (blob URL, offer download). Do
  *not* try to store video on the device — 1.875 MB of LittleFS says no.
- **Constraints:** camera APIs require a secure context in some browsers;
  test against `http://192.168.4.1` on real iOS/Android early — this is an
  owner checkpoint, not something CI can prove. If `getUserMedia` is blocked
  on plain HTTP, the honest fallbacks are: document it, or record with the
  native camera app and align manually — decide with the owner, don't
  silently ship a broken tab.
- **Done when:** the owner records a dry-fire string on a phone and scrubs
  to each click via the markers. Version 0.4.0.

## Workstream D — BLE + PractiScore (research-heavy, highest leverage)

Being the timer PractiScore already knows how to talk to is worth more than
any feature of our own app. It is also the only track with real unknowns, so
it runs research-first.

- **D1 — research (no code).** Gather what is public about the AMG Lab
  Commander BLE protocol — the PractiScore community forum documents it best
  (AMG is the documented/emulatable target; SG's protocol is not public).
  Product: `docs/BLE_PROTOCOL.md` describing service/characteristic UUIDs,
  frame formats for shot data and string state, and what PractiScore Log
  expects. **If the wire format cannot be established from public sources,
  stop and say so** — the fallback is a checkpoint with the owner using a
  BLE sniffer app against PractiScore, not guesswork.
- **D2 — advertise + link.** NimBLE (bundled with the Arduino core — no new
  registry deps, which matters in this environment) advertising as an AMG
  Commander. Flash cost is real: re-check the 3 MB app slot; `partitions.csv`
  has headroom but verify.
- **D3 — live data.** Mirror what the WebSocket already emits — beep, shots,
  string end — into the AMG frame format. One source of truth: a thin
  adapter subscribed to the same `TimerApp::onEvent` sink the web interface
  uses. The detector task is untouched (invariant 1).
- **D4 — owner checkpoint.** PractiScore Log on a real phone pairs, receives
  a dry-fire string, shows correct times. This cannot be verified without a
  phone; do not claim it works before this step.
- **Wi-Fi/BLE coexistence risk:** both share the S3 radio. If the timer
  stutters with both up, the honest resolution is a settings toggle (BLE
  mode vs Wi-Fi mode) rather than degraded both. Measure first.
- **Done when:** D4 passes. Version 0.5.0.

## Sequence (owner's order)

```
done ──▶ CI + A6 (battery)
     ──▶ B (drills)          ── firmware + UI, no hardware needed
     ──▶ D (BLE/PractiScore) ── research first, phone checkpoint last
     ──▶ C (video sync)      ── browser-only, owner tests on a phone
 A runs in parallel throughout, paced by parts arriving and range days.
 A11 (measured thresholds) lands whenever the range data exists.
```

Within each workstream the listed order is dependency order.

## Checkpoint protocol with the owner

When a step needs the owner (A-steps, C's phone test, D4): state exactly what
to do and what to report, in one message, e.g. *"Clap once about 1 m directly
in front of the array, then once from the far left. Report the two 'Last
angle' and 'Last correlation' values from the Detector panel."* Then park that
workstream and continue elsewhere — never block the whole plan on one
checkpoint, and never fill in the expected result on the owner's behalf.
