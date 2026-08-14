#include "api_server.h"
#include "WiFiScan.h"
#include "EvilPortal.h"
#include "settings.h"
#include "CommandLine.h"
#include "configs.h"
#include "web_interface_html.h"
#include "web_interface_sw.h"
#include "web_interface_manifest.h"

extern WiFiScan wifi_scan_obj;
extern EvilPortal evil_portal_obj;
extern Settings settings_obj;
extern CommandLine cli_obj;

extern LinkedList<AccessPoint>* access_points;
extern LinkedList<Station>* stations;
extern LinkedList<BleDevice>* ble_devices;
extern LinkedList<ssid>* ssids;
extern LinkedList<AirTag>* airtags;
extern LinkedList<IPAddress>* ipList;
extern LinkedList<ProbeReqSsid>* probe_req_ssids;

#ifdef HAS_GPS
  #include "GpsInterface.h"
  extern GpsInterface gps_obj;
#endif

#ifdef HAS_SD
  #include "SDInterface.h"
  extern SDInterface sd_obj;
#endif

#if defined(HAS_NEOPIXEL_LED) || defined(HAS_BLACKHAT_LED)
  #ifdef HAS_BLACKHAT_LED
    #include "BlackHatLED.h"
    extern BlackHatLED bh_led;
  #else
    #include "LedInterface.h"
    extern LedInterface led_obj;
  #endif
#endif

extern const String PROGMEM version_number;
extern const String PROGMEM board_target;

// ---------- Helpers ----------

void ApiServer::addCorsHeaders(AsyncWebServerResponse *response) {
  response->addHeader("Access-Control-Allow-Origin", "*");
  response->addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
  response->addHeader("Access-Control-Allow-Headers", "Content-Type");
}

void ApiServer::sendJson(AsyncWebServerRequest *request, JsonDocument& doc, int code) {
  String out;
  serializeJson(doc, out);
  AsyncWebServerResponse *response = request->beginResponse(code, "application/json", out);
  addCorsHeaders(response);
  request->send(response);
}

void ApiServer::sendError(AsyncWebServerRequest *request, const char* msg, int code) {
  DynamicJsonDocument doc(256);
  doc["error"] = true;
  doc["message"] = msg;
  sendJson(request, doc, code);
}

bool ApiServer::hasArg(AsyncWebServerRequest *request, const char* key) {
  return request->hasParam(key);
}

String ApiServer::getArg(AsyncWebServerRequest *request, const char* key, String defaultVal) {
  if (request->hasParam(key)) return request->getParam(key)->value();
  return defaultVal;
}

int ApiServer::getArgInt(AsyncWebServerRequest *request, const char* key, int defaultVal) {
  if (request->hasParam(key)) return request->getParam(key)->value().toInt();
  return defaultVal;
}

// ---------- Constructor ----------

ApiServer::ApiServer() {
  server = new AsyncWebServer(API_PORT);
}

// ---------- WiFi Connect + mDNS ----------

void ApiServer::begin(const char* ssid, const char* password) {
  // Management network: WIFI_AP_STA — AP + STA run in parallel.
  // The management AP "CHANGE_ME_MGMT_AP_SSID" is ALWAYS up (192.168.4.1),
  // so the device stays reachable regardless of STA join success. The STA
  // join to the external hotspot runs in parallel and never tears down the AP.
  const char* mgmtSSID = "CHANGE_ME_MGMT_AP_SSID";
  const char* mgmtPW   = "CHANGE_ME_HOTSPOT_PASSWORD";

  Serial.println("[API] Bringing up management AP + STA (WIFI_AP_STA)...");

  WiFi.mode(WIFI_AP_STA);

  // ---- WiFi event logging (diagnostic) ----
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
    switch (event) {
      case ARDUINO_EVENT_WIFI_AP_START:           Serial.println("[EV] AP_START"); break;
      case ARDUINO_EVENT_WIFI_AP_STACONNECTED:    Serial.printf("[EV] AP_STACONNECTED (aid=%u)\n", info.wifi_ap_staconnected.aid); break;
      case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:   Serial.println("[EV] AP_STAIPASSIGNED"); break;
      case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: Serial.println("[EV] AP_STADISCONNECTED"); break;
      case ARDUINO_EVENT_WIFI_STA_START:          Serial.println("[EV] STA_START"); break;
      case ARDUINO_EVENT_WIFI_STA_CONNECTED:      Serial.println("[EV] STA_CONNECTED"); break;
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:         Serial.println("[EV] STA_GOT_IP"); break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:   Serial.println("[EV] STA_DISCONNECTED"); break;
      default: break;
    }
  });
  Serial.println("[API] WiFi event handlers registered");

  bool apOk = WiFi.softAP(mgmtSSID, mgmtPW);
  String apIP = WiFi.softAPIP().toString();
  Serial.printf("[API] softAP(%s) -> %s · AP IP: %s\n",
                mgmtSSID, apOk ? "OK" : "FAILED", apIP.c_str());

  // ---- netif/stack diagnostic ----
  {
    wifi_mode_t m;
    esp_wifi_get_mode(&m);
    Serial.printf("[DIAG] WiFi.getMode()=%d · esp_wifi_get_mode()=%d · AP IP=%s · AP MAC=%s · stations=%d\n",
                  (int)WiFi.getMode(), (int)m,
                  WiFi.softAPIP().toString().c_str(),
                  WiFi.softAPmacAddress().c_str(),
                  (int)WiFi.softAPgetStationNum());
    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_t* sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    Serial.printf("[DIAG] netif AP=%s · netif STA=%s\n",
                  ap_netif ? "PRESENT" : "MISSING",
                  sta_netif ? "PRESENT" : "MISSING");
    if (ap_netif) {
      esp_netif_dhcp_status_t dhcp = ESP_NETIF_DHCP_INIT;
      esp_err_t derr = esp_netif_dhcps_get_status(ap_netif, &dhcp);
      Serial.printf("[DIAG] AP DHCP server: err=%d status=%s\n",
                    (int)derr,
                    dhcp == ESP_NETIF_DHCP_STARTED ? "STARTED" :
                    dhcp == ESP_NETIF_DHCP_STOPPED ? "STOPPED" :
                    dhcp == ESP_NETIF_DHCP_INIT   ? "INIT" : "UNKNOWN");
      // Also check netif flags
      bool up = esp_netif_is_netif_up(ap_netif);
      Serial.printf("[DIAG] AP netif up=%s\n", up ? "YES" : "NO");
    }
  }

  _device_ip = apIP;  // management AP is the always-reachable address

  // Start STA join in the background — WiFi.begin() is asynchronous. We do NOT
  // block on it here (no 15s wait). The HTTP server starts immediately.
  _wifi_connected = false;
