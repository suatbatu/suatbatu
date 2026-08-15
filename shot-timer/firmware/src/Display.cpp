#include "Display.h"

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

#include <algorithm>

#include "Settings.h"
#include "ShotDetector.h"
#include "Storage.h"
#include "TimerApp.h"
#include "config.h"

Display display;

namespace {

// Pins are set on the Wire bus rather than in the constructor: U8g2's hardware
// I2C backend just calls Wire.begin(), so the bus has to be configured first.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, /*reset=*/U8X8_PIN_NONE);

constexpr uint8_t SSD1306_I2C_ADDR = 0x3C;

// A full 1 KB frame over 400 kHz I2C costs about 25 ms. 15 Hz keeps the timer
// readable while leaving the Arduino loop most of its time; the detector runs
// in its own task and does not care either way.
constexpr uint32_t FRAME_INTERVAL_MS = 66;

constexpr uint8_t REVIEW_ROWS = 4;

const uint8_t* FONT_SMALL = u8g2_font_5x8_tf;
const uint8_t* FONT_MED = u8g2_font_7x13B_tf;
const uint8_t* FONT_BIG = u8g2_font_logisoso30_tn;

// Shot timers speak in hundredths. Fixed two decimals, always.
void fmtSeconds(char* buf, size_t n, uint32_t ms) {
  snprintf(buf, n, "%lu.%02lu", static_cast<unsigned long>(ms / 1000),
           static_cast<unsigned long>((ms % 1000) / 10));
}

// The big font is digits-only (logisoso30_tn), so a value like "RANDOM" has to
// fall back to the medium font rather than render as nothing at all.
bool isNumericish(const char* s) {
  for (const char* p = s; *p; p++) {
    if (!isdigit(static_cast<unsigned char>(*p)) && *p != '.' && *p != ':' && *p != '-') {
      return false;
    }
  }
  return true;
}

void drawCentredBig(const char* s, int y) {
  oled.setFont(isNumericish(s) ? FONT_BIG : FONT_MED);
  const int w = oled.getStrWidth(s);
  oled.drawStr((128 - w) / 2, y, s);
}

const char* delayModeName() {
  switch (settings.delayMode) {
    case DELAY_RANDOM: return "RANDOM";
    case DELAY_FIXED: return "FIXED";
    case DELAY_INSTANT: return "INSTANT";
  }
  return "?";
}

// ------------------------------------------------------------------ menu ----
template <typename T>
T stepClamped(T v, int8_t d, T step, T lo, T hi) {
  const long next = static_cast<long>(v) + static_cast<long>(d) * static_cast<long>(step);
  return static_cast<T>(std::min<long>(std::max<long>(next, lo), hi));
}

struct MenuItem {
  const char* name;
  void (*value)(char* buf, size_t n);
  void (*adjust)(int8_t d);
};

const MenuItem MENU[] = {
    {"Sensitivity",
     [](char* b, size_t n) { snprintf(b, n, "%u", settings.sensitivity); },
     [](int8_t d) { settings.sensitivity = stepClamped<uint8_t>(settings.sensitivity, d, 1, 1, 10); }},

    {"Start delay",
     [](char* b, size_t n) { snprintf(b, n, "%s", delayModeName()); },
     [](int8_t d) {
       const int next = (static_cast<int>(settings.delayMode) + (d > 0 ? 1 : 2)) % 3;
       settings.delayMode = static_cast<StartDelayMode>(next);
     }},

    {"Delay min",
     [](char* b, size_t n) { fmtSeconds(b, n, settings.delayMinMs); },
     [](int8_t d) {
       settings.delayMinMs = stepClamped<uint16_t>(settings.delayMinMs, d, 100, 0, 30000);
       if (settings.delayMinMs > settings.delayMaxMs) settings.delayMaxMs = settings.delayMinMs;
     }},

    {"Delay max",
     [](char* b, size_t n) { fmtSeconds(b, n, settings.delayMaxMs); },
     [](int8_t d) {
       settings.delayMaxMs = stepClamped<uint16_t>(settings.delayMaxMs, d, 100, 0, 30000);
       if (settings.delayMaxMs < settings.delayMinMs) settings.delayMinMs = settings.delayMaxMs;
     }},

    {"Par time",
     [](char* b, size_t n) {
       if (!settings.parEnabled) {
         snprintf(b, n, "OFF");
         return;
       }
       fmtSeconds(b, n, settings.parMs[0]);
     },
     [](int8_t d) {
       if (!settings.parEnabled) {
         if (d > 0) settings.parEnabled = true;
         return;
       }
       const uint16_t next = stepClamped<uint16_t>(settings.parMs[0], d, 100, 0, 60000);
       // Stepping the first par time down past zero is how you turn par off.
       if (next == 0 && d < 0) settings.parEnabled = false;
       settings.parMs[0] = next == 0 ? 100 : next;
     }},

    {"Buzzer vol",
     [](char* b, size_t n) { snprintf(b, n, "%u", settings.beepVolume); },
     [](int8_t d) { settings.beepVolume = stepClamped<uint8_t>(settings.beepVolume, d, 1, 1, 10); }},

    {"Auto stop",
     [](char* b, size_t n) {
       if (settings.autoStopSec == 0)
         snprintf(b, n, "MANUAL");
       else
         snprintf(b, n, "%us", settings.autoStopSec);
     },
     [](int8_t d) { settings.autoStopSec = stepClamped<uint16_t>(settings.autoStopSec, d, 5, 0, 600); }},

    {"Blanking",
     [](char* b, size_t n) { snprintf(b, n, "%ums", settings.blankingMs); },
     [](int8_t d) { settings.blankingMs = stepClamped<uint16_t>(settings.blankingMs, d, 10, 20, 500); }},
};
constexpr uint8_t MENU_COUNT = sizeof(MENU) / sizeof(MENU[0]);

}  // namespace

