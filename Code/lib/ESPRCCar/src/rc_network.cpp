#include "rc_network.h"
#include "rc_settings.h"
#include "rc_httpapi.h"
#include "rc_serial.h"

std::vector<WifiNet> savedNets;
std::vector<ScanNet> lastScan;
bool startConfigPortal = false;

static Preferences prefs;
static Preferences prefsMRD;
static WiFiUDP     udp;

static uint32_t mrdClearAtMs  = 0;
static bool     mrdCleared    = false;
static uint32_t nextBeaconMs  = 0;

// ---- BSSID helpers ----
static bool hasCachedBssid(const WifiNet& n) {
  for (int i = 0; i < 6; i++) if (n.bssid[i] != 0) return true;
  return false;
}

// ---- Broadcast address ----
static IPAddress calcBroadcastIP() {
  if ((WiFi.getMode() & WIFI_MODE_STA) && WiFi.status() == WL_CONNECTED) {
    IPAddress ip   = WiFi.localIP();
    IPAddress mask = WiFi.subnetMask();
    return IPAddress(
      (ip[0] & mask[0]) | ((~mask[0]) & 0xFF),
      (ip[1] & mask[1]) | ((~mask[1]) & 0xFF),
      (ip[2] & mask[2]) | ((~mask[2]) & 0xFF),
      (ip[3] & mask[3]) | ((~mask[3]) & 0xFF));
  }
  IPAddress ap = WiFi.softAPIP();
  return IPAddress(ap[0], ap[1], ap[2], 255);
}

IPAddress rc_current_ip() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP();
  return WiFi.softAPIP();
}

// ---- NVS storage ----
void rc_network_load() {
  savedNets.clear();
  prefs.begin("wifi", false);
  String raw = prefs.getString("nets", "");
  prefs.end();

  if (logFlags.net)
    Serial.printf("[NET] NVS geladen, raw len=%d\n", (int)raw.length());

  if (raw.length() == 0) return;

  int pos = 0;
  while (pos < (int)raw.length()) {
    int nl = raw.indexOf('\n', pos);
    String line = (nl >= 0) ? raw.substring(pos, nl) : raw.substring(pos);
    if (nl < 0) pos = raw.length(); else pos = nl + 1;
    if (line.length() == 0) continue;
    int sep = line.indexOf('\t');
    if (sep <= 0) continue;

    WifiNet n;
    n.ssid = line.substring(0, sep);
    String rest = line.substring(sep + 1);
    memset(n.bssid, 0, 6);
    n.channel = 0;

    int sep2 = rest.indexOf('\t');
    if (sep2 >= 0) {
      n.pass = rest.substring(0, sep2);
      String meta = rest.substring(sep2 + 1);
      int sep3 = meta.indexOf('\t');
      if (sep3 >= 0) {
        String bStr = meta.substring(0, sep3);
        if (bStr.length() == 17) {
          for (int b = 0; b < 6; b++)
            n.bssid[b] = (uint8_t)strtol(bStr.c_str() + b * 3, NULL, 16);
        }
        n.channel = meta.substring(sep3 + 1).toInt();
      }
    } else {
      n.pass = rest;
    }
    if (n.ssid.length() > 0) savedNets.push_back(n);
  }

  if (logFlags.net)
    Serial.printf("[NET] %d Netzwerk(e) geladen\n", (int)savedNets.size());
}

void rc_network_save() {
  String out;
  for (auto& n : savedNets) {
    String s = n.ssid; s.replace('\t', ' '); s.replace('\n', ' ');
    String p = n.pass; p.replace('\t', ' '); p.replace('\n', ' ');
    out += s; out += '\t'; out += p;
    if (hasCachedBssid(n)) {
      char bStr[18];
      snprintf(bStr, sizeof(bStr), "%02X:%02X:%02X:%02X:%02X:%02X",
        n.bssid[0], n.bssid[1], n.bssid[2], n.bssid[3], n.bssid[4], n.bssid[5]);
      out += '\t'; out += bStr; out += '\t'; out += String(n.channel);
    }
    out += '\n';
  }
  prefs.begin("wifi", false);
  prefs.putString("nets", out);
  prefs.end();
  if (logFlags.net)
    Serial.printf("[NET] %d Netzwerk(e) gespeichert (%d bytes)\n", (int)savedNets.size(), (int)out.length());
}

