#include "ShotDetector.h"

#include <driver/i2s_std.h>
#include <esp_timer.h>
#include <math.h>

#include <algorithm>

#include "Settings.h"
#include "Tdoa.h"
#include "config.h"

ShotDetector detector;

namespace {

constexpr float FULL_SCALE = 8388608.0f;  // 2^23, the mic's 24-bit full scale
constexpr float US_PER_SAMPLE = 1000000.0f / AUDIO_SAMPLE_RATE;

// Envelope release: ~15 ms. Long enough to ride over the ringing tail of a
// muzzle blast, short enough that the follower is back down before the next
// shot of a 0.15 s split.
constexpr float ENV_RELEASE_TAU_S = 0.015f;
// Noise floor: ~500 ms. It tracks wind, range chatter and the hum of the
// neighbouring bay without following a shot.
constexpr float FLOOR_TAU_S = 0.5f;

// Clock servo (see the comment in run()).
constexpr int64_t SERVO_CLAMP_US = 2000;
constexpr int64_t SERVO_RESET_US = 50000;
constexpr int SERVO_DIVISOR = 64;

// Below this correlation quality the two channels do not agree on anything and
// the lag estimate is noise. The gate fails *open* there: a shot we cannot
// localise is still a shot, and dropping real shots is a much worse failure
// than letting a neighbour's through.
constexpr uint32_t MIN_CONFIDENCE = 35;

// A second microphone is considered present if its channel ever carries signal.
// Wiring only one mic leaves the right slot at zero, and the direction gate
// then has nothing to work with — so it disables itself rather than silently
// rejecting everything.
constexpr float SECOND_MIC_PRESENT_RMS = 200.0f;

constexpr int TDOA_WINDOW = TDOA_PRE_SAMPLES + TDOA_POST_SAMPLES;

i2s_chan_handle_t rxChan = nullptr;
portMUX_TYPE armMux = portMUX_INITIALIZER_UNLOCKED;
int64_t muteUntilUs = 0;

// Delay line for the cross-correlation, in 16-bit because that is plenty for a
// lag estimate and halves the memory. 1024 samples * 2 ch * 2 B = 4 KB.
int16_t ringA[TDOA_RING_SAMPLES];
int16_t ringB[TDOA_RING_SAMPLES];

// A candidate onset waiting for enough post-roll to be correlated.
struct Pending {
  uint64_t sampleIndex;
  float peak;
};

float tauCoef(float tauSeconds) {
  return 1.0f - expf(-1.0f / (tauSeconds * AUDIO_SAMPLE_RATE));
}

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
      // Stereo: mic A in the left slot (L/R to GND), mic B in the right
      // (L/R to VDD). Both share the same three wires.
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                      I2S_SLOT_MODE_STEREO),
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

  if (i2s_channel_init_std_mode(rxChan, &stdCfg) != ESP_OK) return false;
  if (i2s_channel_enable(rxChan) != ESP_OK) return false;

  // Priority 10: well above the Arduino loop (1) so audio is never starved,
  // well below the Wi-Fi stack so the radio still behaves. Core 1 keeps it off
  // the core the network stack lives on.
  const BaseType_t ok =
      xTaskCreatePinnedToCore(taskTrampoline, "shotdet", 5120, this, 10, &task_, 1);
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

DetectorStats ShotDetector::stats() const {
  DetectorStats s;
  s.accepted = accepted_;
  s.rejectedEcho = rejectedEcho_;
  s.rejectedOffAxis = rejectedOffAxis_;
  s.lastLagSamples = static_cast<int16_t>(lastLag_);
  s.lastAngleDeg = static_cast<int16_t>(lastAngle_);
  s.lastConfidence = static_cast<uint8_t>(lastConfidence_);
  s.secondMicPresent = secondMic_;
  return s;
}

void ShotDetector::resetStats() {
  accepted_ = 0;
  rejectedEcho_ = 0;
  rejectedOffAxis_ = 0;
}

void ShotDetector::taskTrampoline(void* arg) {
  static_cast<ShotDetector*>(arg)->run();
}

