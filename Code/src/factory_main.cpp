// Factory Recovery Firmware — Minimaler Safe Mode
// WiFi-AP + ArduinoOTA (immer) + GitHub FOTA (muss freigeschaltet werden)
// Liest gespeicherte WLANs aus NVS, WiFi auch per Web konfigurierbar.

#ifdef FACTORY_BUILD

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#define FOTA_API_URL "https://api.github.com/repos/marioisnotavailable/ESP-RC-Car/releases/latest"
#define FOTA_FW_URL  "https://github.com/marioisnotavailable/ESP-RC-Car/releases/latest/download/esp-rc-car.bin"
#define FOTA_FS_URL  "https://github.com/marioisnotavailable/ESP-RC-Car/releases/latest/download/esp-rc-car-littlefs.bin"

static WebServer http(80);
static const char* AP_SSID = "ESP-RC-RECOVERY";
static const char* AP_PASS = "recovery123";
static bool fotaEnabled = false;
static bool fotaRunning = false;

// ---- NVS WiFi laden + verbinden ----
struct SavedNet { String ssid; String pass; };
static std::vector<SavedNet> savedNets;

static void loadNetsFromNVS() {
  Preferences prefs;
  prefs.begin("wifi", true);
  String raw = prefs.getString("nets", "");
  prefs.end();

  if (raw.length() == 0) return;

  int pos = 0;
  while (pos < (int)raw.length()) {
    int nl = raw.indexOf('\n', pos);
    String line = (nl >= 0) ? raw.substring(pos, nl) : raw.substring(pos);
    pos = (nl >= 0) ? nl + 1 : raw.length();
    if (line.length() == 0) continue;
    int sep = line.indexOf('\t');
    if (sep <= 0) continue;

    SavedNet n;
    n.ssid = line.substring(0, sep);
    String rest = line.substring(sep + 1);
    int sep2 = rest.indexOf('\t');
    n.pass = (sep2 >= 0) ? rest.substring(0, sep2) : rest;
    savedNets.push_back(n);
  }
  Serial.printf("[REC] %d Netzwerk(e) aus NVS geladen\n", (int)savedNets.size());
}

static bool tryConnectSaved() {
  for (size_t i = 0; i < savedNets.size(); i++) {
    Serial.printf("[REC] Versuche '%s'...\n", savedNets[i].ssid.c_str());
    WiFi.begin(savedNets[i].ssid.c_str(), savedNets[i].pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) {
      delay(250);
      wl_status_t st = WiFi.status();
      if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) break;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[REC] Verbunden mit '%s' — IP: %s\n",
        savedNets[i].ssid.c_str(), WiFi.localIP().toString().c_str());
      return true;
    }
    WiFi.disconnect(true);
    delay(100);
  }
  Serial.println("[REC] Kein gespeichertes WLAN erreichbar");
  return false;
}

// ---- FOTA ----
static bool fotaFlash(const char* url, int type) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(30000);
  if (!http.begin(client, url)) {
    Serial.printf("[FOTA] begin fail: %s\n", url);
    return false;
  }
  http.addHeader("User-Agent", "ESP32-Recovery/1.0");

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[FOTA] HTTP %d: %s\n", code, url);
    http.end();
    return false;
  }

  int totalSize = http.getSize();
  Serial.printf("[FOTA] Download: %d bytes\n", totalSize);

  if (!Update.begin(totalSize > 0 ? (size_t)totalSize : UPDATE_SIZE_UNKNOWN, type)) {
    Serial.printf("[FOTA] Update.begin fail: %s\n", Update.errorString());
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  int written = 0;
  uint32_t deadline = millis() + 60000UL;

  while (http.connected() && (totalSize < 0 || written < totalSize)) {
    if (millis() > deadline) {
      Serial.println("[FOTA] Timeout");
      http.end();
      Update.abort();
      return false;
    }
    int avail = stream->available();
    if (avail > 0) {
      int n = stream->readBytes(buf, min(avail, (int)sizeof(buf)));
      if (n > 0) {
        if (Update.write(buf, n) != (size_t)n) {
          Serial.printf("[FOTA] Write fail: %s\n", Update.errorString());
          http.end();
          Update.abort();
          return false;
        }
        written += n;
      }
    } else {
      delay(1);
    }
  }
  http.end();

  if (!Update.end(true)) {
    Serial.printf("[FOTA] End fail: %s\n", Update.errorString());
    return false;
  }
  Serial.printf("[FOTA] OK: %d bytes\n", written);
  return true;
}

