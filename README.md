# ESP32 Marauder — Unknown Engine Fork

<p align="center"><img alt="Marauder logo" src="https://github.com/justcallmekoko/ESP32Marauder/blob/master/pictures/marauder_skull_patch_04_full_final.png?raw=true" width="300"></p>

<p align="center">
  <b>A suite of WiFi/Bluetooth offensive and defensive tools for the ESP32</b>
  <br>
  <i>Forked from <a href="https://github.com/justcallmekoko/ESP32Marauder">justcallmekoko/ESP32Marauder</a> — upstream synced to v1.14.1</i>
</p>

---

## 🔩 Unknown Engine Changes

This fork adds first-class support for the **BlackHat ESP32-S3** with ESP32 core 3.x and a **REST API + PWA Web Interface** for full remote control.

### Added Features

| Feature | Description |
|---------|-------------|
| 🖤 **BlackHat S3 Port** | Full hardware support: display, LED, buttons, battery for the BlackHat ESP32-S3 board running ESP32 Arduino core 3.x |
| 🌐 **REST API Server** | Full REST API on port 80 — control every Marauder feature via HTTP. Connects to your phone's hotspot and announces via mDNS (`marauder.local`) |
| 📱 **PWA Web Interface** | Progressive Web App served from ESP32 — mobile dashboard with live AP/Station/BLE tables, one-tap scan & attack controls, dark theme. **Service Worker caches the UI on first load, so the dashboard survives WiFi disconnects during attacks (promiscuous mode).** Auto-reconnects when ESP32 comes back online. Installable to home screen. |

### API Quick Reference

Base URL: `http://marauder.local` (or the device IP)

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/api/status` | Device status (version, scan mode, channel, counts, heap, GPS, battery) |
| `POST` | `/api/scan/start?type=ap` | Start scan (ap, sta, all, probe, pwn, deauth, eapol, raw, beacon, packet, sae, pinescan, multissid) |
| `POST` | `/api/scan/stop` | Stop all scans |
| `POST` | `/api/attack/start?type=deauth` | Start attack (deauth, beacon, probe, badmsg, sae, csa, quiet) |
| `POST` | `/api/attack/stop` | Stop all attacks |
| `POST` | `/api/wardrive?action=start` | Start/stop wardriving |
| `POST` | `/api/foxhunt` | Start fox hunt (signal strength tracking) |
| `POST` | `/api/evilportal?action=start` | Evil Portal: start/stop/status/ack/reset |
| `POST` | `/api/pingscan` | Ping scan connected network |
| `POST` | `/api/portscan` | Port scan common services |
| `POST` | `/api/bt/scan` | Bluetooth scan |
| `POST` | `/api/bt/spam?type=sourapple` | BT spam: sourapple, applejuice, swiftpair |
| `POST` | `/api/bt/findmy?action=scan` | FindMy tag scan & sound trigger |
| `GET` | `/api/data/ap` | Get AP list as JSON |
| `GET` | `/api/data/station` | Get station list as JSON |
| `GET` | `/api/data/ble` | Get BLE device list as JSON |
| `GET` | `/api/data/rawstats` | Packet statistics |
| `GET` | `/api/gps` | GPS data (fix, lat/lon, altitude, satellites) |
| `POST` | `/api/gps/tracker?action=start` | GPS tracker start/stop |
| `POST` | `/api/upload?dest=wigle` | Upload wardrive data to WiGLE/WDG |
| `POST` | `/api/channel?channel=6` | Set Wi-Fi channel |
| `POST` | `/api/clearlist?list=ap` | Clear lists (ap, station, ssid, all) |
| `POST` | `/api/join?ssid=...&password=...` | Join a Wi-Fi network |
| `POST` | `/api/mac?action=randap` | MAC spoofing (randap, randsta, cloneap, clonesta) |
| `POST` | `/api/save?what=ap` | Save/load AP/SSID lists |
| `POST` | `/api/reboot` | Reboot device |
| `GET` | `/api/files` | List SD card files |
| `DELETE` | `/api/files/delete?name=...` | Delete SD card file |

Full API spec and Web Interface setup below.

---

## 🚀 Quick Start

### 1. Flash the Firmware

Select **BlackHat ESP32-S3** in Arduino IDE or PlatformIO, flash from this repo.

### 2. Configure Hotspot

Edit `esp32_marauder/api_server.h`:

```cpp
#define API_HOTSPOT_SSID "Your-Hotspot-Name"
#define API_HOTSPOT_PASSWORD "your-password"
```

Or set defines in `configs.h`. Enable the API server:

```cpp
#define HAS_API_SERVER
```

### 3. Connect

- ESP32 boots → connects to your hotspot → announces via mDNS as `marauder.local`
- If hotspot not found: falls back to AP mode (`Marauder-Setup` / password `marauder123`)

### 4. Use the Web Interface

- Open `http://marauder.local` in any browser (phone or desktop)
- **First load:** ESP32 serves the dashboard; Service Worker registers and caches everything
- **After first load:** Dashboard loads from cache — works even while ESP32 is in promiscuous mode running attacks
- **During attacks:** Red "⚠ ESP32 OFFLINE" bar with reconnect counter; UI stays fully interactive
- **Auto-reconnect:** When attack stops and WiFi reconnects, dashboard auto-detects and resumes live data
- **Add to Home Screen:** Tap "📲 Add to Home Screen" button — runs like a native app, fully offline-capable
- Files served: `/` (HTML), `/sw.js` (Service Worker), `/manifest.json` (PWA metadata) — all from ESP32 PROGMEM

### 5. API Access

Any HTTP client can control the Marauder:

```bash
# Check status
curl http://marauder.local/api/status

# Start AP scan
curl -X POST http://marauder.local/api/scan/start?type=ap

# Get AP list
curl http://marauder.local/api/data/ap

# Beacon spam
curl -X POST http://marauder.local/api/attack/start?type=beacon
```

---

## ⚠️ Disclaimer

This tool is intended for educational, research and authorized security testing purposes only. The user assumes all responsibility for compliance with applicable laws. **Do not use on networks you do not own or have explicit permission to test.**

---

## 📜 Credits

- **Original Marauder:** [justcallmekoko/ESP32Marauder](https://github.com/justcallmekoko/ESP32Marauder) — the legendary ESP32 hacking toolkit
- **BlackHat S3 Port:** [PinkSeaCow-Team](https://github.com/PinkSeaCow-Team)
- **API & Web Interface:** [Unknown Engine](https://github.com/UnknownEngineOfficial)

---

[![License: MIT](https://img.shields.io/github/license/mashape/apistatus.svg)](https://github.com/justcallmekoko/ESP32Marauder/blob/master/LICENSE)
