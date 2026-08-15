#include "ShotDetector.h"

#include <driver/i2s_std.h>
#include <esp_timer.h>
#include <math.h>

#include <algorithm>

#include "Settings.h"
#include "config.h"

ShotDetector detector;

namespace {

constexpr float FULL_SCALE = 8388608.0f;  // 2^23, the mic's 24-bit full scale
constexpr float US_PER_SAMPLE = 1000000.0f / AUDIO_SAMPLE_RATE;

// Envelope release: ~15 ms. Long enough to ride over the ringing tail of a
// muzzle blast, short enough that the follower is back down before the next
// shot of a 0.15 s split.
constexpr float ENV_RELEASE = 1.0f - 0.99792f;  // 1 - exp(-1/(0.015 * 32000))
// Noise floor: ~500 ms. It tracks wind, range chatter and the hum of the
// neighbouring bay without following a shot.
constexpr float FLOOR_COEF = 6.25e-5f;  // 1 - exp(-1/(0.5 * 32000))

// Clock servo (see the comment in run()).
constexpr int64_t SERVO_CLAMP_US = 2000;
constexpr int64_t SERVO_RESET_US = 50000;
constexpr int SERVO_DIVISOR = 64;

i2s_chan_handle_t rxChan = nullptr;
portMUX_TYPE armMux = portMUX_INITIALIZER_UNLOCKED;
int64_t muteUntilUs = 0;

float dbToPerMille(float amplitude) {
  const float dbfs = 20.0f * log10f(std::max(amplitude, 1.0f) / FULL_SCALE);
  const float clamped = std::min(std::max(dbfs, -80.0f), 0.0f);
  return (clamped + 80.0f) / 80.0f * 1000.0f;
}

}  // namespace

bool ShotDetector::begin() {
  queue_ = xQueueCreate(MAX_SHOTS_PER_STRING, sizeof(int64_t));
  if (!queue_) return false;

  i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chanCfg.dma_desc_num = AUDIO_DMA_BLOCKS;
  chanCfg.dma_frame_num = AUDIO_FRAMES_PER_BLOCK;
  chanCfg.auto_clear = false;
  if (i2s_new_channel(&chanCfg, nullptr, &rxChan) != ESP_OK) return false;

  i2s_std_config_t stdCfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
      // INMP441 and friends are 24-bit Philips-format in a 32-bit slot.
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                      I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = static_cast<gpio_num_t>(PIN_I2S_BCLK),
              .ws = static_cast<gpio_num_t>(PIN_I2S_WS),
              .dout = I2S_GPIO_UNUSED,
              .din = static_cast<gpio_num_t>(PIN_I2S_DIN),
              .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
          },
  };
  // L/R tied to GND, so the mic drives the left slot only.
  stdCfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

  if (i2s_channel_init_std_mode(rxChan, &stdCfg) != ESP_OK) return false;
  if (i2s_channel_enable(rxChan) != ESP_OK) return false;

  // Priority 10: well above the Arduino loop (1) so audio is never starved,
  // well below the Wi-Fi stack so the radio still behaves. Core 1 keeps it off
  // the core the network stack lives on.
  const BaseType_t ok = xTaskCreatePinnedToCore(taskTrampoline, "shotdet", 4096, this, 10,
                                                &task_, 1);
  return ok == pdPASS;
}

void ShotDetector::arm() {
  drain();
  portENTER_CRITICAL(&armMux);
  muteUntilUs = 0;
  portEXIT_CRITICAL(&armMux);
  armed_ = true;
}

void ShotDetector::disarm() { armed_ = false; }

void ShotDetector::muteUntil(int64_t untilUs) {
  portENTER_CRITICAL(&armMux);
  muteUntilUs = untilUs;
  portEXIT_CRITICAL(&armMux);
}

bool ShotDetector::popShot(int64_t& atUs) {
  if (!queue_) return false;
  return xQueueReceive(queue_, &atUs, 0) == pdTRUE;
}

void ShotDetector::drain() {
  if (queue_) xQueueReset(queue_);
}

uint16_t ShotDetector::levelPerMille() const {
  return static_cast<uint16_t>(dbToPerMille(envelope_));
}

