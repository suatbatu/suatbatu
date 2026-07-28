# Wiring

Pin numbers are defined in `firmware/dispenser/include/config.h` — change them
there, not just here. Defaults target a standard **ESP32 DevKit-C (38-pin)**.

## Dispenser node (ESP32 DevKit-C)

### Motor — v1: 28BYJ-48 + ULN2003 (default)

| ULN2003 | ESP32 |
|---|---|
| IN1 | GPIO19 |
| IN2 | GPIO18 |
| IN3 | GPIO5 |
| IN4 | GPIO17 |
| VCC | 5V (external 5V recommended, not USB) |
| GND | GND (common with ESP32) |

### Motor — v2: NEMA-17 + A4988 (compile-time swap)

Set `#define MOTOR_DRIVER DRIVER_A4988` in `config.h`.

| A4988 | ESP32 |
|---|---|
| STEP | GPIO18 |
| DIR  | GPIO19 |
| EN   | GPIO5 (active-low) |
| VMOT | 12V motor supply (+ 100µF across VMOT/GND) |
| VDD  | 3V3 |
| GND  | GND (logic + motor grounds joined at the driver) |

> Set the A4988 current limit (Vref) before connecting the motor.

### Sensors & indicators

| Signal | ESP32 | Notes |
|---|---|---|
| Hall homing sensor (OUT) | GPIO34 | input-only pin; magnet on the disc at slot 0 |
| IR break-beam (drop detect) | GPIO35 | input-only pin; across the drop chute |
| Cup / "taken" switch or button | GPIO32 | to GND, `INPUT_PULLUP` |
| Piezo buzzer (+) | GPIO25 | via transistor if loud/active buzzer |
| Status LED | GPIO26 | + 330Ω to LED to GND |

### DS3231 RTC (I²C)

| DS3231 | ESP32 |
|---|---|
| SDA | GPIO21 |
| SCL | GPIO22 |
| VCC | 3V3 |
| GND | GND |

### Power

- Feed the ESP32 and 5V motor from a **5V ≥2A** supply.
- **Optional UPS:** an 18650 + TP4056 (or a purpose-built UPS module) on the 5V
  rail keeps time and dosing alive through outages. Route battery status to an
  ADC pin if you want low-battery Telegram alerts (`PIN_VBAT_SENSE` in config).

## Camera node (ESP32-CAM, AI-Thinker)

- Powered from its **own stable 5V ≥2A** with a **470–1000µF cap** across
  5V/GND (brownouts are the #1 ESP32-CAM failure).
- Camera pins are fixed by the AI-Thinker board (defined in `esp32cam/config.h`).
- **Flashing:** no USB — use an FTDI/USB-TTL adapter (or the ESP32-CAM-MB base).
  Wire FTDI 5V→5V, GND→GND, TX→U0R, RX→U0T; jumper **GPIO0→GND** to enter
  bootloader, reset, upload, then remove the jumper and reset.
- Free GPIOs are scarce; the node needs none beyond power + the flash pins for
  its event-snapshot role.

## Grounding

Tie ESP32 ground, motor-supply ground, and sensor ground together at a single
point. A floating motor ground is the most common cause of missed steps and
random resets.