#ifdef API_AP_ONLY_DIAG
  Serial.println("[API] AP-ONLY DIAG BUILD — skipping STA begin (no WiFi.begin)");
  Serial.printf("[API] mode=%d (AP only, STA untouched)\n", (int)WiFi.getMode());
#else
  WiFi.begin(ssid, password);
  Serial.printf("[API] STA begin(%s) · mode=%d (async, non-blocking)\n",
                ssid, (int)WiFi.getMode());
#endif

  if (MDNS.begin(API_MDNS_NAME)) {
    Serial.println("[API] mDNS started: " + String(API_MDNS_NAME) + ".local");
    MDNS.addService("http", "tcp", API_PORT);
  }

  // ---- CORS preflight ----
  server->onNotFound([](AsyncWebServerRequest *request) {
    if (request->method() == HTTP_OPTIONS) {
      AsyncWebServerResponse *response = request->beginResponse(204);
      response->addHeader("Access-Control-Allow-Origin", "*");
      response->addHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
      response->addHeader("Access-Control-Allow-Headers", "Content-Type");
      request->send(response);
    } else {
      request->send(404, "application/json", "{\"error\":true,\"message\":\"not found\"}");
    }
  });

  // ========== ROOT (Web Interface) ==========
  server->on("/", HTTP_GET, [this](AsyncWebServerRequest *r){ handleRoot(r); });
  server->on("/ping", HTTP_GET, [this](AsyncWebServerRequest *r){ handlePing(r); });
  server->on("/sw.js", HTTP_GET, [this](AsyncWebServerRequest *r){ handleSW(r); });
  server->on("/manifest.json", HTTP_GET, [this](AsyncWebServerRequest *r){ handleManifest(r); });
  
  // ========== CORE ==========
  server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *r){ handleStatus(r); });

  // ========== SETTINGS ==========
  server->on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest *r){ handleSettings(r); });

  // ========== SCAN ==========
  // start scan, stop scan
  server->on("/api/scan/start", HTTP_POST, [this](AsyncWebServerRequest *r){ handleScanStart(r); });
  server->on("/api/scan/stop", HTTP_POST, [this](AsyncWebServerRequest *r){ handleScanStop(r); });
  server->on("/api/channel", HTTP_POST, [this](AsyncWebServerRequest *r){ handleChannel(r); });
  server->on("/api/clearlist", HTTP_POST, [this](AsyncWebServerRequest *r){ handleClearList(r); });

  // ========== ATTACK ==========
  server->on("/api/attack/start", HTTP_POST, [this](AsyncWebServerRequest *r){ handleAttack(r); });
  server->on("/api/attack/stop", HTTP_POST, [this](AsyncWebServerRequest *r){ handleAttackStop(r); });
  server->on("/api/wardrive", HTTP_POST, [this](AsyncWebServerRequest *r){ handleWardrive(r); });
  server->on("/api/foxhunt", HTTP_POST, [this](AsyncWebServerRequest *r){ handleFoxhunt(r); });
  server->on("/api/evilportal", HTTP_POST, [this](AsyncWebServerRequest *r){ handleEvilPortal(r); });
  server->on("/api/karma", HTTP_POST, [this](AsyncWebServerRequest *r){ handleKarma(r); });

  // ========== NETWORK SCAN ==========
  server->on("/api/pingscan", HTTP_POST, [this](AsyncWebServerRequest *r){ handlePingScan(r); });
  server->on("/api/portscan", HTTP_POST, [this](AsyncWebServerRequest *r){ handlePortScan(r); });
  server->on("/api/mactrack", HTTP_POST, [this](AsyncWebServerRequest *r){ handleMacTrack(r); });

  // ========== AP MANAGEMENT ==========
  server->on("/api/ap/info", HTTP_GET, [this](AsyncWebServerRequest *r){ handleAPInfo(r); });
  server->on("/api/ap/select", HTTP_POST, [this](AsyncWebServerRequest *r){ handleAPSelect(r); });
  server->on("/api/ap/add", HTTP_POST, [this](AsyncWebServerRequest *r){ handleAPAdd(r); });
  server->on("/api/ssid", HTTP_POST, [this](AsyncWebServerRequest *r){ handleSSIDCmd(r); });

  // ========== PERSISTENCE ==========
  server->on("/api/save", HTTP_POST, [this](AsyncWebServerRequest *r){ handleSaveList(r); });
  server->on("/api/load", HTTP_POST, [this](AsyncWebServerRequest *r){ handleLoadList(r); });

  // ========== WIFI JOIN ==========
  server->on("/api/join", HTTP_POST, [this](AsyncWebServerRequest *r){ handleJoinWiFi(r); });

  // ========== MAC SPOOFING ==========
  server->on("/api/mac", HTTP_POST, [this](AsyncWebServerRequest *r){ handleMacSpoof(r); });

  // ========== BLUETOOTH ==========
  server->on("/api/bt/scan", HTTP_POST, [this](AsyncWebServerRequest *r){ handleBluetooth(r); });
  server->on("/api/bt/findmy", HTTP_POST, [this](AsyncWebServerRequest *r){ handleFindMy(r); });
  server->on("/api/bt/spam", HTTP_POST, [this](AsyncWebServerRequest *r){ handleBluetooth(r); });

  // ========== UPLOAD ==========
  server->on("/api/upload", HTTP_POST, [this](AsyncWebServerRequest *r){ handleUpload(r); });

  // ========== GPS ==========
  server->on("/api/gps", HTTP_GET, [this](AsyncWebServerRequest *r){ handleGPS(r); });
  server->on("/api/gps/tracker", HTTP_POST, [this](AsyncWebServerRequest *r){ handleGPSTracker(r); });

  // ========== LED ==========
  server->on("/api/led", HTTP_POST, [this](AsyncWebServerRequest *r){ handleLed(r); });

  // ========== SYSTEM ==========
  server->on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest *r){ handleReboot(r); });

  // ========== FILES (SD) ==========
  server->on("/api/files", HTTP_GET, [this](AsyncWebServerRequest *r){ handleFileList(r); });
  server->on("/api/files/upload", HTTP_POST, [this](AsyncWebServerRequest *r){ handleFileUpload(r); });
  server->on("/api/files/delete", HTTP_DELETE, [this](AsyncWebServerRequest *r){ handleFileDelete(r); });

  // ========== DATA ENDPOINTS (polling) ==========
  server->on("/api/data/ap", HTTP_GET, [this](AsyncWebServerRequest *r){ handleDataAP(r); });
  server->on("/api/data/station", HTTP_GET, [this](AsyncWebServerRequest *r){ handleDataStation(r); });
  server->on("/api/data/ble", HTTP_GET, [this](AsyncWebServerRequest *r){ handleDataBLE(r); });
  server->on("/api/data/ssid", HTTP_GET, [this](AsyncWebServerRequest *r){ handleDataSSID(r); });
  server->on("/api/data/rawstats", HTTP_GET, [this](AsyncWebServerRequest *r){ handleDataRawStats(r); });

  server->begin();
  _running = true;
  Serial.println("[API] HTTP server started on 0.0.0.0:" + String(API_PORT));
  Serial.println("[API] Management URL: http://" + _device_ip + "/");

  // ---- raw diagnostic WiFiServer on 8080 (bypasses AsyncWebServer) ----
  _diagServer = new WiFiServer(8080);
  _diagServer->begin();
  Serial.println("[DIAG] raw WiFiServer listening on 8080");
}

