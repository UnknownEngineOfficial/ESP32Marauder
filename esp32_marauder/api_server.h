#pragma once

#ifndef api_server_h
#define api_server_h

#include "configs.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include "operation_manager.h"

// Hotspot (STA join) credentials — NO real defaults in source.
// Real values are injected at build time via compiler flags (see secrets.env).
// These placeholders are intentionally non-functional so a build without the
// flags fails loudly instead of silently shipping real credentials.
#ifndef API_HOTSPOT_SSID
  #define API_HOTSPOT_SSID "CHANGE_ME_HOTSPOT_SSID"
#endif
#ifndef API_HOTSPOT_PASSWORD
  #define API_HOTSPOT_PASSWORD "CHANGE_ME_HOTSPOT_PASSWORD"
#endif

// Management AP (the ESP's own AP) credentials — also flag-injected.
#ifndef API_MGMT_AP_SSID
  #define API_MGMT_AP_SSID "CHANGE_ME_MGMT_AP_SSID"
#endif
#ifndef API_MGMT_AP_PASSWORD
  #define API_MGMT_AP_PASSWORD "CHANGE_ME_MGMT_AP_PASSWORD"
#endif

#define API_MDNS_NAME "marauder"
#define API_PORT 80

class ApiServer {
  private:
    AsyncWebServer* server;
    WiFiServer* _diagServer = nullptr;   // raw synchronous server on port 8080 (diagnostic)
    int _bsdListenFd = -1;               // raw lwIP/BSD socket server on port 8081 (diagnostic)
    bool _running = false;
    bool _wifi_connected = false;
    String _device_ip;

    // Observability counters (accept-hit instrumentation) — read-only diag,
    // bumped on every accepted connection so we can see which listener
    // actually receives traffic when the AP is "up but dead".
    uint32_t _http_accepted = 0;     // AsyncWebServer :80 (via onRequest hook)
    uint32_t _raw8080_accepted = 0;  // WiFiServer :8080
    uint32_t _bsd8081_accepted = 0;  // raw lwIP/BSD :8081

    // Health / recovery state
    unsigned long _lastHealthMs = 0;
    uint32_t _recovery_count = 0;
    uint32_t _recovery_fails = 0;
    String _last_recovery_reason;
    void runHealthCheck();
    bool restoreManagementWiFi();
    bool restartMDNS();
    void healthLog(const char* label, bool ok);

    // Operation manager + deferred-start queue (start AFTER the HTTP response
    // has flushed, so the browser reliably receives the acknowledgement before
    // a disruptive operation reconfigures the radio).
    OperationManager _op;
    bool _op_pending = false;
    uint8_t _op_pending_mode = WIFI_SCAN_OFF;
    uint32_t _op_pending_duration = 0;
    uint32_t _op_last_expired_ms = 0;  // debounce for the timer/recovery path
    void applyPendingOperation();
    void executeOperation(uint8_t mode);
    void tickOperation(uint32_t now_ms);
    bool stopOperation();
    void buildOperationJson(JsonDocument& doc);
    static uint8_t modeFromType(const String& type);

    // Helpers
    void sendJson(AsyncWebServerRequest *request, JsonDocument& doc, int code = 200);
    void sendError(AsyncWebServerRequest *request, const char* msg, int code = 400);
    bool hasArg(AsyncWebServerRequest *request, const char* key);
    String getArg(AsyncWebServerRequest *request, const char* key, String defaultVal = "");
    int getArgInt(AsyncWebServerRequest *request, const char* key, int defaultVal = 0);

    // Web interface static files (from PROGMEM)
    void handleRoot(AsyncWebServerRequest *request);
    void handlePing(AsyncWebServerRequest *request);
    void handleSW(AsyncWebServerRequest *request);
    void handleManifest(AsyncWebServerRequest *request);

    // API handlers
    void handleStatus(AsyncWebServerRequest *request);
    void handleOperationStart(AsyncWebServerRequest *request);
    void handleOperationStop(AsyncWebServerRequest *request);
    void handleOperations(AsyncWebServerRequest *request);
    void handleSettings(AsyncWebServerRequest *request);
    void handleScanStart(AsyncWebServerRequest *request);
    void handleScanStop(AsyncWebServerRequest *request);
    void handleChannel(AsyncWebServerRequest *request);
    void handleClearList(AsyncWebServerRequest *request);
    void handleAttack(AsyncWebServerRequest *request);
    void handleAttackStop(AsyncWebServerRequest *request);
    void handleWardrive(AsyncWebServerRequest *request);
    void handleFoxhunt(AsyncWebServerRequest *request);
    void handleEvilPortal(AsyncWebServerRequest *request);
    void handleKarma(AsyncWebServerRequest *request);
    void handlePingScan(AsyncWebServerRequest *request);
    void handlePortScan(AsyncWebServerRequest *request);
    void handleMacTrack(AsyncWebServerRequest *request);
    void handleAPInfo(AsyncWebServerRequest *request);
    void handleAPSelect(AsyncWebServerRequest *request);
    void handleAPAdd(AsyncWebServerRequest *request);
    void handleSSIDCmd(AsyncWebServerRequest *request);
    void handleSaveList(AsyncWebServerRequest *request);
    void handleLoadList(AsyncWebServerRequest *request);
    void handleJoinWiFi(AsyncWebServerRequest *request);
    void handleMacSpoof(AsyncWebServerRequest *request);
    void handleBluetooth(AsyncWebServerRequest *request);
    void handleFindMy(AsyncWebServerRequest *request);
    void handleUpload(AsyncWebServerRequest *request);
    void handleGPS(AsyncWebServerRequest *request);
    void handleGPSTracker(AsyncWebServerRequest *request);
    void handleLed(AsyncWebServerRequest *request);
    void handleReboot(AsyncWebServerRequest *request);
    void handleFileList(AsyncWebServerRequest *request);
    void handleFileUpload(AsyncWebServerRequest *request);
    void handleFileDelete(AsyncWebServerRequest *request);

    // Data endpoints
    void handleDataAP(AsyncWebServerRequest *request);
    void handleDataStation(AsyncWebServerRequest *request);
    void handleDataBLE(AsyncWebServerRequest *request);
    void handleDataSSID(AsyncWebServerRequest *request);
    void handleDataRawStats(AsyncWebServerRequest *request);

    // CORS helper
    void addCorsHeaders(AsyncWebServerResponse *response);

  public:
    ApiServer();
    void begin(const char* ssid, const char* password);
    void handleClient();
    void end();
    bool isRunning() { return _running; }
    bool isWiFiConnected() { return _wifi_connected; }
    String getDeviceIP() { return _device_ip; }
    uint32_t getRecoveryCount() { return _recovery_count; }
    String getLastRecoveryReason() { return _last_recovery_reason; }
};

#endif
