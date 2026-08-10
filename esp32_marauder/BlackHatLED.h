#pragma once

#include <Arduino.h>
#include "configs.h"

#ifdef HAS_BLACKHAT_LED

class BlackHatLED {
public:
  void RunSetup();
  void main(uint32_t currentTime);

  // Non-blocking blink
  void blinkRed(int count, int msOn, int msOff = 150);
  void blinkGreen(int count, int msOn, int msOff = 150);
  void blinkBlue(int count, int msOn, int msOff = 150);

  // Immediate set
  void setRed(bool on);
  void setGreen(bool on);
  void setBlue(bool on);

  // Attack indication
  void attackLED();

  // Mode constants (match Marauder convention)
  static constexpr uint8_t LED_MODE_OFF     = 0;
  static constexpr uint8_t LED_MODE_RAINBOW = 1;
  static constexpr uint8_t LED_MODE_ATTACK  = 2;
  static constexpr uint8_t LED_MODE_SNIFF   = 3;
  static constexpr uint8_t LED_MODE_CUSTOM  = 4;

private:
  struct BlinkState {
    int count;
    int msOn;
    int msOff;
    unsigned long lastToggle;
    bool ledOn;
    bool active;
  };

  BlinkState _redBlink;
  BlinkState _greenBlink;
  BlinkState _blueBlink;

  bool _redState  = false;
  bool _grnState  = false;
  bool _bluState  = false;

  void updateBlink(BlinkState& s, int pin);
};

extern BlackHatLED bh_led;

#endif // HAS_BLACKHAT_LED
