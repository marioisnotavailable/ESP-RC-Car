#include "rc_httpapi.h"
#include "rc_settings.h"
#include "rc_battery.h"
#include "rc_network.h"
#include "rc_pins.h"
#include <LittleFS.h>

#ifndef FOTA_CURRENT_VERSION
#define FOTA_CURRENT_VERSION "v0.0.0"
#endif

WebServer  httpServer(80);
static DNSServer dnsServer;

// ---- File serving ----
static String getContentType(const String& path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css"))  return "text/css";
  if (path.endsWith(".js"))   return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".jpg"))  return "image/jpeg";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".ico"))  return "image/x-icon";
  return "text/plain";
}

static bool serveFile(const String& path) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  String ct = getContentType(path);
  httpServer.streamFile(f, ct);
  f.close();
  return true;
}

void rc_start_portal() {
  startConfigPortal = true;

  String apSsid = String(settings.apPrefix) + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF), HEX);
  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.softAP(apSsid.c_str());
  IPAddress ip = WiFi.softAPIP();

  Serial.printf("[WiFi] Config AP started: SSID=%s IP=%s\n", apSsid.c_str(), ip.toString().c_str());

  rc_wifi_scan();

  dnsServer.start(53, "*", ip);

  // Static files
  httpServer.on("/", HTTP_GET, []() {
    if (!serveFile("/index.html")) httpServer.send(404, "text/plain", "index.html not found");
  });
  httpServer.on("/app.js", HTTP_GET, []() {
    if (!serveFile("/app.js")) httpServer.send(404, "text/plain", "app.js not found");
  });
  httpServer.on("/styles.css", HTTP_GET, []() {
    if (!serveFile("/styles.css")) httpServer.send(404, "text/plain", "styles.css not found");
  });

  // Captive portal probes
  auto portalRedirect = []() {
    httpServer.sendHeader("Cache-Control", "no-store");
    httpServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
    httpServer.send(302, "text/plain", "");
  };
  httpServer.on("/generate_204", HTTP_GET, portalRedirect);
  httpServer.on("/gen_204", HTTP_GET, portalRedirect);
  httpServer.on("/hotspot-detect.html", HTTP_GET, portalRedirect);
  httpServer.on("/library/test/success.html", HTTP_GET, portalRedirect);
  httpServer.on("/canonical.html", HTTP_GET, portalRedirect);
  httpServer.on("/ncsi.txt", HTTP_GET, portalRedirect);
  httpServer.on("/connecttest.txt", HTTP_GET, portalRedirect);
  httpServer.on("/fwlink", HTTP_GET, portalRedirect);
  httpServer.on("/success.txt", HTTP_GET, portalRedirect);

  // ---- JSON API ----
  httpServer.on("/api/saved", HTTP_GET, []() {
    String j = "[";
    for (size_t i = 0; i < savedNets.size(); ++i) {
      if (i) j += ',';
      j += "{\"i\":" + String(i) + ",\"ssid\":\"" + savedNets[i].ssid + "\"}";
    }
    j += "]";
    httpServer.send(200, "application/json", j);
  });

  httpServer.on("/api/save", HTTP_POST, []() {
    String ssid = httpServer.arg("ssid");
    String pass = httpServer.arg("pass");
    bool ok = rc_network_add(ssid, pass);
    httpServer.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
  });

  httpServer.on("/api/delete", HTTP_POST, []() {
    int idx = httpServer.arg("i").toInt();
    bool ok = rc_network_delete(idx);
    httpServer.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
  });

  httpServer.on("/api/move", HTTP_POST, []() {
    int from = httpServer.arg("from").toInt();
    int to   = httpServer.arg("to").toInt();
    bool ok  = false;
    if (from >= 0 && from < (int)savedNets.size() && to >= 0 && to < (int)savedNets.size() && from != to) {
      std::swap(savedNets[from], savedNets[to]);
      rc_network_save();
      ok = true;
    }
    httpServer.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
  });

  httpServer.on("/api/scan", HTTP_POST, []() {
    rc_wifi_scan();
    String j = "[";
    for (size_t i = 0; i < lastScan.size(); ++i) {
      if (i) j += ',';
      j += "{\"ssid\":\"" + lastScan[i].ssid + "\",";
      j += "\"rssi\":" + String(lastScan[i].rssi) + ",";
      j += "\"enc\":" + String((int)lastScan[i].enc) + "}";
    }
    j += "]";
    httpServer.send(200, "application/json", j);
  });

  httpServer.on("/api/adc", HTTP_GET, []() {
    char j[128];
    snprintf(j, sizeof(j), "{\"vAdc\":%.3f,\"vBatt\":%.2f,\"percent\":%d}",
      vAdc_last, vBatt_float_last, batteryPercent);
    httpServer.send(200, "application/json", j);
  });

  httpServer.on("/api/restart-charge", HTTP_POST, []() {
    pinMode(CHARGE_RESTART_PIN, OUTPUT);
    digitalWrite(CHARGE_RESTART_PIN, HIGH);
    delay(100);
    digitalWrite(CHARGE_RESTART_PIN, LOW);
    Serial.println("[DEBUG] GPIO47 pulsed HIGH->LOW (restart charging)");
    httpServer.send(200, "application/json", "{\"ok\":true}");
  });

  httpServer.on("/api/reboot", HTTP_POST, []() {
    httpServer.send(200, "application/json", "{\"ok\":true}");
    delay(200);
    ESP.restart();
  });

  // ---- Settings API ----
  httpServer.on("/api/settings", HTTP_GET, []() {
    String j = "{";
    j += "\"otaEnabled\":" + String(settings.otaEnabled ? "true" : "false") + ",";
    j += "\"otaIntervalMs\":" + String(settings.otaIntervalMs) + ",";
    j += "\"wifiTxPower\":" + String(settings.wifiTxPower) + ",";
    j += "\"failsafeMs\":" + String(settings.failsafeMs) + ",";
    j += "\"beaconIntervalMs\":" + String(settings.beaconIntervalMs) + ",";
    j += "\"apPrefix\":\"" + String(settings.apPrefix) + "\",";
    j += "\"steerInvert\":" + String(settings.steerInvert ? "true" : "false") + ",";
    j += "\"steerGain\":" + String(settings.steerGain, 2) + ",";
    j += "\"steerDeadzone\":" + String(settings.steerDeadzone) + ",";
    j += "\"steerFilter\":" + String(settings.steerFilter, 2) + ",";
    j += "\"battWarnV\":" + String(settings.battWarnV, 1) + ",";
    j += "\"battOffV\":" + String(settings.battOffV, 1) + ",";
    j += "\"maxThrottlePct\":" + String(settings.maxThrottlePct) + ",";
    j += "\"adcCorrFactor\":" + String(settings.adcCorrFactor, 4) + ",";
    j += "\"vBatt\":" + String(vBatt_float_last, 2) + ",";
    j += "\"version\":\"" + String(FOTA_CURRENT_VERSION) + "\"";
    j += "}";
    httpServer.send(200, "application/json", j);
  });

  httpServer.on("/api/settings", HTTP_POST, []() {
    if (httpServer.hasArg("otaEnabled"))
      settings.otaEnabled = (httpServer.arg("otaEnabled") == "1");
    if (httpServer.hasArg("otaIntervalMs"))
      settings.otaIntervalMs = httpServer.arg("otaIntervalMs").toInt();
    if (httpServer.hasArg("wifiTxPower")) {
      settings.wifiTxPower = httpServer.arg("wifiTxPower").toInt();
      rc_apply_wifi_tx_power();
    }
    if (httpServer.hasArg("failsafeMs"))
      settings.failsafeMs = httpServer.arg("failsafeMs").toInt();
    if (httpServer.hasArg("beaconIntervalMs"))
      settings.beaconIntervalMs = httpServer.arg("beaconIntervalMs").toInt();
    if (httpServer.hasArg("apPrefix")) {
      String ap = httpServer.arg("apPrefix");
      strncpy(settings.apPrefix, ap.c_str(), sizeof(settings.apPrefix) - 1);
      settings.apPrefix[sizeof(settings.apPrefix) - 1] = '\0';
    }
    if (httpServer.hasArg("steerInvert"))
      settings.steerInvert = (httpServer.arg("steerInvert") == "1");
    if (httpServer.hasArg("steerGain"))
      settings.steerGain = constrain(httpServer.arg("steerGain").toFloat(), 0.3f, 1.5f);
    if (httpServer.hasArg("steerDeadzone"))
      settings.steerDeadzone = constrain(httpServer.arg("steerDeadzone").toInt(), 0, 200);
    if (httpServer.hasArg("steerFilter"))
      settings.steerFilter = constrain(httpServer.arg("steerFilter").toFloat(), 0.5f, 0.95f);
    if (httpServer.hasArg("battWarnV"))
      settings.battWarnV = httpServer.arg("battWarnV").toFloat();
    if (httpServer.hasArg("battOffV"))
      settings.battOffV = httpServer.arg("battOffV").toFloat();
    if (httpServer.hasArg("maxThrottlePct"))
      settings.maxThrottlePct = constrain(httpServer.arg("maxThrottlePct").toInt(), 10, 100);
    if (httpServer.hasArg("adcCorrFactor")) {
      float f = httpServer.arg("adcCorrFactor").toFloat();
      if (f > 0.5f && f < 2.0f) settings.adcCorrFactor = f;
    }
    if (httpServer.hasArg("adcRealVoltage") && vBatt_float_last > 0.1f) {
      float realV = httpServer.arg("adcRealVoltage").toFloat();
      if (realV > 1.0f && realV < 15.0f)
        settings.adcCorrFactor = realV / (vBatt_float_last / settings.adcCorrFactor);
    }
    rc_settings_save();
    httpServer.send(200, "application/json", "{\"ok\":true}");
  });

  // Default: redirect unknown paths to root
  httpServer.onNotFound([]() {
    httpServer.sendHeader("Cache-Control", "no-store");
    httpServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
    httpServer.send(302, "text/plain", "");
  });

  httpServer.begin();
}

void rc_portal_loop() {
  if (startConfigPortal) {
    dnsServer.processNextRequest();
    httpServer.handleClient();
  }
}