uint16_t ShotDetector::floorPerMille() const {
  return static_cast<uint16_t>(dbToPerMille(noiseFloor_));
}

void ShotDetector::taskTrampoline(void* arg) {
  static_cast<ShotDetector*>(arg)->run();
}

void ShotDetector::run() {
  static int32_t raw[AUDIO_FRAMES_PER_BLOCK];

  float env = 0.0f;
  float floorLevel = FULL_SCALE / 1000.0f;  // start high so boot noise cannot trigger
  uint64_t samplesConsumed = 0;
  uint64_t blankUntilSample = 0;
  int64_t anchorUs = 0;  // esp_timer time of sample 0
  bool anchored = false;

  for (;;) {
    size_t bytesRead = 0;
    if (i2s_channel_read(rxChan, raw, sizeof(raw), &bytesRead, portMAX_DELAY) != ESP_OK) {
      continue;
    }
    const size_t frames = bytesRead / sizeof(int32_t);
    if (frames == 0) continue;

    const int64_t nowUs = esp_timer_get_time();

    // --- sample clock ------------------------------------------------------
    // Shot times are derived from the sample counter, not from when this task
    // woke up: DMA return jitter is hundreds of microseconds and would land
    // straight in the reported split. The anchor is servoed towards esp_timer
    // so the two clocks cannot drift apart over a long session, but slowly
    // enough that one late wake-up does not move it.
    const int64_t blockEndPredicted =
        anchorUs + static_cast<int64_t>((samplesConsumed + frames) * US_PER_SAMPLE);
    if (!anchored) {
      anchorUs = nowUs - static_cast<int64_t>((samplesConsumed + frames) * US_PER_SAMPLE);
      anchored = true;
    } else {
      const int64_t err = nowUs - blockEndPredicted;
      if (err > SERVO_RESET_US || err < -SERVO_RESET_US) {
        // A real discontinuity (DMA overrun, long critical section). Re-anchor.
        anchorUs = nowUs - static_cast<int64_t>((samplesConsumed + frames) * US_PER_SAMPLE);
      } else {
        anchorUs += std::min(std::max(err, -SERVO_CLAMP_US), SERVO_CLAMP_US) / SERVO_DIVISOR;
      }
    }

    // --- per-block detector parameters -------------------------------------
    const bool isArmed = armed_;
    const float ratio = settings.triggerRatio();
    const float absFloor = settings.absoluteFloor();
    const uint64_t blankSamples =
        static_cast<uint64_t>(settings.blankingMs) * AUDIO_SAMPLE_RATE / 1000;
    portENTER_CRITICAL(&armMux);
    const int64_t muteUntil = muteUntilUs;
    portEXIT_CRITICAL(&armMux);

    // --- envelope + threshold ----------------------------------------------
    for (size_t i = 0; i < frames; i++) {
      // 24 bits of signal live in the top of each 32-bit slot.
      const float a = fabsf(static_cast<float>(raw[i] >> 8));

      // Instant attack, exponential release: the envelope crosses the
      // threshold on the exact sample the transient arrives, which is what
      // makes the onset timestamp meaningful.
      if (a > env)
        env = a;
      else
        env += (a - env) * ENV_RELEASE;

      const uint64_t sampleIndex = samplesConsumed + i;
      if (sampleIndex < blankUntilSample) continue;

      const float threshold = std::max(floorLevel * ratio, absFloor);
      if (env > threshold) {
        blankUntilSample = sampleIndex + blankSamples;
        if (isArmed) {
          const int64_t atUs = anchorUs + static_cast<int64_t>(sampleIndex * US_PER_SAMPLE);
          if (atUs >= muteUntil) {
            // Full queue means the string already hit MAX_SHOTS_PER_STRING;
            // StringRun flags the overflow, so dropping here is safe.
            xQueueSend(queue_, &atUs, 0);
          }
        }
        continue;
      }

      // Only track the floor while nothing is going off, otherwise a string of
      // shots walks the floor up and the last shots go undetected.
      floorLevel += (env - floorLevel) * FLOOR_COEF;
    }

    samplesConsumed += frames;
    envelope_ = env;
    noiseFloor_ = floorLevel;
  }
}
