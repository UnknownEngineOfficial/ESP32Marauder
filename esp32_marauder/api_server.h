#pragma once

#ifndef api_server_h
#define api_server_h

#include "configs.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>

// Default hotspot credentials (user-configurable)
#ifndef API_HOTSPOT_SSID
  #define API_HOTSPOT_SSID "CHANGE_ME_HOTSPOT_SSID"
#endif
#ifndef API_HOTSPOT_PASSWORD
  #define API_HOTSPOT_PASSWORD "CHANGE_ME_HOTSPOT_PASSWORD"
#endif

#define API_MDNS_NAME "marauder"
#define API_PORT 80

class ApiServer {
  private:
    AsyncWebServer* server;
    bool _running = false;
    bool _wifi_connected = false;
    String _device_ip;

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
};

#endif