void ApiServer::end() {
  server->end();
  _running = false;
  MDNS.end();
}
// ========== ROOT HANDLER (Web Interface) ==========

void ApiServer::handleRoot(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", WEB_INTERFACE_HTML);
  response->addHeader("Cache-Control", "no-cache");
  request->send(response);
}

void ApiServer::handlePing(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "pong");
  addCorsHeaders(response);
  request->send(response);
}

void ApiServer::handleSW(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse_P(200, "application/javascript", WEB_INTERFACE_SW_JS);
  response->addHeader("Service-Worker-Allowed", "/");
  response->addHeader("Cache-Control", "public, max-age=3600");
  request->send(response);
}

void ApiServer::handleManifest(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse_P(200, "application/json", WEB_INTERFACE_MANIFEST);
  response->addHeader("Cache-Control", "public, max-age=86400");
  request->send(response);
}

// ========== CORE HANDLERS ==========

void ApiServer::handleStatus(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(2048);
  doc["version"] = MARAUDER_VERSION;
  doc["hardware"] = HARDWARE_NAME;
  doc["board"] = board_target;
  doc["wifi_connected"] = _wifi_connected;
  doc["wifi_ap_mode"] = !_wifi_connected;
  doc["debug"] = false;
  doc["scanning"] = wifi_scan_obj.scanning();
  doc["current_scan"] = wifi_scan_obj.currentScanMode;
  doc["channel"] = wifi_scan_obj.set_channel;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["uptime_ms"] = millis();
  doc["ap_count"] = access_points ? access_points->size() : 0;
  doc["station_count"] = stations ? stations->size() : 0;
  doc["ble_count"] = ble_devices ? ble_devices->size() : 0;
  doc["ssid_count"] = ssids ? ssids->size() : 0;

  #ifdef HAS_GPS
    doc["gps_has_fix"] = gps_obj.getFix();
    doc["gps_lat"] = gps_obj.getLat();
    doc["gps_lon"] = gps_obj.getLon();
    doc["gps_alt"] = gps_obj.getAlt();
  #endif

  #ifdef HAS_BATTERY
    doc["battery_pct"] = battery_obj.getBattery();
  #endif

  sendJson(request, doc);
}

// ========== SETTINGS ==========

void ApiServer::handleSettings(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(4096);
  settings_obj.createDefaultSettings(SPIFFS);
  String settingsStr = settings_obj.getSettingsString();
  
  // Parse existing settings JSON back for the response
  DynamicJsonDocument settingsDoc(4096);
  DeserializationError err = deserializeJson(settingsDoc, settingsStr);
  if (err) {
    doc["raw"] = settingsStr;
    doc["parse_error"] = err.c_str();
  } else {
    // List known settings keys with values
    JsonObject keys = doc.createNestedObject("keys");
    int count = 0;
    for (JsonPair kv : settingsDoc.as<JsonObject>()) {
      keys[kv.key().c_str()] = kv.value();
      count++;
    }
    doc["count"] = count;
  }
  sendJson(request, doc);
}

// ========== SCAN ==========

void ApiServer::handleScanStart(AsyncWebServerRequest *request) {
  String type = getArg(request, "type", "ap");
  DynamicJsonDocument doc(256);

  uint8_t mode = WIFI_SCAN_AP;
  if (type == "ap")       mode = WIFI_SCAN_AP;
  else if (type == "sta")  mode = WIFI_SCAN_STATION;
  else if (type == "all")  mode = WIFI_SCAN_ALL;
  else if (type == "probe") mode = WIFI_SCAN_PROBE;
  else if (type == "pwn")  mode = WIFI_SCAN_PWN;
  else if (type == "deauth") mode = WIFI_SCAN_DEAUTH;
  else if (type == "raw")   mode = WIFI_SCAN_RAW_CAPTURE;
  else if (type == "beacon") mode = WIFI_SCAN_TARGET_AP;
  else if (type == "eapol") mode = WIFI_SCAN_EAPOL;
  else if (type == "packet") mode = WIFI_PACKET_MONITOR;
  else if (type == "sae")   mode = WIFI_SCAN_SAE;
  else if (type == "pinescan") mode = WIFI_SCAN_PINESCAN;
  else if (type == "multissid") mode = WIFI_SCAN_MULTISSID;
  else {
    sendError(request, "Unknown scan type", 400);
    return;
  }

  wifi_scan_obj.StartScan(mode);
  doc["ok"] = true;
  doc["scan_type"] = type;
  doc["mode"] = mode;
  sendJson(request, doc);
}

