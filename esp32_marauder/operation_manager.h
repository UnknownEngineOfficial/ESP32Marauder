#pragma once
#ifndef operation_manager_h
#define operation_manager_h

#include <Arduino.h>
#include "WiFiScan.h"

// ============================================================================
// OperationManager
// ----------------------------------------------------------------------------
// Tracks the *semantics* of "what is the device currently doing, and how can
// it be stopped again", decoupled from the raw `wifi_scan_obj.currentScanMode`
// integer. The web UI should never have to hardcode which attack kills the
// management link — the ESP reports that via this manager.
//
// Two orthogonal concerns are modelled separately (per Julius' spec):
//   * `impact`        — does the operation tear down the management link?
//   * `remote_stop`   — can it actually be stopped via HTTP while running?
// A synchronous/blocking routine could be MGMT_SAFE but remote_stop=false.
// ============================================================================

enum ManagementImpact {
  MGMT_SAFE = 0,       // web control stays reachable
  MGMT_DEGRADED = 1,   // may briefly drop (currently unused — verify on HW first)
  MGMT_DISCONNECT = 2  // web control definitively lost during operation
};

class OperationManager {
public:
  struct Meta {
    uint8_t mode;
    const char* name;
    ManagementImpact impact;
    bool remote_stop;        // stoppable via HTTP while running
    bool timer_recommended;  // UI should offer an auto-stop timer
  };

  // ---- state (ESP-owned) ----
  bool running = false;
  uint8_t mode = WIFI_SCAN_OFF;
  uint32_t started_ms = 0;
  uint32_t timeout_ms = 0;   // 0 => no timer (manual reset required)
  uint32_t deadline_ms = 0;  // absolute millis() deadline when timer set
  bool management_available = true;

  // ---- classification ----
  static Meta classify(uint8_t scan_mode);

  // ---- lifecycle ----
  void start(uint8_t scan_mode, uint32_t duration_ms) {
    mode = scan_mode;
    started_ms = millis();
    timeout_ms = duration_ms;
    deadline_ms = (duration_ms > 0) ? (started_ms + duration_ms) : 0;
    running = true;
    management_available = (classify(scan_mode).impact != MGMT_DISCONNECT);
  }

  void stop() {
    running = false;
    mode = WIFI_SCAN_OFF;
    started_ms = 0;
    timeout_ms = 0;
    deadline_ms = 0;
    management_available = true;
  }

  // ---- timer helpers (all overflow-safe via signed delta) ----
  bool hasTimer() const { return running && timeout_ms > 0; }

  bool expired(uint32_t now_ms) const {
    if (!hasTimer()) return false;
    return (int32_t)(now_ms - deadline_ms) >= 0;
  }

  uint32_t remaining_ms(uint32_t now_ms) const {
    if (!hasTimer()) return 0;
    if ((int32_t)(now_ms - deadline_ms) >= 0) return 0;
    return deadline_ms - now_ms;
  }

  uint32_t runtime_ms(uint32_t now_ms) const {
    if (!running) return 0;
    return now_ms - started_ms;
  }

  const Meta& meta() const { return classify(mode); }

  // Human-readable "how can this operation be ended".
  const char* stopMethod() const {
    bool remote = classify(mode).remote_stop;
    bool timer = hasTimer();
    if (remote && timer) return "remote_or_timer";
    if (remote) return "remote";
    if (timer) return "timer_or_reset";
    return "manual_reset";
  }
};

// ============================================================================
// Classification table. Explicit list; anything NOT listed (except OFF)
// falls through to the safe default: MGMT_DISCONNECT / no remote stop.
//
// Rule (Julius, 2026-08-16):
//   * all pure BT_* operations  -> MGMT_SAFE (WiFi stack untouched)
//   * all WIFI_* scans/attacks  -> MGMT_DISCONNECT (leave WIFI_AP_STA)
//   * Evil Portal               -> MGMT_DISCONNECT (own AP ≠ mgmt AP preserved)
//   * nothing DEGRADED yet      -> verify a concrete op on hardware first
// ============================================================================

