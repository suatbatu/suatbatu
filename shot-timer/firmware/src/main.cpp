// shot-timer — wiring only. The behaviour lives in TimerApp; this file maps
// three buttons onto it, keeps the screen and the web server fed, and gets out
// of the way of the audio task.
#include <Arduino.h>

#include "Battery.h"
#include "BleTimer.h"
#include "Buttons.h"
#include "Buzzer.h"
#include "Display.h"
#include "Drills.h"
#include "ImpulseDetector.h"
#include "Net.h"
#include "Settings.h"
#include "ShotDetector.h"
#include "Storage.h"
#include "TimerApp.h"
#include "WebInterface.h"
#include "config.h"

namespace {

// NVS has finite write endurance and the sensitivity buttons are easy to lean
// on, so changes made outside the menu are batched.
constexpr uint32_t SETTINGS_FLUSH_MS = 3000;
uint32_t settingsDirtyAtMs = 0;

void markSettingsDirty() { settingsDirtyAtMs = millis() ? millis() : 1; }

[[noreturn]] void fatal(const char* what) {
  log_e("fatal: %s", what);
  pinMode(PIN_LED, OUTPUT);
  for (;;) {
    // A timer that cannot hear is not a timer. Say so instead of pretending.
    Serial.printf("FATAL: %s\n", what);
    digitalWrite(PIN_LED, HIGH);
    delay(150);
    digitalWrite(PIN_LED, LOW);
    delay(850);
  }
}

void handleButtons() {
  const Press start = buttons.take(Btn::Start);
  const Press up = buttons.take(Btn::Up);
  const Press down = buttons.take(Btn::Down);

  if (start != Press::None || up != Press::None || down != Press::None) {
    display.noteActivity();
  }

  switch (app.state()) {
    case AppState::Ready:
      if (start == Press::Short) {
        display.resetReview();
        app.requestStart();
      }
      if (up == Press::Long) {
        app.openMenu();
      } else if (up == Press::Short && settings.profile().sensitivity < 10) {
        settings.profile().sensitivity++;
        markSettingsDirty();
      }
      if (down == Press::Short && settings.profile().sensitivity > 1) {
        settings.profile().sensitivity--;
        markSettingsDirty();
      }
      break;

    case AppState::Countdown:
    case AppState::Running:
      // Only one thing should be possible mid-string, and it is "stop".
      if (start != Press::None) app.requestStop();
      break;

    case AppState::Review:
      if (start == Press::Short) {
        display.resetReview();
        app.requestStart();
      }
      if (up == Press::Long) {
        app.openMenu();
      } else if (up == Press::Short) {
        display.scrollReview(-1);
      }
      if (down == Press::Short) display.scrollReview(1);
      break;

    case AppState::Menu:
      if (start == Press::Long) {
        app.closeMenu();  // saves
        settingsDirtyAtMs = 0;
      } else if (start == Press::Short) {
        display.menuMove(1);
      }
      if (up == Press::Short) display.menuAdjust(1);
      if (down == Press::Short) display.menuAdjust(-1);
      break;
  }
}

void updateLed() {
  const bool on = app.state() == AppState::Running || buzzer.sounding();
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  settings.load();
  drills.load();
  buttons.begin();
  buzzer.begin();
  battery.begin();

  if (!display.begin()) log_w("no OLED found — running headless");
  if (!storage.begin()) log_w("LittleFS mount failed — history and web UI unavailable");
  if (!detector.begin()) fatal("microphone/I2S init failed");
  // Optional. Its absence is a build choice, not a fault — profiles that ask
  // for impulse detection report it unavailable rather than silently
  // detecting nothing. Must follow display.begin(), which owns the I2C bus.
  impulse.begin();

  net.begin();
  display.setNetLine(net.statusLine().c_str());

  // One event sink, two subscribers. Both run in loop() context, so neither
  // can race the state machine that produced the event.
  app.onEvent([](const JsonDocument& doc) {
    web.pushEvent(doc);
    ble.onTimerEvent(doc);
  });
  ble.begin();
  if (!web.begin()) {
    // Not fatal: the device works standalone. The screen says why the web UI
    // is missing so it does not look like a network problem.
    display.setNetLine("web off: no pwd");
  }

  app.begin();
  log_i("%s v%s ready on %s", FW_NAME, FW_VERSION, net.ipString().c_str());
}

void loop() {
  buttons.poll();
  handleButtons();

  app.loop();
  battery.tick();
  net.loop();
  web.loop();
  ble.loop();
  display.tick();
  updateLed();

  if (settingsDirtyAtMs && (millis() - settingsDirtyAtMs) > SETTINGS_FLUSH_MS) {
    settings.save();
    settingsDirtyAtMs = 0;
  }

  // The audio task is priority 10 and never waits on this loop; yielding here
  // just keeps the idle task and the network stack fed.
  delay(2);
}
