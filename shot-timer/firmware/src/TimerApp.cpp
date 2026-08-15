#include "TimerApp.h"

#include <esp_timer.h>
#include <math.h>

#include "Battery.h"
#include "Buzzer.h"
#include "Drills.h"
#include "ImpulseDetector.h"
#include "Settings.h"
#include "ShotDetector.h"
#include "Storage.h"
#include "config.h"

TimerApp app;

namespace {
// The microphone hears the buzzer far better than it hears anything downrange,
// so detection is muted for the tone plus a short tail for the piezo to stop
// ringing. A shot fired inside that window is not recorded — the same
// compromise every single-microphone timer makes.
constexpr int64_t BEEP_MUTE_TAIL_US = 40000;

// Par beeps are shorter and higher than the start beep so they cannot be
// mistaken for one, and so they mute the detector for less time.
constexpr uint16_t PAR_BEEP_MS = 120;
constexpr uint16_t PAR_BEEP_FREQ_HZ = 3400;
}  // namespace

void TimerApp::begin() { enter(AppState::Ready); }

void TimerApp::emit(const char* type, const std::function<void(JsonObject)>& fill) {
  if (!sink_) return;
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["type"] = type;
  if (fill) fill(o);
  sink_(doc);
}

void TimerApp::enter(AppState s) {
  state_ = s;
  emit("state", [this](JsonObject o) { statusJson(o); });
}

void TimerApp::requestStart() {
  switch (state_) {
    case AppState::Countdown:
    case AppState::Running:
      // Pressing start mid-string means "abort", not "start another".
      requestStop();
      return;
    case AppState::Menu:
      return;
    case AppState::Ready:
    case AppState::Review:
    default:
      break;
  }

  saveFailed_ = false;
  parFired_ = 0;
  detector.drain();
  impulse.drain();

  const uint32_t delayMs = settings.nextStartDelayMs();
  beepDueAtUs_ = esp_timer_get_time() + static_cast<int64_t>(delayMs) * 1000;
  enter(AppState::Countdown);

  if (delayMs == 0) fireStartBeep();
}

void TimerApp::fireStartBeep() {
  const int64_t beepAtUs = buzzer.beep(settings.beepFreqHz, settings.beepMs, settings.beepVolume);

  run_.begin(storage.nextId(), beepAtUs);
  const Drill& drill = drills.current();
  run_.setDrill(drills.active(), drill.name, drill.expectedShots);
  lastShotAtUs_ = beepAtUs;

  detector.muteUntil(beepAtUs + static_cast<int64_t>(settings.beepMs) * 1000 +
                     BEEP_MUTE_TAIL_US);

  // Only the sources this profile actually uses get armed. An unarmed source
  // costs nothing and, more importantly, cannot contribute a phantom shot.
  merge_.reset();
  const ShotSource source = settings.profile().shotSource;
  if (acousticEnabled(source)) detector.arm();
  if (impulseEnabled(source)) impulse.arm();

  enter(AppState::Running);
  emit("beep", nullptr);
}

void TimerApp::requestStop() {
  if (state_ == AppState::Countdown) {
    // Aborted before the beep: there is no string to keep.
    buzzer.stop();
    detector.disarm();
    impulse.disarm();
    enter(AppState::Ready);
    return;
  }
  if (state_ == AppState::Running) closeString("manual");
}

void TimerApp::closeString(const char* reason) {
  detector.disarm();
  impulse.disarm();
  // Anything already queued was fired before the stop, so it counts.
  ingestShots();
  run_.end();

  if (settings.autoSave && run_.count() > 0) {
    saveFailed_ = !storage.save(run_);
  }

  enter(AppState::Review);
  emit("end", [&](JsonObject o) {
    o["reason"] = reason;
    o["saveFailed"] = saveFailed_;
    if (run_.shotCountMismatch()) o["shotCountMismatch"] = true;
    run_.toJson(o["string"].to<JsonObject>());
  });
}

uint8_t TimerApp::parSchedule(const uint16_t*& times) const {
  const Drill& d = drills.current();
  if (d.parCount > 0) {
    times = d.parMs;
    return d.parCount;
  }
  if (settings.parEnabled) {
    times = settings.parMs;
    return settings.parCount;
  }
  times = nullptr;
  return 0;
}

void TimerApp::servicePar(int64_t nowUs) {
  const uint16_t* times = nullptr;
  const uint8_t count = parSchedule(times);
  if (!times || count == 0) return;

  while (parFired_ < count && parFired_ < MAX_PAR_TIMES) {
    const uint16_t offsetMs = times[parFired_];
    if (offsetMs == 0) {  // an unset slot ends the schedule
      parFired_ = count;
      return;
    }
    const int64_t dueAt = run_.beepAtUs() + static_cast<int64_t>(offsetMs) * 1000;
    if (nowUs < dueAt) return;

    const int64_t at = buzzer.beep(PAR_BEEP_FREQ_HZ, PAR_BEEP_MS, settings.beepVolume);
    detector.muteUntil(at + static_cast<int64_t>(PAR_BEEP_MS) * 1000 + BEEP_MUTE_TAIL_US);
    const uint8_t index = parFired_++;
    emit("par", [&](JsonObject o) {
      o["index"] = index;
      o["atMs"] = offsetMs;
    });
  }
}