void ApiServer::handleScanStop(AsyncWebServerRequest *request) {
  String type = getArg(request, "type", "");
  DynamicJsonDocument doc(256);
  
  if (type == "ap" || type == "" || type == "all") wifi_scan_obj.StopScan(WIFI_SCAN_AP);
  if (type == "sta" || type == "" || type == "all") wifi_scan_obj.StopScan(WIFI_SCAN_STATION);
  if (type == "deauth" || type == "all") wifi_scan_obj.StopScan(WIFI_SCAN_DEAUTH);
  if (type == "eapol" || type == "all") wifi_scan_obj.StopScan(WIFI_SCAN_EAPOL);
  if (type == "beacon" || type == "all") wifi_scan_obj.StopScan(WIFI_SCAN_TARGET_AP);
  if (type == "raw" || type == "all") wifi_scan_obj.StopScan(WIFI_SCAN_RAW_CAPTURE);
  if (type == "probe" || type == "all") wifi_scan_obj.StopScan(WIFI_SCAN_PROBE);
  if (type == "pwn" || type == "all") wifi_scan_obj.StopScan(WIFI_SCAN_PWN);
  if (type == "sae" || type == "all") wifi_scan_obj.StopScan(WIFI_SCAN_SAE);
  if (type == "packet" || type == "all") wifi_scan_obj.StopScan(WIFI_PACKET_MONITOR);
  if (type == "pinescan" || type == "all") wifi_scan_obj.StopScan(WIFI_SCAN_PINESCAN);
  if (type == "multissid" || type == "all") wifi_scan_obj.StopScan(WIFI_SCAN_MULTISSID);
  
  wifi_scan_obj.StopScan(WIFI_SCAN_OFF);
  // Also stop attacks
  wifi_scan_obj.send_deauth = false;
  #ifdef HAS_BT
    wifi_scan_obj.ble_scanning = false;
  #endif

  doc["ok"] = true;
  doc["message"] = "All scans stopped";
  sendJson(request, doc);
}

void ApiServer::handleChannel(AsyncWebServerRequest *request) {
  int chan = getArgInt(request, "ch", -1);
  if (chan < 1) chan = getArgInt(request, "channel", -1);
  int hop = getArgInt(request, "hop", -1);
  DynamicJsonDocument doc(256);
  if (hop == 1) {
    wifi_scan_obj.channel_hop = true;
    doc["channel_hop"] = true;
  } else if (hop == 0) {
    wifi_scan_obj.channel_hop = false;
    doc["channel_hop"] = false;
  } else if (chan >= 1 && chan <= 177) {
    wifi_scan_obj.set_channel = chan;
    wifi_scan_obj.changeChannel(chan);
    doc["channel"] = chan;
  } else {
    doc["channel"] = wifi_scan_obj.set_channel;
  }
  doc["ok"] = true;
  sendJson(request, doc);
}

void ApiServer::handleClearList(AsyncWebServerRequest *request) {
  String list = getArg(request, "list", "ap");
  DynamicJsonDocument doc(256);
  int mode = 0;
  if (list == "ap") mode = 0;
  else if (list == "station") mode = 1;
  else if (list == "ssid") mode = 2;
  else if (list == "all") mode = 3;
  wifi_scan_obj.clearList(mode);
  doc["ok"] = true;
  doc["cleared"] = list;
  sendJson(request, doc);
}
// ========== ATTACK ==========

void ApiServer::handleAttack(AsyncWebServerRequest *request) {
  String type = getArg(request, "type", "deauth");
  DynamicJsonDocument doc(256);

  // Stop current scans first
  wifi_scan_obj.send_deauth = false;
  
  if (type == "deauth") {
    wifi_scan_obj.StartScan(WIFI_ATTACK_DEAUTH);
    wifi_scan_obj.send_deauth = true;
    doc["attack"] = "deauth";
  }
  else if (type == "deauth_targeted") {
    String target = getArg(request, "target", "");
    if (target.length() > 0) {
      wifi_scan_obj.dst_mac = target;
    }
    wifi_scan_obj.StartScan(WIFI_ATTACK_DEAUTH_TARGETED);
    wifi_scan_obj.send_deauth = true;
    doc["attack"] = "deauth_targeted";
    doc["target"] = wifi_scan_obj.dst_mac;
  }
  else if (type == "beacon") {
    String mode = getArg(request, "mode", "random");
    if (mode == "list") wifi_scan_obj.StartScan(WIFI_ATTACK_BEACON_LIST);
    else if (mode == "rickroll") wifi_scan_obj.StartScan(WIFI_ATTACK_RICK_ROLL);
    else wifi_scan_obj.StartScan(WIFI_ATTACK_BEACON_SPAM);
    doc["attack"] = "beacon";
    doc["mode"] = mode;
  }
  else if (type == "probe") {
    wifi_scan_obj.StartScan(WIFI_ATTACK_AP_SPAM);
    doc["attack"] = "probe";
  }
  else if (type == "badmsg") {
    wifi_scan_obj.StartScan(WIFI_ATTACK_BAD_MSG);
    doc["attack"] = "badmsg";
  }
  else if (type == "sleep") {
    wifi_scan_obj.StartScan(WIFI_ATTACK_SLEEP);
    doc["attack"] = "sleep";
  }
  else if (type == "sae") {
    wifi_scan_obj.StartScan(WIFI_SCAN_SAE);
    doc["attack"] = "sae";
  }
  else if (type == "csa") {
    wifi_scan_obj.StartScan(WIFI_ATTACK_MIMIC);
    doc["attack"] = "csa";
  }
  else if (type == "quiet") {
    wifi_scan_obj.StartScan(WIFI_ATTACK_SLEEP);
    doc["attack"] = "quiet";
  }
  else {
    sendError(request, "Unknown attack type", 400);
    return;
  }

  doc["ok"] = true;
  sendJson(request, doc);
}

void ApiServer::handleAttackStop(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(256);
  wifi_scan_obj.send_deauth = false;
  wifi_scan_obj.StopScan(WIFI_ATTACK_DEAUTH);
  wifi_scan_obj.StopScan(WIFI_ATTACK_DEAUTH_TARGETED);
  wifi_scan_obj.StopScan(WIFI_ATTACK_BEACON_SPAM);
  wifi_scan_obj.StopScan(WIFI_ATTACK_BEACON_LIST);
  wifi_scan_obj.StopScan(WIFI_ATTACK_RICK_ROLL);
  wifi_scan_obj.StopScan(WIFI_ATTACK_AP_SPAM);
  wifi_scan_obj.StopScan(WIFI_ATTACK_BAD_MSG);
  wifi_scan_obj.StopScan(WIFI_ATTACK_SLEEP);
  wifi_scan_obj.StopScan(WIFI_ATTACK_MIMIC);
  doc["ok"] = true;
  doc["message"] = "All attacks stopped";
  sendJson(request, doc);
}

void ApiServer::handleWardrive(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(256);
  String action = getArg(request, "action", "start");
  if (action == "start") {
    wifi_scan_obj.StartScan(WIFI_SCAN_WAR_DRIVE);
    doc["wardrive"] = "started";
  } else {
    wifi_scan_obj.StopScan(WIFI_SCAN_WAR_DRIVE);
    doc["wardrive"] = "stopped";
  }
  doc["ok"] = true;
  sendJson(request, doc);
}