static void fotaCheckAndUpdate() {
  fotaRunning = true;
  Serial.println("[FOTA] Pruefe GitHub Release...");

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10);

  HTTPClient apiHttp;
  apiHttp.setTimeout(10000);
  if (!apiHttp.begin(client, FOTA_API_URL)) {
    Serial.println("[FOTA] API begin fail");
    fotaRunning = false;
    return;
  }
  apiHttp.addHeader("User-Agent", "ESP32-Recovery/1.0");
  apiHttp.addHeader("Accept", "application/vnd.github+json");

  int code = apiHttp.GET();
  if (code != 200) {
    Serial.printf("[FOTA] API HTTP %d\n", code);
    apiHttp.end();
    fotaRunning = false;
    return;
  }

  JsonDocument filter;
  filter["tag_name"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, apiHttp.getStream(), DeserializationOption::Filter(filter))) {
    Serial.println("[FOTA] JSON parse fail");
    apiHttp.end();
    fotaRunning = false;
    return;
  }
  apiHttp.end();

  const char* tag = doc["tag_name"];
  if (!tag || strlen(tag) == 0) {
    Serial.println("[FOTA] Kein tag_name");
    fotaRunning = false;
    return;
  }
  Serial.printf("[FOTA] Release gefunden: %s\n", tag);

  Serial.println("[FOTA] Flashe LittleFS...");
  if (!fotaFlash(FOTA_FS_URL, U_SPIFFS)) {
    Serial.println("[FOTA] LittleFS FAIL — Abbruch");
    fotaRunning = false;
    return;
  }

  Serial.println("[FOTA] Flashe Firmware...");
  if (!fotaFlash(FOTA_FW_URL, U_FLASH)) {
    Serial.println("[FOTA] Firmware FAIL");
    fotaRunning = false;
    return;
  }

  Serial.println("[FOTA] Fertig — Neustart...");
  delay(500);
  ESP.restart();
}

// ---- Web Page ----
static void sendPage() {
  bool wifiOk = (WiFi.status() == WL_CONNECTED);

  String page = F(
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP Recovery</title>"
    "<style>"
    "body{font-family:monospace;background:#1a1a2e;color:#e0e0e0;padding:20px;text-align:center}"
    "h1{color:#e94560}"
    ".box{background:#16213e;padding:20px;border-radius:8px;max-width:400px;margin:20px auto}"
    "button{background:#e94560;color:#fff;border:none;padding:10px 20px;border-radius:4px;"
    "cursor:pointer;font-size:16px;margin:5px;width:90%}"
    "button:hover{background:#c73650}"
    "button.off{background:#555}"
    "input[type=text],input[type=password]{width:80%;padding:8px;margin:4px;border:1px solid #444;"
    "background:#0f3460;color:#e0e0e0;border-radius:4px;font-family:monospace}"
    ".ok{color:#4caf50}.off{color:#e94560}"
    ".info{color:#888;font-size:12px;margin-top:15px}"
    "hr{border:none;border-top:1px solid #333;margin:15px 0}"
    "</style></head><body>"
    "<h1>RECOVERY MODE</h1>");

  // WiFi Status
  page += F("<div class='box'><p>WiFi: ");
  if (wifiOk) {
    page += "<span class='ok'>Verbunden</span> — " + WiFi.localIP().toString();
  } else {
    page += F("<span class='off'>Nicht verbunden</span>");
  }
  page += F("</p>");

  // WiFi Formular
  page += F("<hr><p>WLAN verbinden:</p>"
            "<form method='POST' action='/wifi'>"
            "<input type='text' name='ssid' placeholder='SSID' required><br>"
            "<input type='password' name='pass' placeholder='Passwort'><br>"
            "<button type='submit'>Verbinden</button></form>");

  // Gespeicherte Netze
  if (!savedNets.empty()) {
    page += F("<hr><p>Gespeicherte Netzwerke:</p>");
    for (size_t i = 0; i < savedNets.size(); i++) {
      page += "<form method='POST' action='/wifi-saved'>"
              "<input type='hidden' name='i' value='" + String(i) + "'>"
              "<button type='submit' class='off'>" + savedNets[i].ssid + "</button></form>";
    }
  }
  page += F("</div>");

  // FOTA
  page += F("<div class='box'><p>GitHub FOTA: <span class='");
  page += fotaEnabled ? "ok'>AKTIV" : "off'>GESPERRT";
  page += F("</span></p>");

  if (!fotaEnabled) {
    page += F("<form method='POST' action='/fota-on'>"
              "<button type='submit'>FOTA aktivieren</button></form>");
  } else if (fotaRunning) {
    page += F("<p>Update laeuft... Bitte warten</p>");
  } else {
    if (wifiOk) {
      page += F("<form method='POST' action='/fota-start'>"
                "<button type='submit'>Jetzt von GitHub updaten</button></form>");
    } else {
      page += F("<p class='off'>Zuerst WLAN verbinden</p>");
    }
    page += F("<form method='POST' action='/fota-off'>"
              "<button type='submit' class='off'>FOTA deaktivieren</button></form>");
  }

  page += F("<p class='info'>ArduinoOTA immer aktiv (Port 3232)</p>"
            "</div></body></html>");
  http.send(200, "text/html", page);
}

