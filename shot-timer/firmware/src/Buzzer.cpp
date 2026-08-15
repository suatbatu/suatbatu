#include "Buzzer.h"

#include <Arduino.h>
#include <esp_timer.h>

#include "config.h"

Buzzer buzzer;

void Buzzer::begin() {
  ledcAttach(PIN_BUZZER, 2700, BUZZER_LEDC_RESOLUTION);
  ledcWrite(PIN_BUZZER, 0);
}

int64_t Buzzer::beep(uint16_t freqHz, uint16_t durationMs, uint8_t volume) {
  const uint32_t full = (1u << BUZZER_LEDC_RESOLUTION);
  // A piezo is loudest at 50% duty. "Volume" narrows the pulse from there,
  // which is a coarse but effective attenuator on a two-terminal sounder.
  const uint32_t maxDuty = full / 2;
  const uint32_t duty = maxDuty * (volume < 1 ? 1 : (volume > 10 ? 10 : volume)) / 10;

  ledcChangeFrequency(PIN_BUZZER, freqHz, BUZZER_LEDC_RESOLUTION);
  const int64_t startedAt = esp_timer_get_time();
  ledcWrite(PIN_BUZZER, duty);

  sounding_ = true;
  stopAtUs_ = startedAt + static_cast<int64_t>(durationMs) * 1000;
  return startedAt;
}

void Buzzer::stop() {
  ledcWrite(PIN_BUZZER, 0);
  sounding_ = false;
}

void Buzzer::tick(int64_t nowUs) {
  if (sounding_ && nowUs >= stopAtUs_) stop();
}

void Buzzer::blip() { beep(3200, 25, 4); }
