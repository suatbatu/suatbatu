// shot-timer — compile-time hardware map and fixed limits.
//
// Anything a shooter would plausibly want to change between strings lives in
// Settings (NVS-backed, editable from the web UI). This file is the stuff that
// is fixed by how the board is wired.
#pragma once

#include <stdint.h>

// ---------------------------------------------------------------- identity --
#define FW_NAME "shot-timer"
#define FW_VERSION "0.1.0"

// ------------------------------------------------------------------- pins ---
// I2S MEMS microphone (INMP441 / ICS-43434 / T5837 breakout).
// L/R pin tied to GND -> the mic transmits in the LEFT slot.
#define PIN_I2S_BCLK 15
#define PIN_I2S_WS 16
#define PIN_I2S_DIN 17

// SSD1306 128x64 OLED, hardware I2C.
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9

// Piezo sounder driven by an LEDC channel (square wave, not a DAC tone).
#define PIN_BUZZER 4
#define BUZZER_LEDC_CHANNEL 0
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
// 32 kHz gives 31.25 us of timestamp resolution on a shot onset, which is two
// orders of magnitude finer than the 0.01 s a shot timer reports. See
// docs/DETECTION.md for why the timestamp is derived from the sample counter
// rather than from when the DMA block happened to arrive.
#define AUDIO_SAMPLE_RATE 32000
#define AUDIO_FRAMES_PER_BLOCK 256  // 8 ms per DMA block
#define AUDIO_DMA_BLOCKS 6

// ---------------------------------------------------------------- limits ----
#define MAX_SHOTS_PER_STRING 120
#define MAX_PAR_TIMES 4
#define MAX_STORED_STRINGS 50

// LittleFS paths.
#define PATH_STRINGS "/strings.ndjson"

// SoftAP fallback, used at ranges with no Wi-Fi.
#define AP_SSID_PREFIX "ShotTimer-"
#define AP_CHANNEL 6