// ---- Setup ----
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=================================");
  Serial.println("  FACTORY RECOVERY FIRMWARE");
  Serial.println("=================================");
  Serial.println();

  // Gespeicherte Netzwerke aus NVS laden
  loadNetsFromNVS();

  // WiFi AP+STA — AP fuer Zugriff, STA fuer Internet/FOTA
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("[REC] AP: %s / %s\n", AP_SSID, AP_PASS);
  Serial.printf("[REC] IP: %s\n", WiFi.softAPIP().toString().c_str());

  // Automatisch mit gespeichertem WLAN verbinden
  if (!savedNets.empty()) {
    tryConnectSaved();
  }

  // ArduinoOTA — immer aktiv
  ArduinoOTA.setHostname("esp-rc-recovery");
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
  ArduinoOTA.onStart([]() { Serial.println("[OTA] Start..."); });
  ArduinoOTA.onEnd([]() { Serial.println("[OTA] Done — reboot"); });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    Serial.printf("[OTA] %u%%\r", (p * 100) / t);
  });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[OTA] Error %d\n", e); });
  ArduinoOTA.begin();

  // HTTP Routes
  http.on("/", HTTP_GET, []() { sendPage(); });

  http.on("/wifi", HTTP_POST, []() {
    String ssid = http.arg("ssid");
    String pass = http.arg("pass");
    Serial.printf("[REC] Web-WiFi: '%s'\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) delay(250);
    if (WiFi.status() == WL_CONNECTED)
      Serial.printf("[REC] Verbunden! IP: %s\n", WiFi.localIP().toString().c_str());
    else
      Serial.println("[REC] Verbindung fehlgeschlagen");
    http.sendHeader("Location", "/");
    http.send(302);
  });

  http.on("/wifi-saved", HTTP_POST, []() {
    int i = http.arg("i").toInt();
    if (i >= 0 && i < (int)savedNets.size()) {
      Serial.printf("[REC] Verbinde mit '%s'...\n", savedNets[i].ssid.c_str());
      WiFi.begin(savedNets[i].ssid.c_str(), savedNets[i].pass.c_str());
      uint32_t t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) delay(250);
      if (WiFi.status() == WL_CONNECTED)
        Serial.printf("[REC] Verbunden! IP: %s\n", WiFi.localIP().toString().c_str());
      else
        Serial.println("[REC] Verbindung fehlgeschlagen");
    }
    http.sendHeader("Location", "/");
    http.send(302);
  });

  http.on("/fota-on", HTTP_POST, []() {
    fotaEnabled = true;
    Serial.println("[REC] FOTA aktiviert");
    http.sendHeader("Location", "/");
    http.send(302);
  });
  http.on("/fota-off", HTTP_POST, []() {
    fotaEnabled = false;
    Serial.println("[REC] FOTA deaktiviert");
    http.sendHeader("Location", "/");
    http.send(302);
  });
  http.on("/fota-start", HTTP_POST, []() {
    if (!fotaEnabled) { http.send(403, "text/plain", "FOTA gesperrt"); return; }
    if (WiFi.status() != WL_CONNECTED) {
      http.sendHeader("Location", "/");
      http.send(302);
      return;
    }
    http.send(200, "text/plain", "FOTA gestartet... ESP startet nach Update neu.");
    fotaCheckAndUpdate();
  });

  http.begin();
  Serial.println("[REC] HTTP-Server bereit");
  Serial.println("[REC] FOTA gesperrt — Freigabe per Web oder Serial 'fota on'");
}

// ---- Loop ----
void loop() {
  ArduinoOTA.handle();
  http.handleClient();

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "fota on") {
      fotaEnabled = true;
      Serial.println("[REC] FOTA aktiviert");
    } else if (cmd == "fota off") {
      fotaEnabled = false;
      Serial.println("[REC] FOTA deaktiviert");
    } else if (cmd == "fota start") {
      if (!fotaEnabled) { Serial.println("[REC] Gesperrt — zuerst 'fota on'"); return; }
      if (WiFi.status() != WL_CONNECTED) { Serial.println("[REC] Kein WLAN"); return; }
      fotaCheckAndUpdate();
    } else if (cmd == "status") {
      Serial.printf("[REC] FOTA: %s | WiFi: %s | AP Clients: %d\n",
        fotaEnabled ? "ON" : "OFF",
        WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "nicht verbunden",
        WiFi.softAPgetStationNum());
    } else if (cmd == "help") {
      Serial.println("  fota on/off   — GitHub-Update ein/ausschalten");
      Serial.println("  fota start    — Jetzt von GitHub updaten");
      Serial.println("  status        — Status anzeigen");
    }
  }
}

#endif // FACTORY_BUILD
