# BlackHat ESP32-S3 — Marauder Build

ESP32Marauder v1.14.0 angepasst für **ESP32-S3-N16R8 (generisches DevKit)** mit:
- **OLED SSD1306 128x64** via I2C (SDA=8, SCL=9)
- **3 Status-LEDs** (Grün=4, Rot=5, Blau=6)
- **Headless CLI-Mode** — Steuerung über Serial (115200 Baud) oder WiFi

## Hardware

```
ESP32-S3-N16R8 DevKit
├── OLED SSD1306 128x64 (I2C 0x3C)
│   ├── SDA → GPIO 8
│   ├── SCL → GPIO 9
│   ├── VCC → 3.3V
│   └── GND → GND
├── LED Grün → GPIO 4 (über Vorwiderstand ~220Ω)
├── LED Rot  → GPIO 5 (über Vorwiderstand ~220Ω)
└── LED Blau → GPIO 6 (über Vorwiderstand ~220Ω)
```

## Build (Arduino IDE)

### 1. Board installieren
- Boardverwalter: **esp32 by Espressif Systems** (v3.x)
- Board: **ESP32S3 Dev Module**
- Flash Size: 16MB
- PSRAM: OPI PSRAM
- Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)

### 2. Libraries installieren (Sketch → Include Library → Manage Libraries)
- **Adafruit SSD1306** (by Adafruit)
- **Adafruit GFX Library** (by Adafruit)
- **TFT_eSPI** (by Bodmer) — NUR für Build-Kompatibilität, wird nicht genutzt

### 3. TFT_eSPI konfigurieren
Die `User_Setup_Select.h` im TFT_eSPI-Library-Ordner muss auf unsere Config zeigen:

Die Datei `User_Setup_blackhat_s3.h` aus diesem Repo in den TFT_eSPI-Library-Ordner kopieren, dann in `User_Setup_Select.h`:

```cpp
#include <User_Setup_blackhat_s3.h>
```

### 4. Öffnen & Flashen
- `esp32_marauder/esp32_marauder.ino` öffnen
- `configs.h` prüfen: `#define BLACKHAT_S3` muss aktiv sein (ist es standardmäßig)
- Kompilieren & Upload

## CLI Commands

Nach dem Boot über Serial Monitor (115200 Baud) verbinden:

```
help              — Alle Commands
scanap            — WiFi APs scannen
scansta           — Clients auf einem AP scannen (danach: `select -a <nr>`)
attack -t deauth  — Deauth-Angriff auf selektierten AP
attack -t deauth -c <mac>  — Gezielter Client-Deauth
attack -t beacon -l        — Beacon-Spam (Liste)
stop               — Angriff stoppen
list -a            — Gescannte APs auflisten
list -s            — Gescannte Stations/Clients
select -a <nr>     — AP auswählen
settings           — Einstellungen
```

Siehe [Marauder CLI Wiki](https://github.com/justcallmekoko/ESP32Marauder/wiki/cli) für alle Commands.

## OLED-Anzeige

Das OLED zeigt im 500ms-Rhythmus:
- **Mode + IP** (STA oder AP)
- **AP-Count / Client-Count**
- **DEAUTH-Status** mit Paketzähler (invertiert, wenn aktiv)
- **Uptime** und Marauder-Version

Beim Boot: BlackHat-Splashscreen für 1.5s.

## LED-Status

| LED | Bedeutung |
|-----|-----------|
| Grün | Boot blink (3x), dann aus |
| Rot | Deauth-Angriff aktiv (schnelles Blinken) |
| Blau | Wird bei manuellen Blink-Commands verwendet |

## WiFi

Der ESP32-S3 verbindet sich standardmäßig mit dem in den Settings hinterlegten WiFi. Falls keine Verbindung, startet er einen Access Point:

- **SSID:** `Marauder` (oder konfiguriert)
- **IP:** 192.168.4.1

## Geändert vs. Original Marauder

| Datei | Änderung |
|-------|----------|
| `configs.h` | `BLACKHAT_S3` Target, `HAS_BLACKHAT_LED`, `HAS_BLACKHAT_OLED`, GPIO-Pins |
| `WiFiScan.h` | `getAPcount()` public-Methode |
| `esp32_marauder.ino` | OLED/LED Init in setup(), Status-Refresh + Attack-LED in loop() |
| `User_Setup_Select.h` | `#include <User_Setup_blackhat_s3.h>` |
| **NEU** `BlackHatLED.h/.cpp` | RGB-LED-Blinking via GPIO |
| **NEU** `BlackHatDisplay.h/.cpp` | SSD1306 OLED Status-Display |
| **NEU** `User_Setup_blackhat_s3.h` | TFT_eSPI Stub (kein TFT genutzt) |
