#include "BlackHatDisplay.h"

#ifdef HAS_BLACKHAT_OLED

BlackHatDisplay bh_display;

void BlackHatDisplay::RunSetup() {
  Wire.begin(BHOLED_SDA, BHOLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, BHOLED_ADDR)) {
    Serial.println(F("[BlackHatOLED] SSD1306 init FAILED"));
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("OLED ready.");
  display.display();

  Serial.println(F("[BlackHatOLED] Ready — SDA:8 SCL:9 0x3C"));
}

void BlackHatDisplay::main(uint32_t currentTime) {
  // Refresh handled externally via showStatus()
}

void BlackHatDisplay::showSplash() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(8, 8);
  display.println("BlackHat");
  display.setCursor(20, 32);
  display.println("ESP32-S3");

  display.setTextSize(1);
  display.setCursor(16, 52);
  display.println("Marauder " MARAUDER_VERSION);

  display.display();
}

void BlackHatDisplay::showStatus(const char* mode, const char* ip,
                                  int wifiCount, int clientCount,
                                  bool deauthActive, uint32_t deauthPkts) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print(mode);
  display.print(" ");
  display.println(ip);

  display.setCursor(0, 12);
  display.print("APs:");
  display.print(wifiCount);
  display.print("  Clients:");
  display.println(clientCount);

  display.setCursor(0, 24);
  if (deauthActive) {
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); // inverted
    display.print(" DEAUTH ");
    display.setTextColor(SSD1306_WHITE);
    display.print(" ");
    display.print(deauthPkts);
    display.println(" pkts");
  } else {
    display.println("IDLE");
  }

  // Footer
  display.setCursor(0, 48);
  display.setTextSize(1);
  display.print("Marauder ");
  display.println(MARAUDER_VERSION);

  // Right-side: uptime
  uint32_t sec = millis() / 1000;
  uint32_t min = sec / 60;
  uint32_t hr  = min / 60;
  display.setCursor(0, 56);
  display.print("Up: ");
  if (hr > 0) {
    display.print(hr);
    display.print("h");
  }
  display.print(min % 60);
  display.print("m");

  display.display();
}

void BlackHatDisplay::clear() {
  display.clearDisplay();
  display.display();
}

#endif // HAS_BLACKHAT_OLED