bool Display::begin() {
  Wire.setPins(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.begin();
  Wire.setClock(400000);

  // U8g2's begin() cannot report a missing panel, so probe for it first —
  // otherwise a device with no OLED plugged in silently draws into the void.
  Wire.beginTransmission(SSD1306_I2C_ADDR);
  if (Wire.endTransmission() != 0) return false;

  oled.begin();
  oled.setFontPosBaseline();
  oled.clearBuffer();
  oled.setFont(FONT_MED);
  oled.drawStr(0, 20, FW_NAME);
  oled.setFont(FONT_SMALL);
  oled.drawStr(0, 36, "v" FW_VERSION);
  oled.sendBuffer();
  return true;
}

void Display::setNetLine(const char* s) {
  strncpy(netLine_, s, sizeof(netLine_) - 1);
  netLine_[sizeof(netLine_) - 1] = '\0';
}

void Display::scrollReview(int8_t delta) {
  const int maxTop = std::max(0, static_cast<int>(app.run().count()) - REVIEW_ROWS);
  reviewTop_ = std::min(std::max(reviewTop_ + delta, 0), maxTop);
}

void Display::resetReview() { reviewTop_ = 0; }

void Display::menuMove(int8_t delta) {
  menuIndex_ = (menuIndex_ + MENU_COUNT + delta) % MENU_COUNT;
}

void Display::menuAdjust(int8_t delta) { MENU[menuIndex_].adjust(delta); }

const char* Display::menuItemName() const { return MENU[menuIndex_].name; }

void Display::tick() {
  const uint32_t now = millis();
  if (now - lastDrawMs_ < FRAME_INTERVAL_MS) return;
  lastDrawMs_ = now;

  oled.clearBuffer();
  switch (app.state()) {
    case AppState::Ready: drawReady(); break;
    case AppState::Countdown: drawCountdown(); break;
    case AppState::Running: drawRunning(); break;
    case AppState::Review: drawReview(); break;
    case AppState::Menu: drawMenu(); break;
  }
  oled.sendBuffer();
}

void Display::drawFooter() {
  oled.setFont(FONT_SMALL);
  oled.drawStr(0, 63, netLine_);
}

void Display::drawReady() {
  oled.setFont(FONT_MED);
  oled.drawStr(0, 12, "READY");

  char buf[32];
  oled.setFont(FONT_SMALL);
  if (settings.delayMode == DELAY_RANDOM) {
    char lo[8], hi[8];
    fmtSeconds(lo, sizeof(lo), settings.delayMinMs);
    fmtSeconds(hi, sizeof(hi), settings.delayMaxMs);
    snprintf(buf, sizeof(buf), "Delay %s-%ss", lo, hi);
  } else {
    char v[8];
    fmtSeconds(v, sizeof(v), settings.delayMode == DELAY_FIXED ? settings.delayFixedMs : 0);
    snprintf(buf, sizeof(buf), "Delay %s %ss", delayModeName(), v);
  }
  oled.drawStr(0, 26, buf);

  snprintf(buf, sizeof(buf), "Sens %u   Par %s", settings.sensitivity,
           settings.parEnabled ? "ON" : "off");
  oled.drawStr(0, 36, buf);

  // A live noise bar: point the mic downrange, watch it sit low, and you know
  // the sensitivity is not going to trip on wind before the beep.
  const int w = detector.levelPerMille() * 126 / 1000;
  oled.drawFrame(0, 42, 128, 8);
  if (w > 0) oled.drawBox(1, 43, std::min(w, 126), 6);

  drawFooter();
}

void Display::drawCountdown() {
  // Deliberately shows nothing about how much delay is left. Displaying a
  // countdown next to the shooter would hand back the start cue that the
  // random delay exists to take away.
  oled.setFont(FONT_MED);
  const char* s = "STAND BY";
  oled.drawStr((128 - oled.getStrWidth(s)) / 2, 38, s);
}

void Display::drawRunning() {
  char buf[16];
  const StringRun& run = app.run();

  oled.setFont(FONT_SMALL);
  snprintf(buf, sizeof(buf), "SHOTS %u", run.count());
  oled.drawStr(0, 8, buf);

  if (run.count() >= 2) {
    char sp[8];
    fmtSeconds(sp, sizeof(sp), run.splitMs(run.count() - 1));
    snprintf(buf, sizeof(buf), "SPLIT %s", sp);
    oled.drawStr(128 - oled.getStrWidth(buf), 8, buf);
  }

  fmtSeconds(buf, sizeof(buf), app.elapsedMs());
  drawCentredBig(buf, 46);

  if (run.count() >= 1) {
    char f[8];
    fmtSeconds(f, sizeof(f), run.firstShotMs());
    oled.setFont(FONT_SMALL);
    snprintf(buf, sizeof(buf), "1st %s", f);
    oled.drawStr(0, 63, buf);
  }
}

void Display::drawReview() {
  const StringRun& run = app.run();
  char buf[32], a[8], b[8];

  oled.setFont(FONT_SMALL);
  fmtSeconds(a, sizeof(a), run.firstShotMs());
  fmtSeconds(b, sizeof(b), run.totalMs());
  snprintf(buf, sizeof(buf), "n=%u  1st %s  tot %s", run.count(), a, b);
  oled.drawStr(0, 8, buf);
  oled.drawHLine(0, 11, 128);

  if (run.count() == 0) {
    oled.setFont(FONT_MED);
    oled.drawStr(0, 34, "NO SHOTS");
    drawFooter();
    return;
  }

  oled.setFont(FONT_SMALL);
  for (uint8_t row = 0; row < REVIEW_ROWS; row++) {
    const uint8_t i = reviewTop_ + row;
    if (i >= run.count()) break;
    fmtSeconds(a, sizeof(a), run.shotMs(i));
    fmtSeconds(b, sizeof(b), run.splitMs(i));
    snprintf(buf, sizeof(buf), "%2u  %6s  %s%s", i + 1, a, i == 0 ? "draw " : "+", i == 0 ? "" : b);
    oled.drawStr(0, 22 + row * 10, buf);
  }

  if (run.count() > REVIEW_ROWS) {
    // Scrollbar so it is obvious there are more shots below the fold.
    const int barH = std::max(4, 40 * REVIEW_ROWS / run.count());
    const int barY = 14 + (40 - barH) * reviewTop_ /
                              std::max<int>(1, run.count() - REVIEW_ROWS);
    oled.drawFrame(124, 14, 4, 40);
    oled.drawBox(125, barY + 1, 2, barH);
  }

  if (app.lastSaveFailed()) {
    oled.setFont(FONT_SMALL);
    oled.drawStr(0, 63, "! not saved");
  } else {
    drawFooter();
  }
}

void Display::drawMenu() {
  char buf[24];
  oled.setFont(FONT_SMALL);
  oled.drawStr(0, 8, "SETTINGS");
  snprintf(buf, sizeof(buf), "%u/%u", menuIndex_ + 1, MENU_COUNT);
  oled.drawStr(128 - oled.getStrWidth(buf), 8, buf);
  oled.drawHLine(0, 11, 128);

  oled.setFont(FONT_MED);
  oled.drawStr(0, 30, MENU[menuIndex_].name);

  MENU[menuIndex_].value(buf, sizeof(buf));
  drawCentredBig(buf, 60);
}
