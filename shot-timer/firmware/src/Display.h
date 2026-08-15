// 128x64 SSD1306 OLED — the screen you actually shoot off.
//
// Design rule for every screen: the number you need mid-string is the biggest
// thing on the glass, and it never moves or changes width as digits change.
#pragma once

#include <stdint.h>

class Display {
 public:
  bool begin();
  void tick();

  // Re-applies rotation and contrast. Call after changing either — the flip is
  // what makes a top-of-belt mount readable.
  void applyDisplaySettings();
  // Resets the auto-dim timer; any button press should call this.
  void noteActivity();

  // Review screen scrolling.
  void scrollReview(int8_t delta);
  void resetReview();

  // Menu navigation, driven from main.cpp.
  void menuMove(int8_t delta);
  void menuAdjust(int8_t delta);
  const char* menuItemName() const;

  void setNetLine(const char* s);

 private:
  void drawReady();
  void drawCountdown();
  void drawRunning();
  void drawReview();
  void drawMenu();
  void drawFooter();

  bool present_ = false;
  bool dimmed_ = false;
  uint32_t lastDrawMs_ = 0;
  uint32_t lastActivityMs_ = 0;
  int8_t reviewTop_ = 0;
  uint8_t menuIndex_ = 0;
  char netLine_[24] = "";
};

extern Display display;
