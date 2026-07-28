# Architecture

## Two nodes, one job

PillPilot is split across two ESP32 boards on purpose:

- **Dispenser node** (`firmware/dispenser/`) — the safety-critical brain. Owns
  the motor, sensors, RTC, schedule, and the secured web server. It must be
  boring and reliable.
- **Camera node** (`firmware/esp32cam/`) — an ESP32-CAM that does nothing but
  take a photo when asked and push it to Telegram. It is deliberately *not*
  trusted with any dispensing logic, because the ESP32-CAM is the flakiest part
  of the build (brownouts, tiny RAM). **If the camera dies, dosing continues.**

They coordinate over MQTT (and Telegram for humans). No control path runs
*into* the camera from the internet.

## Data flow

```
Scheduler (RTC/NTP)
      │ dose due
      ▼
Carousel FSM ──rotate──► stepper ──► pill drops
      │                                  │
      │                          IR break-beam (drop?)
      ▼                                  │
 buzzer + LED  ◄──────────────────── DoseSensors
      │                                  ▲
      │                          cup switch / button (taken?)
      ▼
 Notifier ──► Telegram: "Sabah dozu hazır 💊"
      │   └─► MQTT: pillpilot/<id>/event {state:"due"}
      │   └─► MQTT: pillpilot/<id>/cam/capture   ──► CAMERA NODE ──► Telegram photo
      ▼
 (waits up to graceMinutes)
   taken ─► Notifier "Alındı ✅" + log            missed ─► Notifier "KAÇIRILDI ⚠️" + log + photo
```

## Dispensing state machine

Implemented in `Carousel` + `main.cpp` loop.

```
        ┌────────┐  schedule match & not fired    ┌────────────┐
        │  IDLE  │ ─────────────────────────────► │ DISPENSING │
        └────────┘                                └─────┬──────┘
            ▲                                           │ rotate one slot
            │                                           ▼
            │                                    ┌──────────────┐  no drop detected
            │                                    │  VERIFY_DROP │ ──────────────► JAM/EMPTY
            │                                    └──────┬───────┘   (alert, stop, needs human)
            │                                           │ drop OK
            │                                           ▼
            │  taken confirmed                   ┌──────────────┐
      ┌─────┴──────┐  ◄──────────────────────────│ WAIT_CONFIRM │
      │   TAKEN    │                             └──────┬───────┘
      │ log+notify │                                    │ graceMinutes elapsed
      └────────────┘                                    ▼
                                                 ┌──────────────┐
                                                 │   MISSED     │ ─► log+notify+photo ─► IDLE
                                                 └──────────────┘
```

Key invariants:

- **Idempotent firing.** Each dose slot has a `lastFiredEpoch` persisted to NVS.
  On boot the scheduler compares wall-clock (RTC) against it, so a reboot never
  re-dispenses a slot already dispensed today, and never silently skips one that
  is overdue within the grace window.
- **Homing on boot.** The carousel rotates to its index mark (hall sensor) to
  establish absolute compartment 0 before any dose can fire.
- **Fail-safe.** `JAM/EMPTY` halts the mechanism and alerts; it never keeps
  rotating "to try again" in a way that could dump multiple compartments.

## Persistence (NVS)

| Key | Meaning |
|---|---|
| `sched` | JSON schedule (dose times, day mask, labels, enabled) |
| `slot` | current carousel compartment index |
| `fired` | per-slot last-fired epoch (double-dose guard) |
| `auth_user` / `auth_hash` / `auth_salt` | web admin credentials |
| `events` | small ring buffer of recent taken/missed/jam events |

## Time

NTP over Wi-Fi sets the clock, but the **DS3231 RTC is the source of truth** so
schedules survive Wi-Fi and power loss. On boot: read RTC → if Wi-Fi + NTP
available and drift is large, discipline the RTC → run from RTC thereafter.
