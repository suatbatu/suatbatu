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

  uint32_t lastDrawMs_ = 0;
  int8_t reviewTop_ = 0;
  uint8_t menuIndex_ = 0;
  char netLine_[24] = "";
};

extern Display display;