void ApiServer::handleFoxhunt(AsyncWebServerRequest *request) {
  String band = getArg(request, "band", "wifi");
  DynamicJsonDocument doc(256);
  wifi_scan_obj.StartScan(WIFI_SCAN_SIG_STREN);
  doc["ok"] = true;
  doc["band"] = band;
  doc["foxhunt"] = "started";
  sendJson(request, doc);
}

void ApiServer::handleEvilPortal(AsyncWebServerRequest *request) {
  String action = getArg(request, "action", "start");
  DynamicJsonDocument doc(512);
  if (action == "start") {
    String htmlFile = getArg(request, "html", "index.html");
    // Set AP name if provided
    if (request->hasParam("apname")) {
      strncpy(apName, getArg(request, "apname").c_str(), MAX_AP_NAME_SIZE);
    }
    wifi_scan_obj.StartScan(WIFI_SCAN_EVIL_PORTAL);
    doc["evilportal"] = "started";
    doc["ap_name"] = apName;
  } else if (action == "stop") {
    evil_portal_obj.cleanup();
    doc["evilportal"] = "stopped";
  } else if (action == "ack") {
    doc["evilportal"] = "ack_generated";
  } else if (action == "reset") {
    evil_portal_obj.cleanup();
    doc["evilportal"] = "creds_reset";
  } else if (action == "sethtml") {
    String html = getArg(request, "html", "");
    if (html.length() > 0) {
      #ifdef HAS_SD
        evil_portal_obj.target_html_name = html;
        evil_portal_obj.setHtml();
      #endif
      doc["html"] = html;
    }
    doc["evilportal"] = "html_set";
  } else if (action == "status") {
    doc["running"] = (wifi_scan_obj.currentScanMode == WIFI_SCAN_EVIL_PORTAL);
    doc["ap_name"] = apName;
    doc["victim_count"] = (evil_portal_obj.get_user_name().length() > 0 ? 1 : 0);
  }
  doc["ok"] = true;
  sendJson(request, doc);
}

void ApiServer::handleKarma(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(256);
  String action = getArg(request, "action", "start");
  if (action == "start") {
    wifi_scan_obj.StartScan(WIFI_ATTACK_MIMIC); // closest proxy
    doc["karma"] = "started";
  } else {
    wifi_scan_obj.StopScan(WIFI_ATTACK_MIMIC);
    doc["karma"] = "stopped";
  }
  doc["ok"] = true;
  sendJson(request, doc);
}
// ========== NETWORK SCAN ==========

void ApiServer::handlePingScan(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(256);
  wifi_scan_obj.StartScan(WIFI_SCAN_ACTIVE_EAPOL); // ping scan mode
  doc["ok"] = true;
  doc["pingscan"] = "started";
  sendJson(request, doc);
}

void ApiServer::handlePortScan(AsyncWebServerRequest *request) {
  String svc = getArg(request, "service", "all");
  DynamicJsonDocument doc(256);
  if (svc == "ssh") wifi_scan_obj.StartScan(WIFI_SCAN_SSH);
  else if (svc == "telnet") wifi_scan_obj.StartScan(WIFI_SCAN_TELNET);
  else wifi_scan_obj.StartScan(WIFI_SCAN_ACTIVE_LIST_EAPOL);
  doc["ok"] = true;
  doc["service"] = svc;
  doc["portscan"] = "started";
  sendJson(request, doc);
}

void ApiServer::handleMacTrack(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(256);
  wifi_scan_obj.StartScan(WIFI_SCAN_STATION);
  doc["ok"] = true;
  doc["mactrack"] = "started";
  sendJson(request, doc);
}

// ========== AP MANAGEMENT ==========

void ApiServer::handleAPInfo(AsyncWebServerRequest *request) {
  int index = getArgInt(request, "index", -1);
  DynamicJsonDocument doc(4096);
  if (index >= 0 && access_points && index < access_points->size()) {
    AccessPoint ap = access_points->get(index);
    doc["index"] = index;
    doc["essid"] = ap.essid;
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
      ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
    doc["bssid"] = macStr;
    doc["channel"] = ap.channel;
    doc["rssi"] = ap.rssi;
    doc["security"] = wifi_scan_obj.security_int_to_string(ap.sec);
    doc["selected"] = ap.selected;
    doc["stations"] = ap.stations ? ap.stations->size() : 0;
    doc["wps"] = ap.wps;
  } else {
    // List all APs
    JsonArray arr = doc.createNestedArray("access_points");
    if (access_points) {
      for (int i = 0; i < access_points->size() && i < 50; i++) {
        AccessPoint ap = access_points->get(i);
        JsonObject o = arr.createNestedObject();
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
          ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
        o["index"] = i;
        o["essid"] = ap.essid;
        o["bssid"] = macStr;
        o["channel"] = ap.channel;
        o["rssi"] = ap.rssi;
        o["security"] = wifi_scan_obj.security_int_to_string(ap.sec);
        o["selected"] = ap.selected;
        o["stations"] = ap.stations ? ap.stations->size() : 0;
      }
    }
    doc["total"] = access_points ? access_points->size() : 0;
  }
  sendJson(request, doc);
}

void ApiServer::handleAPSelect(AsyncWebServerRequest *request) {
  String what = getArg(request, "what", "ap");
  String indices = getArg(request, "indices", "");
  String filter = getArg(request, "filter", "");
  int idx = getArgInt(request, "index", -1);
  bool toggle = getArg(request, "toggle", "false") == "1" || getArg(request, "toggle", "false") == "true";
  bool all = getArg(request, "all", "false") == "1" || getArg(request, "all", "false") == "true";
  bool clear = getArg(request, "clear", "false") == "1" || getArg(request, "clear", "false") == "true";
  DynamicJsonDocument doc(256);

  if (clear && access_points) { for (int i=0;i<access_points->size();i++){AccessPoint ap=access_points->get(i);ap.selected=false;access_points->set(i,ap);} }
  if (all && access_points) { for (int i=0;i<access_points->size();i++){AccessPoint ap=access_points->get(i);ap.selected=true;access_points->set(i,ap);} }
  if (idx>=0 && access_points && idx<access_points->size() && toggle){AccessPoint ap=access_points->get(idx);ap.selected=!ap.selected;access_points->set(idx,ap);}
  else if (idx>=0 && access_points && idx<access_points->size()){AccessPoint ap=access_points->get(idx);ap.selected=true;access_points->set(idx,ap);}
  if (indices.length()==0&&!all&&!clear&&idx<0&&!toggle) return;;
  
  if (indices.length() > 0) {
    // Parse comma-separated indices
    int start = 0;
    while (start < indices.length()) {
      int comma = indices.indexOf(',', start);
      if (comma == -1) comma = indices.length();
      int idx = indices.substring(start, comma).toInt();
      if (what == "ap" && access_points && idx < access_points->size()) {
        AccessPoint ap = access_points->get(idx);
        ap.selected = true;
        access_points->set(idx, ap);
      } else if (what == "station" && stations && idx < stations->size()) {
        Station st = stations->get(idx);
        st.selected = true;
        stations->set(idx, st);
      }
      start = comma + 1;
    }
  }
  
  doc["ok"] = true;
  doc["selected"] = true;
  sendJson(request, doc);
}

