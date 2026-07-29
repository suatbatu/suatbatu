#include "Carousel.h"
#include "config.h"

// Half-step sequence for 28BYJ-48 (IN1..IN4).
#if MOTOR_DRIVER == DRIVER_ULN2003
static const uint8_t HALFSTEP[8] = {
  0b0001, 0b0011, 0b0010, 0b0110,
  0b0100, 0b1100, 0b1000, 0b1001
};
static const int STEP_DELAY_US = 1200;   // 28BYJ-48 needs a slow-ish rate
#else
static const int STEP_DELAY_US = 600;    // A4988 pulse spacing — tune to torque
#endif

void Carousel::begin(DoseSensors* sensors) {
  sensors_ = sensors;
#if MOTOR_DRIVER == DRIVER_ULN2003
  pinMode(PIN_IN1, OUTPUT); pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT); pinMode(PIN_IN4, OUTPUT);
  writeCoils(0);
#else
  pinMode(PIN_STEP, OUTPUT); pinMode(PIN_DIR, OUTPUT); pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, HIGH);   // disabled (active-low) until we move
#endif
}

void Carousel::writeCoils(uint8_t mask) {
#if MOTOR_DRIVER == DRIVER_ULN2003
  digitalWrite(PIN_IN1, (mask >> 0) & 1);
  digitalWrite(PIN_IN2, (mask >> 1) & 1);
  digitalWrite(PIN_IN3, (mask >> 2) & 1);
  digitalWrite(PIN_IN4, (mask >> 3) & 1);
#else
  (void)mask;
#endif
}

void Carousel::stepN(long steps, bool forward) {
  bool dir = forward ? (CAROUSEL_DIR != 0) : (CAROUSEL_DIR == 0);
#if MOTOR_DRIVER == DRIVER_ULN2003
  static int phase = 0;
  for (long i = 0; i < steps; i++) {
    phase += dir ? 1 : -1;
    if (phase < 0) phase = 7;
    if (phase > 7) phase = 0;
    writeCoils(HALFSTEP[phase]);
    delayMicroseconds(STEP_DELAY_US);
    if (sensors_) sensors_->update();     // keep drop-latch responsive
  }
#else
  digitalWrite(PIN_EN, LOW);              // enable driver
  digitalWrite(PIN_DIR, dir ? HIGH : LOW);
  delayMicroseconds(5);
  for (long i = 0; i < steps; i++) {
    digitalWrite(PIN_STEP, HIGH);
    delayMicroseconds(STEP_DELAY_US);
    digitalWrite(PIN_STEP, LOW);
    delayMicroseconds(STEP_DELAY_US);
    if (sensors_) sensors_->update();
  }
#endif
}

bool Carousel::advanceOneSlot() {
  long steps = STEPS_PER_REV / CAROUSEL_SLOTS;
  stepN(steps, true);
  slot_ = (slot_ + 1) % CAROUSEL_SLOTS;
  release();
  return true;
}

void Carousel::jog(long steps) {
  if (steps == 0) return;
  stepN(steps > 0 ? steps : -steps, steps > 0);
  release();
}

bool Carousel::home() {
  if (!sensors_) return false;

  // If already on the mark, step off it first so we detect a clean edge.
  if (sensors_->hallHome()) {
    stepN(STEPS_PER_REV / (CAROUSEL_SLOTS * 2), true);
  }

  // Seek the mark within ~1.5 revolutions.
  long limit = (STEPS_PER_REV * 3) / 2;
  long moved = 0;
  const long CHUNK = 4;
  while (moved < limit) {
    stepN(CHUNK, true);
    moved += CHUNK;
    if (sensors_->hallHome()) {
      slot_ = 0;
      release();
      return true;
    }
  }
  release();
  return false;   // mark never found -> fault
}

void Carousel::release() {
#if MOTOR_DRIVER == DRIVER_ULN2003
  writeCoils(0);
#else
  digitalWrite(PIN_EN, HIGH);   // disable driver (active-low)
#endif
}
