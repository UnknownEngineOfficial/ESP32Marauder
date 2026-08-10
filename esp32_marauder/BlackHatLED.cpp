#include "BlackHatLED.h"

#ifdef HAS_BLACKHAT_LED

BlackHatLED bh_led;

void BlackHatLED::RunSetup() {
  pinMode(BHLED_RED,   OUTPUT);
  pinMode(BHLED_GREEN, OUTPUT);
  pinMode(BHLED_BLUE,  OUTPUT);

  digitalWrite(BHLED_RED,   LOW);
  digitalWrite(BHLED_GREEN, LOW);
  digitalWrite(BHLED_BLUE,  LOW);

  // Startup flash: green
  digitalWrite(BHLED_GREEN, HIGH);
  delay(200);
  digitalWrite(BHLED_GREEN, LOW);

  Serial.println(F("[BlackHatLED] Ready — GPIOs R:4 G:5 B:6"));
}

void BlackHatLED::main(uint32_t currentTime) {
  updateBlink(_redBlink,   BHLED_RED);
  updateBlink(_greenBlink, BHLED_GREEN);
  updateBlink(_blueBlink,  BHLED_BLUE);

  // Continuous attack blink (fast red) when mode is ATTACK
  // (called separately from attackLED)
}

void BlackHatLED::attackLED() {
  // Quick single-frame red pulse — called from loop when deauth active
  static unsigned long lastAttack = 0;
  unsigned long now = millis();
  if (now - lastAttack > 100) {
    lastAttack = now;
    _redState = !_redState;
    digitalWrite(BHLED_RED, _redState ? HIGH : LOW);
  }
}

void BlackHatLED::blinkRed(int count, int msOn, int msOff) {
  _redBlink.count  = count;
  _redBlink.msOn   = msOn;
  _redBlink.msOff  = msOff;
  _redBlink.active = true;
  _redBlink.ledOn  = false;
  _redBlink.lastToggle = millis();
  digitalWrite(BHLED_RED, HIGH);
}

void BlackHatLED::blinkGreen(int count, int msOn, int msOff) {
  _greenBlink.count  = count;
  _greenBlink.msOn   = msOn;
  _greenBlink.msOff  = msOff;
  _greenBlink.active = true;
  _greenBlink.ledOn  = false;
  _greenBlink.lastToggle = millis();
  digitalWrite(BHLED_GREEN, HIGH);
}

void BlackHatLED::blinkBlue(int count, int msOn, int msOff) {
  _blueBlink.count  = count;
  _blueBlink.msOn   = msOn;
  _blueBlink.msOff  = msOff;
  _blueBlink.active = true;
  _blueBlink.ledOn  = false;
  _blueBlink.lastToggle = millis();
  digitalWrite(BHLED_BLUE, HIGH);
}

void BlackHatLED::setRed(bool on)   { _redState = on; digitalWrite(BHLED_RED,   on ? HIGH : LOW); }
void BlackHatLED::setGreen(bool on) { _grnState = on; digitalWrite(BHLED_GREEN, on ? HIGH : LOW); }
void BlackHatLED::setBlue(bool on)  { _bluState = on; digitalWrite(BHLED_BLUE,  on ? HIGH : LOW); }

void BlackHatLED::updateBlink(BlinkState& s, int pin) {
  if (!s.active) return;
  if (s.count <= 0) {
    s.active = false;
    digitalWrite(pin, LOW);
    return;
  }

  unsigned long now = millis();
  int interval = s.ledOn ? s.msOff : s.msOn;

  if (now - s.lastToggle > (unsigned long)interval) {
    s.lastToggle = now;
    s.ledOn = !s.ledOn;
    if (!s.ledOn) { // Just went OFF = one cycle done
      s.count--;
    }
    digitalWrite(pin, s.ledOn ? HIGH : LOW);

    if (s.count <= 0) {
      s.active = false;
      digitalWrite(pin, LOW);
    }
  }
}

#endif // HAS_BLACKHAT_LED