void ApiServer::handleAPAdd(AsyncWebServerRequest *request) {
  String mac = getArg(request, "mac", "");
  String essid = getArg(request, "essid", "");
  int channel = getArgInt(request, "channel", 1);
  DynamicJsonDocument doc(256);
  
  // Parse MAC
  uint8_t macBytes[6] = {0};
  sscanf(mac.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
    &macBytes[0], &macBytes[1], &macBytes[2], &macBytes[3], &macBytes[4], &macBytes[5]);
  
  AccessPoint newAP;
  memcpy(newAP.bssid, macBytes, 6);
  newAP.essid = essid;
  newAP.channel = channel;
  newAP.rssi = -50;
  newAP.selected = false;
  
  if (access_points) access_points->add(newAP);
  
  doc["ok"] = true;
  doc["added"] = mac;
  sendJson(request, doc);
}

void ApiServer::handleSSIDCmd(AsyncWebServerRequest *request) {
  String action = getArg(request, "action", "add");
  DynamicJsonDocument doc(256);
  
  if (action == "add") {
    String name = getArg(request, "name", "");
    int count = getArgInt(request, "count", 20);
    if (name.length() > 0) {
      wifi_scan_obj.addSSID(name);
      doc["ssid_added"] = name;
    } else {
      wifi_scan_obj.RunGenerateSSIDs(count);
      doc["ssids_generated"] = count;
    }
  } else if (action == "remove") {
    wifi_scan_obj.RunClearSSIDs();
    doc["ssids"] = "cleared";
  }
  doc["ok"] = true;
  sendJson(request, doc);
}

// ========== PERSISTENCE ==========

void ApiServer::handleSaveList(AsyncWebServerRequest *request) {
  String what = getArg(request, "what", "ap");
  String list = getArg(request, "list", "");
  if (list.length() > 0) what = list;
  DynamicJsonDocument doc(256);
  if (what == "ap" || what == "aps") wifi_scan_obj.RunSaveAPList(true);
  else if (what == "ssid" || what == "ssids") wifi_scan_obj.RunSaveSSIDList(true);
  else if (what == "sta" || what == "station" || what == "stations") wifi_scan_obj.RunSaveAPList(true);
  doc["ok"] = true;
  doc["saved"] = what;
  sendJson(request, doc);
}

void ApiServer::handleLoadList(AsyncWebServerRequest *request) {
  String what = getArg(request, "what", "ap");
  DynamicJsonDocument doc(256);
  if (what == "ap") wifi_scan_obj.RunLoadAPList();
  else if (what == "ssid") wifi_scan_obj.RunLoadSSIDList();
  else if (what == "station") wifi_scan_obj.RunLoadAPList();
  doc["ok"] = true;
  doc["loaded"] = what;
  sendJson(request, doc);
}
// ========== WIFI JOIN ==========

void ApiServer::handleJoinWiFi(AsyncWebServerRequest *request) {
  String ssid = getArg(request, "ssid", "");
  String password = getArg(request, "pw", "");
  if (password.length()==0) password = getArg(request, "password", "");
  int apIdx = getArgInt(request, "ap_index", -1);
  DynamicJsonDocument doc(256);
  
  if (ssid.length() == 0 && apIdx >= 0 && access_points && apIdx < access_points->size()) {
    ssid = access_points->get(apIdx).essid;
  }
  
  if (ssid.length() == 0) {
    sendError(request, "SSID required", 400);
    return;
  }
  
  bool result = wifi_scan_obj.joinWiFi(ssid, password, false);
  doc["ok"] = result;
  doc["ssid"] = ssid;
  if (result) {
    doc["ip"] = WiFi.localIP().toString();
  } else {
    doc["message"] = "Join failed";
  }
  sendJson(request, doc);
}

// ========== MAC SPOOFING ==========

void ApiServer::handleMacSpoof(AsyncWebServerRequest *request) {
  String action = getArg(request, "action", "randap");
  String type = getArg(request, "type", "");
  if (type.length() > 0) action = type;
  DynamicJsonDocument doc(256);
  
  if (action == "randap") {
    wifi_scan_obj.RunGenerateRandomMac(true);
    doc["type"] = "random_ap";
  } else if (action == "randsta") {
    wifi_scan_obj.RunGenerateRandomMac(false);
    doc["type"] = "random_station";
  } else if (action == "cloneap") {
    int idx = getArgInt(request, "index", getArgInt(request, "ap_index", -1));
    if (idx >= 0 && access_points && idx < access_points->size()) {
      wifi_scan_obj.RunSetMac(access_points->get(idx).bssid, true);
      doc["type"] = "clone_ap";
      doc["cloned_from"] = idx;
    } else {
      sendError(request, "Invalid AP index", 400);
      return;
    }
  } else if (action == "clonesta") {
    int idx = getArgInt(request, "station_index", -1);
    if (idx >= 0 && stations && idx < stations->size()) {
      wifi_scan_obj.RunSetMac(stations->get(idx).mac, false);
      doc["type"] = "clone_station";
      doc["cloned_from"] = idx;
    } else {
      sendError(request, "Invalid station index", 400);
      return;
    }
  }
  
  doc["ok"] = true;
  sendJson(request, doc);
}

// ========== BLUETOOTH ==========

