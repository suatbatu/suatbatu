// shot-timer — compile-time hardware map and fixed limits.
//
// Anything a shooter would plausibly want to change between strings lives in
// Settings (NVS-backed, editable from the web UI). This file is the stuff that
// is fixed by how the board is wired.
#pragma once

#include <stdint.h>

// ---------------------------------------------------------------- identity --
#define FW_NAME "shot-timer"
#define FW_VERSION "0.6.0"

// ------------------------------------------------------------------- pins ---
// Two I2S MEMS microphones share one bus. They are distinguished by their L/R
// pin, not by wiring: mic A ties L/R to GND (left slot), mic B ties it to VDD
// (right slot). Both share BCLK, WS and DIN — a second microphone costs three
// solder joints and no GPIO. See docs/WIRING.md.
#define PIN_I2S_BCLK 15
#define PIN_I2S_WS 16
#define PIN_I2S_DIN 17

// SSD1306 128x64 OLED, hardware I2C. Shared with the optional LIS3DH.
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9

// Optional LIS3DH accelerometer for impulse (dry-fire) detection. It sits on
// the display's I2C bus — no extra bus, one extra wire for the interrupt. The
// INT1 line is what carries the timing; the bus is only used at setup.
#define PIN_IMU_INT 14
#define LIS3DH_ADDR_PRIMARY 0x18  // SA0 low
#define LIS3DH_ADDR_ALT 0x19      // SA0 high

// How far apart two candidates from different sources must be to count as two
// shots rather than one shot heard twice. 30 ms is comfortably inside the
// fastest human split (~0.12 s) and comfortably outside the few milliseconds
// that separate a recoil impulse from its own muzzle blast.
#define SHOT_MERGE_WINDOW_US 30000

// Piezo sounder driven by an LEDC channel (square wave, not a DAC tone).
#define PIN_BUZZER 4
#define BUZZER_LEDC_RESOLUTION 10  // bits; 50% duty == (1<<10)/2

// Buttons to GND, internal pull-ups.
#define PIN_BTN_START 5
#define PIN_BTN_UP 6
#define PIN_BTN_DOWN 7

// Battery sense: 100k/100k divider from VBAT to this ADC1 pin.
#define PIN_VBAT_ADC 1
#define VBAT_DIVIDER_RATIO 2.0f

// Activity LED (plain LED + resistor; the DevKitC RGB is on 48 and is not used).
#define PIN_LED 21

// ------------------------------------------------------------------ audio ---
// 48 kHz is chosen for the direction gate, not for fidelity. Two microphones
// 120 mm apart see a time-of-arrival difference of at most 350 us, which is
// 16.8 samples at 48 kHz and only 11 at 32 kHz — and the whole front/off-axis
// decision is made inside that span. See docs/DETECTION.md.
#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_FRAMES_PER_BLOCK 256  // 5.33 ms per DMA block
#define AUDIO_DMA_BLOCKS 6
#define SPEED_OF_SOUND_MPS 343.0f

// Cross-correlation window around each onset, and the delay line it reads from.
// The ring must be comfortably longer than PRE + POST + one DMA block.
#define TDOA_RING_SAMPLES 1024  // must be a power of two
#define TDOA_PRE_SAMPLES 32
#define TDOA_POST_SAMPLES 96
#define TDOA_MAX_LAG 32
#define TDOA_MAX_PENDING 8

// ---------------------------------------------------------------- limits ----
#define MAX_SHOTS_PER_STRING 120
#define MAX_PAR_TIMES 4
#define MAX_PROFILES 6
#define PROFILE_NAME_LEN 17
#define MAX_DRILLS 8
#define DRILL_NAME_LEN 17

// The history is an append-only log, compacted when it outgrows its budget, so
// "how many strings" is really "how much flash". 1.2 MB of the 1.875 MB
// LittleFS partition holds a few thousand typical strings.
#define MAX_STORED_STRINGS 2000
#define HISTORY_MAX_BYTES 1200000

// LittleFS paths.
#define PATH_STRINGS "/strings.ndjson"

// SoftAP fallback, used at ranges with no Wi-Fi.
#define AP_SSID_PREFIX "ShotTimer-"
#define AP_CHANNEL 6
