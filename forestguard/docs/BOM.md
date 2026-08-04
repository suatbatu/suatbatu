# ForestGuard — Bill of Materials

Two parts: the **server** (the Pi that runs this software) and an example
**sensor node** (the field hardware that sends telemetry). Prices are rough,
2025-ish, and vary by region.

## Server — Raspberry Pi 5 + SSD HAT

| Item | Notes | ~Cost |
|---|---|---|
| Raspberry Pi 5, **8 GB** | Plenty for this workload; 8 GB gives headroom for cloud-sync, dashboards, future extras. | $80 |
| NVMe **SSD HAT** | M.2 HAT for the Pi 5's PCIe lane. Pimoroni NVMe Base, Pineberry HatDrive!, official M.2 HAT+, 52Pi, etc. | $10–20 |
| **NVMe SSD** (M.2 2230/2242/2280) | 256 GB is ample — years of readings. Prefer a known brand; check your HAT's supported length. | $25–40 |
| Official **27 W USB-C PSU** | The Pi 5 + NVMe needs the real 5 V/5 A supply; don't skimp. | $12 |
| **Active cooler** / case with fan | Pi 5 runs warm, especially with PCIe active. Official Active Cooler or a fanned case. | $5–15 |
| microSD card (boot) | 32 GB; the OS boots here, the **database lives on the SSD**. (Or boot from NVMe.) | $8 |
| Ethernet cable / reliable Wi-Fi | Wired is more reliable for a base station. | — |
| **UPS / battery HAT** (optional) | Rides out power blips so the base station stays up during an event. | $20–40 |
| **Subtotal** | | **~$150–210** |

The 8 GB Pi 5 is comfortably oversized for a few hundred nodes — that's
deliberate. It leaves room to also run nginx (TLS), the cloud forwarder, and
local dashboards without the box ever being the bottleneck.

## Example sensor node (per node)

ForestGuard is agnostic to the exact node; anything that can POST JSON works.
A common, cheap wildfire-oriented build:

| Item | Purpose | Metric sent | ~Cost |
|---|---|---|---|
| ESP32 dev board (Wi-Fi) or ESP32 + **LoRa** | MCU + uplink | — | $5–12 |
| **DHT22** / SHT31 / BME280 | Temperature + humidity (BME280 also pressure) | `temp_c`, `humidity` | $3–8 |
| **MQ-2** gas/smoke sensor | Smoke / combustible gases | `smoke` | $2 |
| **MQ-135** air-quality sensor | General gas / air quality | `gas` | $2 |
| IR **flame sensor** module | Direct flame presence | `flame` (0/1) | $1–2 |
| 18650 cell + TP4056 / solar charger | Off-grid power | `battery_v` | $6–12 |
| Weatherproof enclosure (vented) | Field survival | — | $5–10 |
| **Per-node subtotal** | | | **~$25–45** |

Notes:
- **Wi-Fi vs. LoRa.** Wi-Fi nodes POST straight to the Pi. For range beyond
  Wi-Fi, use LoRa nodes → a LoRa **gateway** that forwards to the Pi's HTTP API;
  the gateway is the thing that speaks `POST /api/v1/telemetry`.
- **Metrics are optional.** A node that only has temp + humidity still works; the
  risk model scores whatever it reports (see `ARCHITECTURE.md §4`).
- **Calibrate.** MQ-series sensors drift and need warm-up + per-unit calibration.
  Tune the `FG_SMOKE_*` / `FG_GAS_*` thresholds to your baseline (see `.env.example`).

## What this repo provides vs. what you build

- **This repo:** the server, dashboard, API, alerting, risk model, cloud
  redundancy, Pi deployment, and a simulator to prove it all without hardware.
- **You build:** the physical sensor nodes and (optionally) the AWS ingest
  endpoint for redundancy mode. The node firmware just needs to POST the JSON in
  [`API.md`](API.md).
