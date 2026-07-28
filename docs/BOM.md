# Bill of materials

Approximate — prices vary by region/supplier. Two full units (one per person)
roughly double the per-unit electronics cost.

## Dispenser node

| Part | Qty | Notes | ~USD |
|---|---:|---|---:|
| ESP32 DevKit-C (38-pin) | 1 | Any ESP32; not ESP8266 | 5–8 |
| DS3231 RTC module | 1 | Battery-backed, with CR2032 | 1–2 |
| 28BYJ-48 stepper + ULN2003 driver | 1 | v1 motor; cheap, low torque | 2–3 |
| — or NEMA-17 + A4988/DRV8825 | 1 | v2; precise, more torque | 6–10 |
| Hall-effect sensor (e.g. A3144) + magnet | 1 | Carousel homing | <1 |
| IR break-beam / photo-interrupter | 1 | Drop detection at chute | 1–2 |
| Arcade button or microswitch | 1 | "Taken" confirmation / cup switch | 1–2 |
| Active piezo buzzer | 1 | Language-neutral alarm | <1 |
| LED + 330Ω resistor | 1 | Status indicator | <1 |
| 5V ≥2A power supply | 1 | Mains adapter | 3–5 |
| 18650 + TP4056/UPS module | 1 | **Optional** outage backup | 3–6 |
| Wire, headers, protoboard/PCB | — | | 2–4 |

## Camera node

| Part | Qty | Notes | ~USD |
|---|---:|---|---:|
| ESP32-CAM (AI-Thinker, OV2640) | 1 | Get one **with PSRAM** | 6–10 |
| FTDI / USB-TTL adapter | 1 | For flashing (reusable) | 2–4 |
| 5V ≥2A supply + 470–1000µF cap | 1 | Brownout prevention | 3–5 |

## Consumables / mechanical (you design these)

- **PETG filament** for parts near the pills (better heat/food tolerance than
  PLA). Ideally a food-safe insert or cup for direct pill contact.
- Small screws/inserts, bearing or bushing for the disc hub, magnet for homing.

## Rough total

- **One unit, electronics only:** ~$25–45 (28BYJ-48) / ~$35–60 (NEMA-17)
- Add filament + the reusable FTDI adapter once.