void TimerApp::postStart() { pendingStart_ = true; }
void TimerApp::postStop() { pendingStop_ = true; }

void TimerApp::loop() {
  // Requests queued from other tasks are performed here, in loop()'s context.
  // Stop wins if both arrived in the same cycle: it is the safer of the two.
  if (pendingStop_) {
    pendingStop_ = false;
    pendingStart_ = false;
    requestStop();
  } else if (pendingStart_) {
    pendingStart_ = false;
    requestStart();
  }

  const int64_t nowUs = esp_timer_get_time();
  buzzer.tick(nowUs);

  switch (state_) {
    case AppState::Countdown:
      if (nowUs >= beepDueAtUs_) fireStartBeep();
      break;

    case AppState::Running: {
      ingestShots();
      servicePar(nowUs);

      if (settings.autoStopSec > 0) {
        const int64_t idleUs = nowUs - lastShotAtUs_;
        if (idleUs > static_cast<int64_t>(settings.autoStopSec) * 1000000LL) {
          closeString("timeout");
        }
      }
      break;
    }

    case AppState::Ready:
    case AppState::Review:
    case AppState::Menu:
    default:
      break;
  }
}

// One physical shot can be detected by both sources a few milliseconds apart.
// The merge rule that decides whether a candidate is a second shot or the same
// shot heard twice lives in ShotMerge.h and is host-tested — getting it wrong
// either doubles every shot or swallows genuine fast splits.
uint8_t TimerApp::ingestShots() {
  uint8_t added = 0;
  int64_t atUs;
  while (detector.popShot(atUs)) {
    if (!shouldAcceptShot(merge_, atUs, SHOT_MERGE_WINDOW_US)) continue;
    recordShot(atUs);
    added++;
  }
  while (impulse.popShot(atUs)) {
    if (!shouldAcceptShot(merge_, atUs, SHOT_MERGE_WINDOW_US)) continue;
    recordShot(atUs);
    added++;
  }
  return added;
}

void TimerApp::recordShot(int64_t atUs) {
  const uint8_t before = run_.count();
  run_.addShot(atUs);
  if (run_.count() == before) return;  // string is full
  noteAcceptedShot(merge_, atUs);
  lastShotAtUs_ = atUs;

  const uint8_t index = run_.count() - 1;
  emit("shot", [&](JsonObject o) {
    o["index"] = index;
    o["atMs"] = run_.shotMs(index);
    o["splitMs"] = run_.splitMs(index);
  });
}

void TimerApp::openMenu() {
  if (state_ == AppState::Countdown || state_ == AppState::Running) return;
  stateBeforeMenu_ = state_;
  enter(AppState::Menu);
}

void TimerApp::closeMenu() {
  if (state_ != AppState::Menu) return;
  settings.save();
  enter(stateBeforeMenu_ == AppState::Menu ? AppState::Ready : stateBeforeMenu_);
}

uint32_t TimerApp::elapsedMs() const {
  if (state_ == AppState::Running) return run_.elapsedMs(esp_timer_get_time());
  return run_.totalMs();
}

void TimerApp::statusJson(JsonObject out) const {
  out["state"] = appStateName(state_);
  out["elapsedMs"] = elapsedMs();
  out["armed"] = detector.armed();
  out["levelPerMille"] = detector.levelPerMille();
  out["floorPerMille"] = detector.floorPerMille();

  // Rejection counters matter: without them, a correctly working direction gate
  // and a broken microphone both look like "the timer is missing my shots".
  const DetectorStats st = detector.stats();
  JsonObject d = out["detector"].to<JsonObject>();
  d["accepted"] = st.accepted;
  d["rejectedEcho"] = st.rejectedEcho;
  d["rejectedOffAxis"] = st.rejectedOffAxis;
  d["lastLagSamples"] = st.lastLagSamples;
  d["lastAngleDeg"] = st.lastAngleDeg;
  d["lastConfidence"] = st.lastConfidence;
  d["secondMic"] = st.secondMicPresent;
  d["profile"] = settings.profile().name;
  d["directionGate"] = settings.profile().directionGate;
  d["shotSource"] = shotSourceName(settings.profile().shotSource);
  d["impulseAvailable"] = impulse.present();
  d["impulseDetected"] = impulse.detected();

  JsonObject dr = out["drill"].to<JsonObject>();
  dr["index"] = drills.active();
  dr["name"] = drills.current().name;
  dr["expectedShots"] = drills.current().expectedShots;
  {
    const uint16_t* times = nullptr;
    const uint8_t n = parSchedule(times);
    dr["parCount"] = n;
    JsonArray par = dr["parMs"].to<JsonArray>();
    for (uint8_t i = 0; i < n && times; i++) par.add(times[i]);
  }

  if (battery.present()) {
    JsonObject b = out["battery"].to<JsonObject>();
    b["volts"] = roundf(battery.volts() * 100.0f) / 100.0f;
    b["percent"] = battery.percent();
    b["low"] = battery.low();
    b["critical"] = battery.critical();
  }
  // Note: the time remaining in a countdown is deliberately never published.
  // Anyone who can see it has the start cue the random delay exists to remove.
  if (saveFailed_) out["saveFailed"] = true;
  run_.toJson(out["string"].to<JsonObject>());
}