void ShotDetector::run() {
  // One DMA block, interleaved L/R.
  static int32_t raw[AUDIO_FRAMES_PER_BLOCK * 2];

  const float envRelease = tauCoef(ENV_RELEASE_TAU_S);
  const float floorCoef = tauCoef(FLOOR_TAU_S);

  float env = 0.0f;
  float floorLevel = FULL_SCALE / 1000.0f;  // start high so boot noise cannot trigger
  float echoGuard = 0.0f;
  float rmsB = 0.0f;

  uint64_t samplesConsumed = 0;
  uint64_t refractoryUntil = 0;
  int64_t anchorUs = 0;  // esp_timer time of sample 0
  bool anchored = false;

  Pending pending[TDOA_MAX_PENDING];
  uint8_t pendingCount = 0;

  for (;;) {
    size_t bytesRead = 0;
    if (i2s_channel_read(rxChan, raw, sizeof(raw), &bytesRead, portMAX_DELAY) != ESP_OK) {
      continue;
    }
    const size_t frames = bytesRead / (sizeof(int32_t) * 2);
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
    const Profile& prof = settings.profile();
    const float ratio = settings.triggerRatio();
    const float absFloor = settings.absoluteFloor();
    const uint64_t refractorySamples =
        static_cast<uint64_t>(prof.refractoryMs) * AUDIO_SAMPLE_RATE / 1000;
    const float echoStartFactor = powf(10.0f, -static_cast<float>(prof.echoRejectDb) / 20.0f);
    const float echoDecay = 1.0f - tauCoef(prof.echoDecayMs / 1000.0f);

    // --- envelope, threshold, echo guard -----------------------------------
    for (size_t i = 0; i < frames; i++) {
      // 24 bits of signal live in the top of each 32-bit slot.
      const int32_t sa = raw[i * 2] >> 8;
      const int32_t sb = raw[i * 2 + 1] >> 8;
      const float a = fabsf(static_cast<float>(sa));

      const uint64_t sampleIndex = samplesConsumed + i;
      ringA[sampleIndex & (TDOA_RING_SAMPLES - 1)] = static_cast<int16_t>(sa >> 8);
      ringB[sampleIndex & (TDOA_RING_SAMPLES - 1)] = static_cast<int16_t>(sb >> 8);

      // Instant attack, exponential release: the envelope crosses the
      // threshold on the exact sample the transient arrives, which is what
      // makes the onset timestamp meaningful.
      if (a > env)
        env = a;
      else
        env += (a - env) * envRelease;

      // The echo guard always decays, whether or not anything is happening.
      echoGuard *= echoDecay;

      if (sampleIndex < refractoryUntil) continue;

      const float threshold = std::max(floorLevel * ratio, absFloor);
      if (env > threshold) {
        refractoryUntil = sampleIndex + refractorySamples;
        if (env <= echoGuard) {
          // Loud, but not loud enough to be anything but shot N's reflection.
          rejectedEcho_ = rejectedEcho_ + 1;
        } else if (pendingCount < TDOA_MAX_PENDING) {
          pending[pendingCount++] = {sampleIndex, env};
        }
        echoGuard = std::max(echoGuard, env * echoStartFactor);
        continue;
      }

      // Only track the floor while nothing is going off, otherwise a string of
      // shots walks the floor up and the last shots go undetected.
      floorLevel += (env - floorLevel) * floorCoef;
    }

    samplesConsumed += frames;
    envelope_ = env;
    noiseFloor_ = floorLevel;

    // --- direction gate ----------------------------------------------------
    // Deferred by design: correlating needs post-roll that only exists once the
    // following block has arrived. The reported time still comes from the onset
    // sample, so the extra few milliseconds of latency cost no accuracy.
    const bool isArmed = armed_;
    portENTER_CRITICAL(&armMux);
    const int64_t muteUntil = muteUntilUs;
    portEXIT_CRITICAL(&armMux);

    uint8_t keep = 0;
    for (uint8_t p = 0; p < pendingCount; p++) {
      const Pending& cand = pending[p];
      if (samplesConsumed < cand.sampleIndex + TDOA_POST_SAMPLES) {
        pending[keep++] = cand;  // not ripe yet, carry it to the next block
        continue;
      }

      const int64_t atUs = anchorUs + static_cast<int64_t>(cand.sampleIndex * US_PER_SAMPLE);
      if (!isArmed || atUs < muteUntil) continue;

      const float spacingM = settings.micSpacingMm / 1000.0f;
      const LagResult lr =
          estimateLag(ringA, ringB, TDOA_RING_SAMPLES, cand.sampleIndex - TDOA_PRE_SAMPLES,
                      samplesConsumed, TDOA_WINDOW, TDOA_MAX_LAG);

      rmsB = sqrtf(lr.energyB / TDOA_WINDOW);
      const bool haveSecondMic = rmsB > SECOND_MIC_PRESENT_RMS;
      secondMic_ = haveSecondMic;
      lastLag_ = lr.lag;
      lastConfidence_ = lr.confidence;
      lastAngle_ =
          lagToAngleDeg(lr.lag, spacingM, AUDIO_SAMPLE_RATE, SPEED_OF_SOUND_MPS);

      // Fail open on every uncertainty: the window fell outside the ring, no
      // second microphone is fitted, or the two channels do not correlate well
      // enough to place the source. Dropping a real shot is a far worse failure
      // than letting a neighbour's through.
      const bool gateApplies = prof.directionGate && lr.valid && haveSecondMic &&
                               lr.confidence >= MIN_CONFIDENCE;
      const int accept = maxLagForAngle(prof.maxOffAxisDeg, spacingM, AUDIO_SAMPLE_RATE,
                                        SPEED_OF_SOUND_MPS, TDOA_MAX_LAG);
      if (gateApplies && abs(lr.lag) > accept) {
        rejectedOffAxis_ = rejectedOffAxis_ + 1;
        continue;
      }

      if (xQueueSend(queue_, &atUs, 0) == pdTRUE) {
        accepted_ = accepted_ + 1;
      }
      // A full queue means the string already hit MAX_SHOTS_PER_STRING;
      // StringRun flags the overflow, so dropping here is safe.
    }
    pendingCount = keep;
  }
}
