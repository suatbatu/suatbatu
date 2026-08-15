# BLE: the AMG Lab Commander protocol

Being a timer that PractiScore already knows how to talk to is worth more than
any app of our own. PractiScore's timer integration speaks the **AMG Lab
Commander** protocol, so that is what this firmware emits.

## Where this came from

Everything below is derived from a **public, MIT-licensed reference
implementation** — [`DenisZhadan/AmgLabCommander`](https://github.com/DenisZhadan/AmgLabCommander),
an Android demo app that connects to a real Commander, starts it remotely,
receives live shot pushes and requests a stored string. No sniffing, no
guessing: the UUIDs, the ASCII commands and the frame layouts are all read
directly out of that source.

What is **not** established from public sources, and is therefore marked as
such below: the meaning of bytes 10–11 of the live push frame, the reply format
for `REQ SCREEN HEX`, and whether PractiScore's own scan filter is identical to
that app's. Those need a sniffer or a real Commander to settle — see the
verification checklist at the end.

## Transport

Nordic UART Service (NUS) — a generic serial-over-BLE profile, not a bespoke one.

| Role | UUID |
|---|---|
| Service | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| **RX** — client writes commands here | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| **TX** — timer notifies data here | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |
| CCCD on TX | `00002902-0000-1000-8000-00805f9b34fb` |

Clients find the timer **by advertised name**. The reference app accepts any
device whose name, upper-cased, starts with `AMG LAB COMM` or `COMMANDER`:

```java
return upperCase.startsWith("AMG LAB COMM") || upperCase.startsWith("COMMANDER");
```

Frames are 14 bytes and fit inside the default 23-byte ATT MTU, so no MTU
negotiation is required.

## Commands (ASCII, written to RX)

| Command | Effect | Confidence |
|---|---|---|
| `COM START` | Remote start — the timer beeps and opens a string | **confirmed** (wired to a button in the reference app) |
| `REQ STRING HEX` | Timer replies with the whole shot series | **confirmed** |
| `SET SENSITIVITY %02d` | Microphone sensitivity, 1–10 | **likely** (present as a commented-out example) |
| `REQ SCREEN HEX` | Unknown reply format | **unverified** — not implemented |

## Notifications (from TX)

### Value encoding

Every time value is a **big-endian unsigned 16-bit count of centiseconds**.
`0x0076` = 118 = 1.18 s. The maximum representable time is 655.35 s.

> ⚠️ **The reference app decodes this with an off-by-one bug**, and anything
> written against it inherits the bug:
>
> ```java
> int value = 256 * value1 + value2;   // Java bytes are signed
> if (value2 <= 0) value += 256;       // should be < 0, not <= 0
> ```
>
> The `+= 256` exists to undo Java's sign extension when the low byte is ≥ 0x80.
> Using `<= 0` instead of `< 0` also fires when the low byte is exactly `0x00`,
> so every time that is an exact multiple of 2.56 s reads 2.56 s too high.
>
> **This firmware emits the correct encoding.** Matching the bug would corrupt
> every correct client to please one broken one. A client carrying this bug will
> misread our 2.56 s / 5.12 s / 7.68 s values, exactly as it misreads a real
> Commander's.

### Live shot push — `0x01 0x03`

Sent as each shot is detected.

| Bytes | Meaning |
|---|---|
| 0–1 | `01 03` — frame type |
| 2–3 | shot number |
| 4–5 | time of this shot, from the beep |
| 6–7 | split into this shot |
| 8–9 | first-shot time of the string |
| 10–11 | **unknown** — the reference app marks these `???`. We send zeros |
| 12–13 | series / batch identifier |

### State frames — `0x01 0x05` and `0x01 0x08`

| Bytes | Meaning |
|---|---|
| `01 05` | timer started (the beep fired) |
| `01 08` | timer stopped / no longer waiting |

The reference app only reads the first two bytes of these; we send 14 bytes
zero-padded so every frame on the wire is the same length.

### String data — `0x0A`…`0x1A`

The reply to `REQ STRING HEX`, sent as a sequence of frames:

| Bytes | Meaning |
|---|---|
| 0 | frame counter, starting at **10** (`0x0A`). Receiving 10 means "new string, clear what you had" |
| 1 | how many shot values are in *this* frame |
| 2*i*, 2*i*+1 | shot value *i*, for *i* = 1…count |

Values start at offset 2 and a 14-byte frame therefore carries at most **6**.
The counter is bounded at 26 (`0x1A`) by the reference app's range check, so the
protocol tops out at **17 frames × 6 = 102 shots** per string — fewer than our
own 120-shot limit, so a very long string is truncated on the BLE side only.
The stored string and the web UI keep everything.

## What this firmware does

- Advertises as **`Commander shot-timer`** by default. It starts with
  `COMMANDER`, so name-filtering clients find it, while still saying what it
  actually is — this is a compatibility shim, not an attempt to pass as an AMG
  Lab product. The name is a setting if you need a different one.
- Serves the NUS UUIDs above, emits `01 05` on the beep, a `01 03` push per
  shot, `01 08` at the end of a string, and answers `REQ STRING HEX`.
- Accepts `COM START` and `SET SENSITIVITY nn`.
- **Queues every inbound command for the main loop** rather than acting on it
  from the BLE host task — the same rule the web handlers follow, because two
  tasks driving one state machine is how you corrupt an open string.
- Is **off by default**. Wi-Fi and BLE share one radio on the ESP32-S3, and
  until the coexistence cost is measured on real hardware, the timer should not
  silently pay it.

## Verification checklist — none of this is proven yet

Written from a reference implementation, compiled, and never tested against a
real client. Before any of it is claimed to work:

1. Scan with a generic BLE tool (nRF Connect). Confirm the name, the service,
   and that TX notifies.
2. Run the reference Android app against it: connect, press *Start (BEEP)*,
   fire a dry-fire string, press *Read shots*.
3. Run **PractiScore Log** against it — the actual goal. Confirm it pairs, and
   that the times it records match what the device's own web UI shows.
4. Measure Wi-Fi throughput and detector timing with BLE connected. If the
   timer stutters, the honest fix is a mode switch, not a degraded both.

Until step 3 passes, the README says "built, unproven" and so should we.
