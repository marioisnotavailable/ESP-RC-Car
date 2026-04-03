#include "rc_httpapi.h"
#include "rc_settings.h"
#include "rc_battery.h"
#include "rc_network.h"
#include "rc_serial.h"
#include "rc_console.h"
#include "rc_pins.h"
#include <LittleFS.h>
#include <ESPmDNS.h>

#ifndef FOTA_CURRENT_VERSION
#define FOTA_CURRENT_VERSION "v0.0.0"
#endif

WebServer  httpServer(80);
static DNSServer dnsServer;
static bool dnsActive = false;
static bool routesRegistered = false;
static bool httpStarted = false;
static bool captivePortalMode = false;
static bool mdnsActive = false;
static String mdnsHost;

static void setNoCacheHeaders() {
  httpServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  httpServer.sendHeader("Pragma", "no-cache");
  httpServer.sendHeader("Expires", "0");
}

static String buildMdnsHost() {
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06lx", (unsigned long)(ESP.getEfuseMac() & 0xFFFFFF));
  String host = "esp-rccar-";
  host += suffix;
  host.toLowerCase();
  return host;
}

static void startMdnsService() {
  if (mdnsActive) {
    MDNS.end();
    mdnsActive = false;
  }

  mdnsHost = buildMdnsHost();
  if (!MDNS.begin(mdnsHost.c_str())) {
    console.println("[HTTP] mDNS start failed");
    return;
  }

  MDNS.addService("http", "tcp", 80);
  mdnsActive = true;
  console.printf("[HTTP] mDNS started: http://%s.local/\n", mdnsHost.c_str());
}

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
  if (!f) {
    if (logFlags.http) console.printf("[HTTP] File not found: %s\n", path.c_str());
    return false;
  }
  String ct = getContentType(path);
  if (logFlags.http) console.printf("[HTTP] Serving %s (%d bytes, %s)\n", path.c_str(), (int)f.size(), ct.c_str());
  httpServer.streamFile(f, ct);
  f.close();
  return true;
}