bool rc_network_add(const String& ssid, const String& pass) {
  if (ssid.length() == 0) return false;
  for (auto& n : savedNets)
    if (n.ssid == ssid) {
      n.pass = pass;
      rc_network_save();
      if (logFlags.net) Serial.printf("[NET] Netzwerk aktualisiert: \"%s\"\n", ssid.c_str());
      return true;
    }
  if (savedNets.size() >= 16) {
    if (logFlags.net) Serial.println("[NET] Max 16 Netzwerke erreicht");
    return false;
  }
  WifiNet nw;
  nw.ssid = ssid; nw.pass = pass;
  memset(nw.bssid, 0, 6); nw.channel = 0;
  savedNets.push_back(nw);
  rc_network_save();
  if (logFlags.net) Serial.printf("[NET] Netzwerk hinzugefuegt: \"%s\" (#%d)\n", ssid.c_str(), (int)savedNets.size());
  return true;
}

bool rc_network_delete(int idx) {
  if (idx < 0 || (size_t)idx >= savedNets.size()) return false;
  if (logFlags.net) Serial.printf("[NET] Netzwerk geloescht: \"%s\" (idx=%d)\n", savedNets[idx].ssid.c_str(), idx);
  savedNets.erase(savedNets.begin() + idx);
  rc_network_save();
  return true;
}

// ---- WiFi scan ----
void rc_wifi_scan() {
  lastScan.clear();
  if (logFlags.net) Serial.println("[NET] WiFi-Scan gestartet...");
  WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks(false, true, true, SCAN_DWELL_MS);
  for (int i = 0; i < n; ++i) {
    ScanNet s{WiFi.SSID(i), WiFi.RSSI(i), (uint8_t)WiFi.encryptionType(i)};
    lastScan.push_back(s);
  }
  WiFi.scanDelete();
  Serial.printf("[NET] Scan: %d Netzwerk(e) gefunden\n", (int)lastScan.size());
}

// ---- WiFi connect ----
bool rc_wifi_connect() {
  if (startConfigPortal || savedNets.empty()) {
    if (logFlags.net)
      Serial.printf("[NET] WiFi connect uebersprungen (portal=%d, nets=%d)\n",
        startConfigPortal, (int)savedNets.size());
    return false;
  }

  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  rc_apply_wifi_tx_power();

  if (logFlags.net)
    Serial.printf("[NET] WiFi STA Modus, TX Power Level=%d, %d Netzwerk(e) gespeichert\n",
      settings.wifiTxPower, (int)savedNets.size());

  auto tryConnect = [&](size_t i, uint32_t timeoutMs, bool useCached) -> bool {
    WiFi.disconnect(true);
    delay(100);
    if (useCached && hasCachedBssid(savedNets[i])) {
      Serial.printf("[NET] [%u/%u] Fast connect \"%s\" (ch=%d) ...\n",
        (unsigned)(i+1), (unsigned)savedNets.size(),
        savedNets[i].ssid.c_str(), (int)savedNets[i].channel);
      WiFi.begin(savedNets[i].ssid.c_str(), savedNets[i].pass.c_str(),
        savedNets[i].channel, savedNets[i].bssid);
    } else {
      Serial.printf("[NET] [%u/%u] Trying \"%s\" (pass len=%u) ...\n",
        (unsigned)(i+1), (unsigned)savedNets.size(),
        savedNets[i].ssid.c_str(), (unsigned)savedNets[i].pass.length());
      WiFi.begin(savedNets[i].ssid.c_str(), savedNets[i].pass.c_str());
    }
    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
      wl_status_t status = WiFi.status();
      if (status == WL_CONNECTED) {
        Serial.printf("[NET] Connected to \"%s\" (RSSI: %d, ch=%d) after %lums\n",
          WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.channel(), millis() - t0);
        return true;
      }
      if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
        const char* reason = (status == WL_CONNECT_FAILED)
          ? "FAILED - wrong password or auth rejected"
          : "NO_SSID - network not found (out of range or 5GHz only)";
        Serial.printf("[NET] \"%s\": %s | %lums\n", savedNets[i].ssid.c_str(), reason, millis() - t0);
        return false;
      }
      if (logFlags.net && ((millis() - t0) % 1000 < WIFI_CONNECT_POLL_MS))
        Serial.printf("[NET] \"%s\": waiting... status=%d %lums\n",
          savedNets[i].ssid.c_str(), (int)status, millis() - t0);
      delay(WIFI_CONNECT_POLL_MS);
    }
    Serial.printf("[NET] \"%s\": timeout after %lums\n", savedNets[i].ssid.c_str(), timeoutMs);
    return false;
  };

  bool connected = false;

  // Phase 1: Fast connect with cached BSSID/channel
  if (logFlags.net) Serial.println("[NET] Phase 1: Fast connect (cached BSSID)...");
  for (size_t i = 0; i < savedNets.size() && !connected; ++i) {
    if (hasCachedBssid(savedNets[i]))
      connected = tryConnect(i, 5000, true);
  }

  // Phase 2: Normal connect
  if (!connected) {
    Serial.println("[NET] Phase 2: Normal scan connect...");
    uint32_t perNet = WIFI_CONNECT_TOTAL_MS / savedNets.size();
    if (logFlags.net)
      Serial.printf("[NET] %lums pro Netzwerk\n", (unsigned long)perNet);
    for (size_t i = 0; i < savedNets.size() && !connected; ++i) {
      connected = tryConnect(i, perNet, false);
    }
  }

  // Cache BSSID/channel on success
  if (connected) {
    String connSsid = WiFi.SSID();
    uint8_t* bssid  = WiFi.BSSID();
    int32_t ch      = WiFi.channel();
    for (auto& n : savedNets) {
      if (n.ssid == connSsid) {
        memcpy(n.bssid, bssid, 6);
        n.channel = ch;
        rc_network_save();
        if (logFlags.net)
          Serial.printf("[NET] Cached BSSID=%02X:%02X:%02X:%02X:%02X:%02X ch=%d for \"%s\"\n",
            bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], ch, connSsid.c_str());
        break;
      }
    }
  }

  if (!connected) Serial.println("[NET] Alle Netzwerke fehlgeschlagen");
  return connected;
}

