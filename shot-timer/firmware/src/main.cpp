// shot-timer — wiring only. The behaviour lives in TimerApp; this file maps
// three buttons onto it, keeps the screen and the web server fed, and gets out
// of the way of the audio task.
#include <Arduino.h>

#include "Buttons.h"
#include "Buzzer.h"
#include "Display.h"
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

  switch (app.state()) {
    case AppState::Ready:
      if (start == Press::Short) {
        display.resetReview();
        app.requestStart();
      }
      if (up == Press::Long) {
        app.openMenu();
      } else if (up == Press::Short && settings.sensitivity < 10) {
        settings.sensitivity++;
        markSettingsDirty();
      }
      if (down == Press::Short && settings.sensitivity > 1) {
        settings.sensitivity--;
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
  buttons.begin();
  buzzer.begin();

  if (!display.begin()) log_w("no OLED found — running headless");
  if (!storage.begin()) log_w("LittleFS mount failed — history and web UI unavailable");
  if (!detector.begin()) fatal("microphone/I2S init failed");

  net.begin();
  display.setNetLine(net.statusLine().c_str());

  app.onEvent([](const JsonDocument& doc) { web.pushEvent(doc); });
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
  net.loop();
  web.loop();
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