static void ensureHttpRoutesRegistered() {
  if (routesRegistered) return;

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
    String target = "/";
    if (captivePortalMode) {
      target = String("http://") + WiFi.softAPIP().toString() + "/";
    } else if (WiFi.status() == WL_CONNECTED) {
      target = String("http://") + WiFi.localIP().toString() + "/";
    }
    httpServer.sendHeader("Location", target);
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
    if (logFlags.http) console.printf("[HTTP] GET /api/saved (%d nets)\n", (int)savedNets.size());
    String j = "[";
    for (size_t i = 0; i < savedNets.size(); ++i) {
      if (i) j += ',';
      j += "{\"i\":" + String(i) + ",\"ssid\":\"" + savedNets[i].ssid + "\",\"lastConn\":" + String(savedNets[i].lastConnected) + "}";
    }
    j += "]";
    setNoCacheHeaders();
    httpServer.send(200, "application/json", j);
  });

  httpServer.on("/api/save", HTTP_POST, []() {
    String ssid = httpServer.arg("ssid");
    String pass = httpServer.arg("pass");
    if (logFlags.http) console.printf("[HTTP] POST /api/save ssid=\"%s\" pass_len=%d\n", ssid.c_str(), (int)pass.length());
    bool ok = rc_network_add(ssid, pass);
    if (logFlags.http) console.printf("[HTTP] /api/save result: %s\n", ok ? "OK" : "FAIL");
    httpServer.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
  });

  httpServer.on("/api/delete", HTTP_POST, []() {
    int idx = httpServer.arg("i").toInt();
    if (logFlags.http) console.printf("[HTTP] POST /api/delete idx=%d\n", idx);
    bool ok = rc_network_delete(idx);
    if (logFlags.http) console.printf("[HTTP] /api/delete result: %s\n", ok ? "OK" : "FAIL");
    httpServer.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
  });

  httpServer.on("/api/move", HTTP_POST, []() {
    int from = httpServer.arg("from").toInt();
    int to   = httpServer.arg("to").toInt();
    if (logFlags.http) console.printf("[HTTP] POST /api/move from=%d to=%d\n", from, to);
    bool ok  = false;
    if (from >= 0 && from < (int)savedNets.size() && to >= 0 && to < (int)savedNets.size() && from != to) {
      std::swap(savedNets[from], savedNets[to]);
      rc_network_save();
      ok = true;
    }
    if (logFlags.http) console.printf("[HTTP] /api/move result: %s\n", ok ? "OK" : "FAIL");
    httpServer.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
  });

  httpServer.on("/api/scan", HTTP_POST, []() {
    if (logFlags.http) console.println("[HTTP] POST /api/scan — starting WiFi scan...");
    rc_wifi_scan();
    if (logFlags.http) console.printf("[HTTP] /api/scan result: %d networks\n", (int)lastScan.size());
    String j = "[";
    for (size_t i = 0; i < lastScan.size(); ++i) {
      if (i) j += ',';
      j += "{\"ssid\":\"" + lastScan[i].ssid + "\",";
      j += "\"rssi\":" + String(lastScan[i].rssi) + ",";
      j += "\"enc\":" + String((int)lastScan[i].enc) + "}";
    }
    j += "]";
    setNoCacheHeaders();
    httpServer.send(200, "application/json", j);
  });

  httpServer.on("/api/adc", HTTP_GET, []() {
    if (logFlags.http) console.printf("[HTTP] GET /api/adc (vBatt=%.2f, %d%%)\n", vBatt_float_last, batteryPercent);
    char j[128];
    snprintf(j, sizeof(j), "{\"vAdc\":%.3f,\"vBatt\":%.2f,\"percent\":%d}",
      vAdc_last, vBatt_float_last, batteryPercent);
    setNoCacheHeaders();
    httpServer.send(200, "application/json", j);
  });

  httpServer.on("/api/restart-charge", HTTP_POST, []() {
    if (logFlags.http) console.println("[HTTP] POST /api/restart-charge");
    pinMode(CHARGE_RESTART_PIN, OUTPUT);
    digitalWrite(CHARGE_RESTART_PIN, HIGH);
    delay(100);
    digitalWrite(CHARGE_RESTART_PIN, LOW);
    console.println("[HTTP] GPIO47 pulsed HIGH->LOW (restart charging)");
    httpServer.send(200, "application/json", "{\"ok\":true}");
  });

  httpServer.on("/api/reboot", HTTP_POST, []() {
    console.println("[HTTP] POST /api/reboot — restarting...");
    httpServer.send(200, "application/json", "{\"ok\":true}");
    delay(200);
    ESP.restart();
  });

  // ---- Settings API ----
  httpServer.on("/api/settings", HTTP_GET, []() {
    if (logFlags.http) console.println("[HTTP] GET /api/settings");
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
    j += "\"alwaysStartPanel\":" + String(settings.alwaysStartPanel ? "true" : "false") + ",";
    j += "\"vBatt\":" + String(vBatt_float_last, 2) + ",";
    j += "\"version\":\"" + String(FOTA_CURRENT_VERSION) + "\"";
    j += "}";
    setNoCacheHeaders();
    httpServer.send(200, "application/json", j);
  });

  httpServer.on("/api/settings", HTTP_POST, []() {
    if (logFlags.http) console.printf("[HTTP] POST /api/settings (%d args)\n", httpServer.args());
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
    if (httpServer.hasArg("alwaysStartPanel"))
      settings.alwaysStartPanel = (httpServer.arg("alwaysStartPanel") == "1");
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
    if (logFlags.http) console.println("[HTTP] Settings saved");
    httpServer.send(200, "application/json", "{\"ok\":true}");
  });

  // Default: redirect unknown paths to root
  httpServer.onNotFound(portalRedirect);

  routesRegistered = true;
}

static void ensureHttpServerStarted() {
  if (httpStarted) return;
  httpServer.begin();
  httpStarted = true;
}

void rc_start_portal() {
  startConfigPortal = true;
  captivePortalMode = true;

  String apSsid = String(settings.apPrefix) + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF), HEX);
  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);
  WiFi.setSleep(true);
  WiFi.softAP(apSsid.c_str());
  IPAddress ip = WiFi.softAPIP();

  console.printf("[WiFi] Config AP started: SSID=%s IP=%s\n", apSsid.c_str(), ip.toString().c_str());

  rc_wifi_scan();

  dnsServer.start(53, "*", ip);
  dnsActive = true;
  ensureHttpRoutesRegistered();
  ensureHttpServerStarted();
  startMdnsService();
}

bool rc_start_panel_sta() {
  if (WiFi.status() != WL_CONNECTED) {
    console.println("[WiFi] STA panel start failed: no active WiFi connection");
    return false;
  }

  startConfigPortal = true;
  captivePortalMode = false;

  WiFi.persistent(false);
  WiFi.setSleep(true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);

  if (dnsActive) {
    dnsServer.stop();
    dnsActive = false;
  }

  rc_wifi_scan();

  ensureHttpRoutesRegistered();
  ensureHttpServerStarted();
  startMdnsService();

  console.printf("[WiFi] Config panel started on STA IP=%s\n", WiFi.localIP().toString().c_str());
  return true;
}

void rc_portal_loop() {
  if (startConfigPortal) {
    if (dnsActive) dnsServer.processNextRequest();
    httpServer.handleClient();
  }
}