// ---- Full WiFi setup ----
bool rc_wifi_setup() {
  bool connected = rc_wifi_connect();
  if (connected) {
    Serial.printf("[NET] Verbunden. IP=%s RSSI=%d Gateway=%s\n",
      WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.gatewayIP().toString().c_str());
  } else {
    Serial.println("[NET] Starte Config-Portal...");
    rc_start_portal();
  }
  return connected;
}

// ---- Multi-Reset Detection ----
void rc_mrd_check_boot() {
  prefsMRD.begin("mrd", false);
  uint8_t cnt = prefsMRD.getUChar("cnt", 0);
  cnt++;
  prefsMRD.putUChar("cnt", cnt);
  prefsMRD.end();
  Serial.printf("[MRD] boot count = %u / %u\n", (unsigned)cnt, (unsigned)MRD_REQUIRED);
  mrdClearAtMs = millis() + MRD_TIMEOUT_MS;
  if (cnt >= MRD_REQUIRED) {
    startConfigPortal = true;
    prefsMRD.begin("mrd", false);
    prefsMRD.putUChar("cnt", 0);
    prefsMRD.end();
    Serial.println("[MRD] threshold reached -> starting config portal");
  }
}

void rc_mrd_loop() {
  if (!mrdCleared && millis() >= mrdClearAtMs) {
    prefsMRD.begin("mrd", false);
    prefsMRD.putUChar("cnt", 0);
    prefsMRD.end();
    mrdCleared = true;
    if (logFlags.net) Serial.println("[MRD] window expired; counter cleared (NVS)");
  }
}

// ---- UDP Discovery ----
void rc_udp_begin() {
  udp.begin(DISCOVERY_PORT);
  if (logFlags.net)
    Serial.printf("[NET] UDP Discovery auf Port %d gestartet\n", DISCOVERY_PORT);
}

void rc_udp_loop() {
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    char buf[64];
    int n = udp.read(buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
    buf[n] = '\0';
    if (strcmp(buf, DISCOVERY_QUERY) == 0) {
      IPAddress ip = rc_current_ip();
      char msg[96];
      snprintf(msg, sizeof(msg), "%sws://%s:81/", DISCOVERY_RESP_PREFIX, ip.toString().c_str());
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      udp.write((const uint8_t*)msg, strlen(msg));
      udp.endPacket();
      if (logFlags.net)
        Serial.printf("[NET] Discovery Query von %s:%d -> %s\n",
          udp.remoteIP().toString().c_str(), udp.remotePort(), msg);
    } else {
      if (logFlags.net)
        Serial.printf("[NET] UDP unbekannt (%d bytes): '%s'\n", packetSize, buf);
    }
  }

  uint32_t now = millis();
  if (now >= nextBeaconMs) {
    nextBeaconMs = now + settings.beaconIntervalMs;
    IPAddress ip = rc_current_ip();
    char msg[96];
    snprintf(msg, sizeof(msg), "%sws://%s:81/", DISCOVERY_RESP_PREFIX, ip.toString().c_str());
    IPAddress bcast = calcBroadcastIP();
    udp.beginPacket(bcast, DISCOVERY_PORT);
    udp.write((const uint8_t*)msg, strlen(msg));
    udp.endPacket();
    udp.beginPacket(IPAddress(255, 255, 255, 255), DISCOVERY_PORT);
    udp.write((const uint8_t*)msg, strlen(msg));
    udp.endPacket();
    if (logFlags.net)
      Serial.printf("[NET] Beacon -> %s + 255.255.255.255\n", bcast.toString().c_str());
  }
}
