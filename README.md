# PillPilot 💊

A DIY, network-connected **medication carousel** for helping elderly relatives take
the right pills at the right time — with a secured, password-protected web
interface for remote control and camera monitoring.

Built for a specific real-world situation: an 85+ couple in Turkey with mild
short-term memory loss, a family member (uncle) nearby who reloads the device
weekly, and family further away who manage the schedule and watch adherence
**online**.

> ⚠️ **This is a hobbyist project that dispenses medication. Read
> [`docs/SECURITY.md`](docs/SECURITY.md) and the _Safety_ section below before
> trusting it with anyone's health.** A missed or doubled dose can be dangerous.
> Run it in parallel with a proven method until you trust it.

---

## Why it's built this way

| Requirement | Design decision |
|---|---|
| Only the current dose reachable, no double-dosing | **Rotating carousel** under a fixed lid with a single opening — geometry does the locking |
| "Make sure it was taken", not just "reminded" | **IR break-beam** (pill dropped) + **cup/taken switch** (dose collected) → real adherence signal |
| Work through Wi-Fi and power outages | **DS3231 battery-backed RTC** + schedule/state persisted to NVS + optional 18650 UPS |
| Manage online from anywhere | Secured **async web server** + **MQTT** state/schedule + **Telegram** alerts |
| Visual proof without a nanny-cam | Separate **ESP32-CAM node**, aimed at the tray, event snapshots to Telegram |
| A camera crash must never stop a dose | **Two-board split** — dispenser logic never runs on the flaky camera board |

## Architecture (two nodes)

```
                    ┌─────────────────────────────┐
                    │   Family phones (anywhere)   │
                    │  Telegram group · Web UI     │
                    └───────▲───────────▲──────────┘
                            │ TLS       │ VPN / TLS reverse proxy
                 Telegram   │           │  (see docs/SECURITY.md)
                  Bot API   │           │
            ┌───────────────┴──┐    ┌───┴────────────────────┐
            │  DISPENSER NODE  │    │   Home LAN (Wi-Fi)     │
            │  (ESP32 devkit)  │◄──►│   MQTT broker          │
            │                  │    └───┬────────────────────┘
            │ • Carousel FSM   │        │ capture trigger
            │ • DS3231 RTC     │        ▼
            │ • Drop+taken     │    ┌──────────────────┐
            │   sensors        │    │  CAMERA NODE     │
            │ • Secured web UI │    │  (ESP32-CAM)     │
            │ • Telegram+MQTT  │    │ • Snapshot→TG    │
            └──────────────────┘    │ • Auth'd /snapshot│
                                    └──────────────────┘
```

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the full data flow and
the dispensing state machine.

## Repository layout

```
firmware/
  dispenser/     PlatformIO project — main ESP32 node
  esp32cam/      PlatformIO project — ESP32-CAM node
hardware/
  README.md      Mechanical "design contract" the firmware assumes
docs/
  ARCHITECTURE.md  Data flow + dispensing state machine
  WIRING.md        Pin map and connections
  BOM.md           Bill of materials (~$30–60)
  SECURITY.md      Read this before any remote access
```

The **mechanical design (carousel, lid, chute, mounts) is yours to model** — this
repo only specifies the interface the firmware depends on. See
[`hardware/README.md`](hardware/README.md) for that contract.

## Quick start

1. **Build the mechanism.** Your carousel just has to satisfy the contract in
   [`hardware/README.md`](hardware/README.md) (one drop opening, a homing mark,
   `SLOTS` compartments).
2. **Wire it up.** Follow [`docs/WIRING.md`](docs/WIRING.md).
3. **Configure secrets.** In each firmware folder:
   ```bash
   cp include/secrets.example.h include/secrets.h
   # edit Wi-Fi, MQTT, Telegram, and web-admin credentials
   ```
   `secrets.h` is git-ignored. The web password is stored **salted+hashed** in
   NVS on first boot — the plaintext in `secrets.h` is only the initial seed.
4. **Flash the dispenser** (`firmware/dispenser/`):
   ```bash
   pio run -t upload          # firmware
   pio run -t uploadfs        # web UI assets (LittleFS)
   ```
5. **Flash the camera node** (`firmware/esp32cam/`) — needs an FTDI adapter or
   ESP32-CAM-MB board; hold GPIO0→GND to enter flash mode.
6. **Open the web UI** at `http://pillpilot.local/` (or the device IP), log in,
   set the schedule, and run a **test dispense** and a **test miss** to confirm
   the Telegram alerts reach everyone before relying on it.

## Safety (please don't skip)

- **Fail-safe, not fail-open.** On a jam or power loss the device stops and
  alerts; it never dumps all compartments.
- **No double-dispense.** The last-fired state is persisted to NVS, so a reboot
  mid-cycle never re-fires or skips.
- **Emergency access.** The lid is openable by hand — never build a box that can
  trap the medicine if the electronics die.
- **The camera is proof, not the sensor of record.** The IR beam + cup switch
  decide taken/missed; the photo is human-verifiable backup.
- **Not a medical device.** No regulatory approval, no warranty. Confirm any
  schedule change with the person's doctor/pharmacist (aile hekimi / eczacı).

## License

MIT — see [`LICENSE`](LICENSE). Provided **as-is with no warranty**; you are
responsible for validating it before any real use.