void ApiServer::handleBluetooth(AsyncWebServerRequest *request) {
  #ifdef HAS_BT
    String path = request->url();
    String action = getArg(request, "action", "scan");
    String type = getArg(request, "type", "airtag");
    DynamicJsonDocument doc(256);
    
    if (path.indexOf("scan") > 0) {
      // BT sniff
      int mode = WIFI_SCAN_BLE;
      if (type == "airtag") mode = WIFI_SCAN_BLE;
      else if (type == "flipper") mode = WIFI_SCAN_BLE;
      else if (type == "flock") mode = WIFI_SCAN_BLE;
      else if (type == "meta") mode = WIFI_SCAN_BLE;
      
      wifi_scan_obj.RunBluetoothScan(mode, TFT_WHITE);
      doc["bt_action"] = "scan";
      doc["type"] = type;
    } else if (path.indexOf("spam") > 0) {
      // BT spam
      String spamType = getArg(request, "type", "sourapple");
      int mode = WIFI_SCAN_BLE;
      if (spamType == "sourapple") mode = BT_ATTACK_SOUR_APPLE;
      else if (spamType == "applejuice") mode = BT_ATTACK_APPLE_JUICE;
      else if (spamType == "swiftpair") mode = WIFI_SCAN_BLE;
      
      wifi_scan_obj.RunSourApple(mode, TFT_WHITE);
      doc["bt_action"] = "spam";
      doc["type"] = spamType;
    }
    
    doc["ok"] = true;
    sendJson(request, doc);
  #else
    sendError(request, "Bluetooth not available on this hardware", 400);
  #endif
}

void ApiServer::handleFindMy(AsyncWebServerRequest *request) {
  #if defined(HAS_BT) && defined(HAS_NIMBLE_2)
    String action = getArg(request, "action", "scan");
    DynamicJsonDocument doc(256);
    
    if (action == "scan") {
      wifi_scan_obj.RunFindMyLive(WIFI_SCAN_BLE, TFT_WHITE);
      doc["findmy"] = "scanning";
    } else if (action == "sound") {
      int tagIdx = getArgInt(request, "tag_index", 0);
      bool soundResult = wifi_scan_obj.executeFindMySound(false);
      doc["findmy"] = "sound";
      doc["sound_sent"] = soundResult;
      doc["tag_index"] = tagIdx;
    } else if (action == "stop") {
      wifi_scan_obj.StopScan(WIFI_SCAN_BLE);
      doc["findmy"] = "stopped";
    }
    
    doc["ok"] = true;
    sendJson(request, doc);
  #else
    sendError(request, "FindMy not available on this hardware", 400);
  #endif
}

// ========== UPLOAD ==========

void ApiServer::handleUpload(AsyncWebServerRequest *request) {
  String dest = getArg(request, "dest", "wigle");
  DynamicJsonDocument doc(256);
  int uploadType = 0;
  if (dest == "wigle") uploadType = 0;
  else if (dest == "wdg") uploadType = 1;
  else if (dest == "both") uploadType = 2;
  
  #ifdef HAS_SD
    // Find latest CSV on SD
    wifi_scan_obj.uploadFile("wardrive_output.csv", false, uploadType);
    doc["upload"] = "started";
    doc["dest"] = dest;
  #else
    doc["upload"] = "not_available";
    doc["message"] = "SD card required";
  #endif
  
  doc["ok"] = true;
  sendJson(request, doc);
}
// ========== GPS ==========

void ApiServer::handleGPS(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(1024);
  #ifdef HAS_GPS
    doc["fix"] = gps_obj.getFix();
    doc["lat"] = gps_obj.getLat();
    doc["lon"] = gps_obj.getLon();
    doc["alt"] = gps_obj.getAlt();
    doc["satellites"] = gps_obj.getNumSats();
    doc["accuracy"] = gps_obj.getAccuracy();
    doc["datetime"] = gps_obj.getDatetime();
    doc["nmea"] = gps_obj.getNmea();
  #else
    doc["available"] = false;
    doc["message"] = "GPS not available";
  #endif
  doc["ok"] = true;
  sendJson(request, doc);
}

void ApiServer::handleGPSTracker(AsyncWebServerRequest *request) {
  #ifdef HAS_GPS
    String action = getArg(request, "action", "start");
    DynamicJsonDocument doc(256);
    if (action == "start") {
      wifi_scan_obj.RunSetupGPSTracker(1);
      doc["tracker"] = "started";
    } else {
      wifi_scan_obj.StopScan(WIFI_SCAN_GPS_DATA);
      doc["tracker"] = "stopped";
    }
    doc["ok"] = true;
    sendJson(request, doc);
  #else
    sendError(request, "GPS not available", 400);
  #endif
}

// ========== LED ==========

void ApiServer::handleLed(AsyncWebServerRequest *request) {
  String action = getArg(request, "action", "color");
  DynamicJsonDocument doc(256);
  #if defined(HAS_NEOPIXEL_LED) || defined(HAS_BLACKHAT_LED)
    if (action == "color") {
      String hex = getArg(request, "hex", "#FF0000");
      long color = strtol(hex.c_str() + 1, NULL, 16);
      #ifdef HAS_BLACKHAT_LED
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;
        bh_led.setRed(r > 0);
        bh_led.setGreen(g > 0);
        bh_led.setBlue(b > 0);
      #else
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;
        led_obj.setColor(r, g, b);
      #endif
      doc["led"] = "color_set";
      doc["color"] = hex;
    } else if (action == "rainbow") {
      #ifdef HAS_BLACKHAT_LED
        bh_led.blinkRed(3, 200);
        bh_led.blinkGreen(3, 200);
        bh_led.blinkBlue(3, 200);
      #else
        led_obj.setMode(MODE_RAINBOW);
      #endif
      doc["led"] = "rainbow";
    } else if (action == "off") {
      #ifdef HAS_BLACKHAT_LED
        bh_led.setRed(false);
        bh_led.setGreen(false);
        bh_led.setBlue(false);
      #else
        led_obj.setMode(MODE_OFF);
      #endif
      doc["led"] = "off";
    }
    doc["ok"] = true;
  #else
    doc["ok"] = false;
    doc["message"] = "LED not available";
  #endif
  sendJson(request, doc);
}

// ========== SYSTEM ==========

void ApiServer::handleReboot(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(256);
  doc["ok"] = true;
  doc["message"] = "Rebooting...";
  sendJson(request, doc);
  delay(500);
  ESP.restart();
}

// ========== FILE MANAGEMENT ==========

