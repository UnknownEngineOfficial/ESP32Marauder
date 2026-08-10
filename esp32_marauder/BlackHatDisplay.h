#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "configs.h"

#ifdef HAS_BLACKHAT_OLED

#define OLED_WIDTH  128
#define OLED_HEIGHT 64

class BlackHatDisplay {
public:
  void RunSetup();
  void main(uint32_t currentTime);

  void showSplash();
  void showStatus(const char* mode, const char* ip,
                  int wifiCount, int clientCount,
                  bool deauthActive, uint32_t deauthPkts);
  void clear();

  Adafruit_SSD1306 display;

private:
  unsigned long _lastRefresh = 0;
  static constexpr unsigned long REFRESH_MS = 500;
};

extern BlackHatDisplay bh_display;

#endif // HAS_BLACKHAT_OLED