inline const char* impactToString(ManagementImpact imp) {
  switch (imp) {
    case MGMT_SAFE:       return "safe";
    case MGMT_DEGRADED:   return "degraded";
    case MGMT_DISCONNECT: return "disconnect";
  }
  return "unknown";
}

#define OM_ROW(m, n, imp, rs, tr) { (uint8_t)(m), (n), (imp), (rs), (tr) }

static const OperationManager::Meta OPERATION_META[] = {
  // --- idle / neutral ---
  OM_ROW(WIFI_SCAN_OFF,            "Idle",              MGMT_SAFE,       true,  false),

  // --- BT (SAFE: WiFi stack untouched; stoppable via HTTP) ---
  OM_ROW(BT_SCAN_ALL,              "BLE Scan",          MGMT_SAFE,       true,  false),
  OM_ROW(BT_SCAN_SKIMMERS,         "Skimmer Sniff",     MGMT_SAFE,       true,  false),
  OM_ROW(BT_SCAN_WAR_DRIVE,        "BT Wardrive",       MGMT_SAFE,       true,  false),
  OM_ROW(BT_SCAN_WAR_DRIVE_CONT,   "BT Wardrive Cont",  MGMT_SAFE,       true,  false),
  OM_ROW(BT_ATTACK_SOUR_APPLE,     "SourApple",         MGMT_SAFE,       true,  false),
  OM_ROW(BT_ATTACK_SWIFTPAIR_SPAM, "SwiftPair Spam",    MGMT_SAFE,       true,  false),
  OM_ROW(BT_ATTACK_SPAM_ALL,       "BLE Spam All",      MGMT_SAFE,       true,  false),
  OM_ROW(BT_ATTACK_SAMSUNG_SPAM,   "Samsung Spam",      MGMT_SAFE,       true,  false),
  OM_ROW(BT_ATTACK_GOOGLE_SPAM,    "Google Spam",       MGMT_SAFE,       true,  false),
  OM_ROW(BT_ATTACK_FLIPPER_SPAM,   "Flipper Spam",      MGMT_SAFE,       true,  false),
  OM_ROW(BT_SCAN_AIRTAG,           "Airtag Sniff",      MGMT_SAFE,       true,  false),
  OM_ROW(BT_SPOOF_AIRTAG,          "Airtag Spoof",      MGMT_SAFE,       true,  false),
  OM_ROW(BT_SCAN_FLIPPER,          "Flipper Sniff",     MGMT_SAFE,       true,  false),
  OM_ROW(BT_SCAN_ANALYZER,         "BT Analyzer",       MGMT_SAFE,       true,  false),
  OM_ROW(BT_SCAN_AIRTAG_MON,       "Airtag Monitor",    MGMT_SAFE,       true,  false),
  OM_ROW(BT_SCAN_FLOCK,            "Flock Sniff",       MGMT_SAFE,       true,  false),
  OM_ROW(BT_SCAN_SIMPLE,           "Simple Sniff",      MGMT_SAFE,       true,  false),
  OM_ROW(BT_SCAN_SIMPLE_TWO,       "Simple Sniff 2",    MGMT_SAFE,       true,  false),
  OM_ROW(BT_SCAN_FLOCK_WARDRIVE,   "Flock Wardrive",    MGMT_SAFE,       true,  false),
  OM_ROW(BT_SCAN_RAYBAN,           "Rayban Sniff",      MGMT_SAFE,       true,  false),
  OM_ROW(BT_ATTACK_APPLE_JUICE,    "AppleJuice",        MGMT_SAFE,       true,  false),
  OM_ROW(BT_SCAN_FOX_HUNT,         "BT Fox Hunt",       MGMT_SAFE,       true,  false),
  OM_ROW(BT_FINDMY_SOUND,          "FindMy Sound",      MGMT_SAFE,       true,  false),
  OM_ROW(BT_ATTACK_FINDMY_LIVE,    "FindMy Live",       MGMT_SAFE,       true,  false),
  OM_ROW(WIFI_SCAN_BLE,            "BLE Scan",          MGMT_SAFE,       true,  false),

  // --- WiFi (DISCONNECT: leave WIFI_AP_STA; not stoppable remotely) ---
  OM_ROW(WIFI_SCAN_PROBE,          "Probe Sniff",       MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_AP,             "AP Scan",           MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_PWN,            "PWN Scan",          MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_EAPOL,          "EAPOL Sniff",       MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_DEAUTH,         "Deauth Sniff",      MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_ALL,            "Full WiFi Scan",    MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_PACKET_MONITOR,      "Packet Monitor",    MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_BEACON_SPAM,  "Beacon Spam",       MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_RICK_ROLL,    "Rickroll",          MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_ESPRESSIF,      "ESP Sniff",         MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_BEACON_LIST,  "Beacon List",       MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_TARGET_AP,      "Target AP",         MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_AUTH,         "Auth Attack",       MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_MIMIC,        "Mimic Attack",      MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_DEAUTH,       "Deauth Attack",     MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_AP_SPAM,      "AP Spam",           MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_TARGET_AP_FULL, "Target AP Full",    MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_ACTIVE_EAPOL,   "Active EAPOL",      MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_DEAUTH_MANUAL,"Deauth Manual",     MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_RAW_CAPTURE,    "Raw Capture",       MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_STATION,        "Station Scan",      MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_DEAUTH_TARGETED,"Deauth Targeted", MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_ACTIVE_LIST_EAPOL,"Active List EAPOL",MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_SIG_STREN,      "Signal Strength",   MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_EVIL_PORTAL,    "Evil Portal",       MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_WAR_DRIVE,      "Wardrive",          MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_STATION_WAR_DRIVE,"Station Wardrive",MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_CHAN_ANALYZER,  "Channel Analyzer",  MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_PACKET_RATE,    "Packet Rate",       MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_AP_STA,         "AP+STA Scan",       MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_PINESCAN,       "PineScan",          MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_MULTISSID,      "MultiSSID",         MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_PING_SCAN,           "Ping Scan",         MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_PORT_SCAN_ALL,       "Port Scan",         MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_BAD_MSG,      "BadMSG",            MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_BAD_MSG_TARGETED,"BadMSG Targeted",MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_TELNET,         "Telnet Scan",       MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_SSH,            "SSH Scan",          MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ARP_SCAN,            "ARP Scan",          MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_SLEEP,        "Sleep Attack",      MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_SLEEP_TARGETED,"Sleep Targeted",   MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_DNS,            "DNS Scan",          MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_HTTP,           "HTTP Scan",         MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_HTTPS,          "HTTPS Scan",        MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_SMTP,           "SMTP Scan",         MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_RDP,            "RDP Scan",          MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_HOSTSPOT,            "Hostspot",          MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_CHAN_ACT,       "Channel Activity",  MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_DETECT_FOLLOW,  "Detect Follow",     MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_SCAN_SAE_COMMIT,     "SAE Commit",        MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_SAE_COMMIT,   "SAE Attack",        MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_CSA,          "CSA Attack",        MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_QUIET,        "Quiet Attack",      MGMT_DISCONNECT, false, true),
  OM_ROW(WIFI_ATTACK_FUNNY_BEACON, "Funny Beacon",      MGMT_DISCONNECT, false, true),
};

#undef OM_ROW

inline OperationManager::Meta OperationManager::classify(uint8_t scan_mode) {
  const size_t N = sizeof(OPERATION_META) / sizeof(OPERATION_META[0]);
  for (size_t i = 0; i < N; i++) {
    if (OPERATION_META[i].mode == scan_mode) return OPERATION_META[i];
  }
  // Safe default: unknown mode => assume it disrupts management and cannot be
  // stopped remotely. UI shows the raw integer as the name.
  static Meta unknown = { 0, "Unknown", MGMT_DISCONNECT, false, true };
  unknown.mode = scan_mode;
  return unknown;
}

#endif
