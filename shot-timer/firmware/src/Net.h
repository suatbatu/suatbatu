// Wi-Fi bring-up.
//
// A shot timer lives at a range, and ranges do not have Wi-Fi. So the SoftAP is
// the normal case, not the fallback: if no station credentials are configured,
// or the configured network is not there within a few seconds, the device
// serves its own network and you connect your phone to that.
#pragma once

#include <Arduino.h>

class Net {
 public:
  void begin();
  void loop();

  bool isAp() const { return isAp_; }
  // "AP ShotTimer-A1B2" or "192.168.4.1" — whatever the OLED footer should say.
  const String& statusLine() const { return statusLine_; }
  String ipString() const;

 private:
  void startAp();

  bool isAp_ = true;
  uint32_t lastCheckMs_ = 0;
  String statusLine_;
  String apSsid_;
};

extern Net net;
