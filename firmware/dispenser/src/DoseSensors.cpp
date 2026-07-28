#include "DoseSensors.h"
#include "config.h"

void DoseSensors::begin() {
  pinMode(PIN_IR_DROP, INPUT);          // input-only pins have no internal pullup
  pinMode(PIN_HALL_HOME, INPUT);
  pinMode(PIN_TAKEN_BTN, TAKEN_ACTIVE_LOW ? INPUT_PULLUP : INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_STATUS_LED, LOW);
}

void DoseSensors::update() {
  // Latch a beam-break edge (a pill falling briefly interrupts the beam).
  int beam = digitalRead(PIN_IR_DROP);
  bool broken = IR_BEAM_BROKEN_LOW ? (beam == LOW) : (beam == HIGH);
  if (broken) dropLatched_ = true;
  lastBeam_ = beam;
}

void DoseSensors::arm() {
  dropLatched_ = false;
}

bool DoseSensors::takenPressed() {
  bool raw = digitalRead(PIN_TAKEN_BTN);
  bool pressed = TAKEN_ACTIVE_LOW ? (raw == LOW) : (raw == HIGH);
  uint32_t now = millis();
  if (pressed && !lastBtnState_ && (now - lastBtnMs_) > 60) {  // debounce
    lastBtnState_ = true;
    lastBtnMs_    = now;
    return true;
  }
  if (!pressed) lastBtnState_ = false;
  return false;
}

bool DoseSensors::hallHome() {
  int v = digitalRead(PIN_HALL_HOME);
  return HALL_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

void DoseSensors::buzz(bool on) { digitalWrite(PIN_BUZZER, on ? HIGH : LOW); }
void DoseSensors::led(bool on)  { digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW); }

void DoseSensors::beep(uint16_t ms) {
  digitalWrite(PIN_BUZZER, HIGH);
  delay(ms);
  digitalWrite(PIN_BUZZER, LOW);
}