void ApiServer::handleFileList(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(4096);
  #ifdef HAS_SD
    JsonArray arr = doc.createNestedArray("files");
    if (sd_obj.supported) {
      File root = SD.open("/");
      if (root) {
        File file = root.openNextFile();
        while (file) {
          if (!file.isDirectory()) {
            JsonObject f = arr.createNestedObject();
            f["name"] = String(file.name());
            f["size"] = file.size();
            arr.add(f);
          }
          file = root.openNextFile();
        }
      }
    }
    doc["sd_supported"] = sd_obj.supported;
  #else
    doc["sd_supported"] = false;
  #endif
  doc["ok"] = true;
  sendJson(request, doc);
}

void ApiServer::handleFileUpload(AsyncWebServerRequest *request) {
  sendError(request, "File upload via POST body not implemented", 400);
}

void ApiServer::handleFileDelete(AsyncWebServerRequest *request) {
  String filename = getArg(request, "name", "");
  DynamicJsonDocument doc(256);
  #ifdef HAS_SD
    if (filename.length() > 0 && sd_obj.supported) {
      String path = "/" + filename;
      if (SD.exists(path)) {
        SD.remove(path);
        doc["deleted"] = filename;
      } else {
        doc["message"] = "File not found";
      }
    }
  #else
    doc["message"] = "SD not available";
  #endif
  doc["ok"] = true;
  sendJson(request, doc);
}
// ========== DATA ENDPOINTS (real-time polling) ==========

void ApiServer::handleDataAP(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.createNestedArray("access_points");
  if (access_points) {
    for (int i = 0; i < access_points->size() && i < 50; i++) {
      AccessPoint ap = access_points->get(i);
      JsonObject o = arr.createNestedObject();
      char macStr[18];
      snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
        ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
      o["index"] = i;
      o["essid"] = ap.essid;
      o["bssid"] = macStr;
      o["channel"] = ap.channel;
      o["rssi"] = ap.rssi;
      o["security"] = wifi_scan_obj.security_int_to_string(ap.sec);
      o["selected"] = ap.selected;
      o["stations"] = ap.stations ? ap.stations->size() : 0;
      o["wps"] = ap.wps;
    }
  }
  doc["total"] = access_points ? access_points->size() : 0;
  doc["scanning"] = wifi_scan_obj.scanning();
  sendJson(request, doc);
}

void ApiServer::handleDataStation(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.createNestedArray("stations");
  if (stations) {
    for (int i = 0; i < stations->size() && i < 50; i++) {
      Station st = stations->get(i);
      JsonObject o = arr.createNestedObject();
      char macStr[18];
      snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
        st.mac[0], st.mac[1], st.mac[2], st.mac[3], st.mac[4], st.mac[5]);
      o["index"] = i;
      o["mac"] = macStr;
      o["ap_index"] = st.ap;
      o["packets"] = st.packets;
      o["selected"] = st.selected;
    }
  }
  doc["total"] = stations ? stations->size() : 0;
  sendJson(request, doc);
}

void ApiServer::handleDataBLE(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.createNestedArray("ble_devices");
  #ifdef HAS_BT
    if (ble_devices) {
      for (int i = 0; i < ble_devices->size() && i < 50; i++) {
        BleDevice bt = ble_devices->get(i);
        JsonObject o = arr.createNestedObject();
        o["name"] = bt.name;
        o["rssi"] = bt.rssi;
        o["index"] = i;
        // Add MAC as string
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
          bt.mac[0], bt.mac[1], bt.mac[2], bt.mac[3], bt.mac[4], bt.mac[5]);
        o["mac"] = macStr;
      }
    }
    doc["total"] = ble_devices ? ble_devices->size() : 0;
  #endif
  sendJson(request, doc);
}

void ApiServer::handleDataSSID(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.createNestedArray("ssids");
  if (ssids) {
    for (int i = 0; i < ssids->size() && i < 50; i++) {
      arr.add(ssids->get(i).essid);
    }
  }
  doc["total"] = ssids ? ssids->size() : 0;
  sendJson(request, doc);
}

void ApiServer::handleDataRawStats(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(1024);
  doc["mgmt_frames"] = wifi_scan_obj.mgmt_frames;
  doc["data_frames"] = wifi_scan_obj.data_frames;
  doc["beacon_frames"] = wifi_scan_obj.beacon_frames;
  doc["req_frames"] = wifi_scan_obj.req_frames;
  doc["resp_frames"] = wifi_scan_obj.resp_frames;
  doc["deauth_frames"] = wifi_scan_obj.deauth_frames;
  doc["eapol_frames"] = wifi_scan_obj.eapol_frames;
  doc["complete_eapol"] = wifi_scan_obj.complete_eapol;
  doc["flock_devices"] = wifi_scan_obj.flock_devices;
  doc["min_rssi"] = wifi_scan_obj.min_rssi;
  doc["max_rssi"] = wifi_scan_obj.max_rssi;
  doc["bt_frames"] = wifi_scan_obj.bt_frames;
  doc["scan_mode"] = wifi_scan_obj.currentScanMode;
  doc["channel"] = wifi_scan_obj.set_channel;
  sendJson(request, doc);
}

// ========== MAIN LOOP TICK ==========

void ApiServer::handleClient() {
  // AsyncWebServer handles itself. Here we only poll the async STA state so
  // we don't block at boot: report connect/disconnect transitions once.
  static int lastStaStatus = -1;
  int s = (int)WiFi.status();
  if (s != lastStaStatus) {
    lastStaStatus = s;
    if (s == WL_CONNECTED) {
      _wifi_connected = true;
      Serial.printf("[API] STA connected! STA IP: %s · AP: %s\n",
                    WiFi.localIP().toString().c_str(), _device_ip.c_str());
    } else if (s == WL_DISCONNECTED && _wifi_connected) {
      _wifi_connected = false;
      Serial.println("[API] STA disconnected — AP remains up");
    }
  }

  // raw diagnostic server (port 8080) — accept + reply "pong\n"
  if (_diagServer) {
    WiFiClient cl = _diagServer->accept();
    if (cl) {
      Serial.println("[DIAG] raw client connected on 8080");
      cl.setTimeout(1000);
      while (cl.connected()) {
        if (cl.available()) {
          String ln = cl.readStringUntil('\n');
          if (ln.length() <= 2) break;  // empty line -> end of headers
        }
      }
      String body = "pong\n";
      String resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                    String(body.length()) + "\r\nConnection: close\r\n\r\n" + body;
      cl.print(resp);
      cl.flush();
      delay(10);
      cl.stop();
      Serial.println("[DIAG] raw 8080 replied 'pong'");
    }
  }
}
