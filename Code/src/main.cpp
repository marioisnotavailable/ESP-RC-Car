#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <vector>
#include "driver/gpio.h"

// ----------------- WLAN / AP -----------------

// ----------------- Pins / LED ----------------
#define LED_PIN          13

// ----------------- Servo (LEDC bevorzugt) ----
#define LENKUNG_PIN      5     // stabiler PWM-Pin
#define SERVO_CH         6     // LEDC-Kanal 0..7
#define SERVO_FREQ       50    // 50 Hz
#define SERVO_RES_BITS   12    // <=14 bei 50 Hz

// Servo-Parametrisierung
static const int   SERVO_MIN_US   = 1000;   // linker Anschlag
static const int   SERVO_MAX_US   = 2000;   // rechter Anschlag
static const int   SERVO_MID_US   = 1500;   // Mitte
static const bool  SERVO_INVERT   = false;  // Richtung invertieren
static const float SERVO_GAIN     = 1.0f;   // <1.0 weniger, >1.0 mehr Ausschlag

// Anti-Zappeln
static const int   SERVO_DEADZONE = 30;     // –1000..+1000
static const int   SERVO_SLEW_US_PER_LOOP = 0; // 0 = aus (wir takten auf 50 Hz)
static float       steerFilt = 0.0f;        // EMA-Filterzustand
static const float FILTER_ALPHA = 0.85f;    // 0.8–0.9 = weich, 0.6 = direkter
static uint32_t    nextServoUpdateMs = 0;   // 20ms Scheduler

// Failsafe
static const uint32_t FAILSAFE_MS = 400;    // ohne WS → Mitte

// LED Blink (Gas)
const uint16_t BLINK_MIN_MS = 50;
const uint16_t BLINK_MAX_MS = 800;

// ----------------- Webserver / WS ------------
WebSocketsServer ws(81);
WebServer http(80);
WiFiMulti wifiMulti;
Preferences prefs;

// ----------------- Multi-Reset Detector (3x, NVS) -------
// Detect three quick resets within a short window to open the Wi-Fi config portal
static const uint32_t MRD_TIMEOUT_MS = 8000;   // window to count resets
static const uint8_t  MRD_REQUIRED  = 3;       // number of resets required
static uint32_t       mrdClearAtMs = 0;        // when to clear counter (relative to current boot)
static bool           mrdCleared = false;
static bool           startConfigPortal = false;
Preferences prefsMRD;                          // separate namespace for MRD

// ----------------- Saved WiFi storage --------------------
struct WifiNet { String ssid; String pass; };
static std::vector<WifiNet> savedNets;

void storageBegin() {
  prefs.begin("wifi", false);
}

void storageEnd() {
  prefs.end();
}

void loadSavedNetworks() {
  savedNets.clear();
  storageBegin();
  String raw = prefs.getString("nets", "");
  storageEnd();
  if (raw.length() == 0) return;
  // Format: one entry per line: ssid\tpass\n (tab separated)
  int pos = 0;
  while (pos < (int)raw.length()) {
    int nl = raw.indexOf('\n', pos);
    String line = (nl >= 0) ? raw.substring(pos, nl) : raw.substring(pos);
    if (nl < 0) pos = raw.length(); else pos = nl + 1;
    if (line.length() == 0) continue;
    int sep = line.indexOf('\t');
    if (sep <= 0) continue;
    WifiNet n{ line.substring(0, sep), line.substring(sep + 1) };
    if (n.ssid.length() > 0) savedNets.push_back(n);
  }
}

void saveSavedNetworks() {
  String out;
  for (auto &n : savedNets) {
    // Don't allow tabs/newlines in stored strings to keep format simple
    String s = n.ssid; s.replace('\t', ' '); s.replace('\n', ' ');
    String p = n.pass; p.replace('\t', ' '); p.replace('\n', ' ');
    out += s; out += '\t'; out += p; out += '\n';
  }
  storageBegin();
  prefs.putString("nets", out);
  storageEnd();
}

bool addNetwork(const String& ssid, const String& pass) {
  if (ssid.length() == 0) return false;
  // de-duplicate by ssid
  for (auto &n : savedNets) if (n.ssid == ssid) { n.pass = pass; saveSavedNetworks(); return true; }
  if (savedNets.size() >= 16) return false; // cap
  savedNets.push_back({ssid, pass});
  saveSavedNetworks();
  return true;
}

bool deleteNetworkByIndex(int idx) {
  if (idx < 0 || (size_t)idx >= savedNets.size()) return false;
  savedNets.erase(savedNets.begin() + idx);
  saveSavedNetworks();
  return true;
}

// ----------------- Network Scan (AP portal) ---------------
struct ScanNet { String ssid; int32_t rssi; uint8_t enc; };
static std::vector<ScanNet> lastScan;

void runWiFiScan() {
  lastScan.clear();
  // Ensure STA is enabled for scanning
  WiFi.mode(WIFI_AP_STA);
  // Use passive scan with short dwell to reduce AP disconnects
  // Signature: scanNetworks(async=false, show_hidden=false, passive=false, max_ms_per_chan=120)
  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true, /*passive=*/true, /*max_ms_per_chan=*/40);
  for (int i = 0; i < n; ++i) {
    ScanNet s{ WiFi.SSID(i), WiFi.RSSI(i), (uint8_t)WiFi.encryptionType(i) };
    lastScan.push_back(s);
  }
  WiFi.scanDelete();
  Serial.printf("[WiFi] Scan found %d network(s)\n", (int)lastScan.size());
}

// ----------------- Config Portal (AP + HTTP) -------------
// File serving helpers
String getContentType(const String &path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css"))  return "text/css";
  if (path.endsWith(".js"))   return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".jpg"))  return "image/jpeg";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  return "text/plain";
}

bool serveFile(const String &path) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  String ct = getContentType(path);
  http.streamFile(f, ct);
  f.close();
  return true;
}

void startAPPortal() {
  startConfigPortal = true;

  String apSsid = String("ESP-RC-Car-Setup-") + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF), HEX);
  WiFi.mode(WIFI_AP_STA); // allow scanning while AP is active
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.softAP(apSsid.c_str());
  IPAddress ip = WiFi.softAPIP();

  Serial.printf("[WiFi] Config AP started: SSID=%s IP=%s\n", apSsid.c_str(), ip.toString().c_str());

  // Pre-scan once at startup (before clients are connected) to avoid on-demand scan disconnects
  runWiFiScan();

  // Static files
  http.on("/", HTTP_GET, [](){ if (!serveFile("/index.html")) http.send(404, "text/plain", "index.html not found"); });
  http.on("/app.js", HTTP_GET, [](){ if (!serveFile("/app.js")) http.send(404, "text/plain", "app.js not found"); });
  http.on("/styles.css", HTTP_GET, [](){ if (!serveFile("/styles.css")) http.send(404, "text/plain", "styles.css not found"); });

  // JSON API
  http.on("/api/saved", HTTP_GET, [](){
    String j = "[";
    for (size_t i = 0; i < savedNets.size(); ++i) {
      if (i) j += ',';
      j += '{';
      j += "\"i\":" + String(i) + ",\"ssid\":\"" + savedNets[i].ssid + "\"";
      j += '}';
    }
    j += "]";
    http.send(200, "application/json", j);
  });
  http.on("/api/save", HTTP_POST, [](){
    String ssid = http.arg("ssid");
    String pass = http.arg("pass");
    bool ok = addNetwork(ssid, pass);
    http.send(200, "application/json", String("{\"ok\":") + (ok?"true":"false") + "}");
  });
  http.on("/api/delete", HTTP_POST, [](){
    int idx = http.arg("i").toInt();
    bool ok = deleteNetworkByIndex(idx);
    http.send(200, "application/json", String("{\"ok\":") + (ok?"true":"false") + "}");
  });
  http.on("/api/scan", HTTP_POST, [](){
    runWiFiScan();
    String j = "[";
    for (size_t i = 0; i < lastScan.size(); ++i) {
      if (i) j += ',';
      j += '{';
      j += "\"ssid\":\"" + lastScan[i].ssid + "\",";
      j += "\"rssi\":" + String(lastScan[i].rssi) + ",";
      j += "\"enc\":" + String((int)lastScan[i].enc);
      j += '}';
    }
    j += "]";
    http.send(200, "application/json", j);
  });
  http.on("/api/reboot", HTTP_POST, [](){
    http.send(200, "application/json", "{\"ok\":true}");
    delay(200);
    ESP.restart();
  });
  http.begin();
}

// ----------------- Steuerdaten ----------------
struct Cmd {
  int16_t throttle; // –1000..+1000
  int16_t steer;    // –1000..+1000
  uint8_t flags;
};
volatile Cmd lastCmd = {0,0,0};
static uint32_t lastCmdMs = 0;

// ----------------- LittleFS -------------------
void listFS() {
  Serial.println("[FS] Listing:");
  File root = LittleFS.open("/");
  if (!root) { Serial.println("  cannot open /"); return; }
  File f = root.openNextFile();
  while (f) {
    Serial.printf("  %s (%u bytes)\n", f.name(), (unsigned)f.size());
    f = root.openNextFile();
  }
}

// ----------------- Utils ----------------------
uint16_t throttleToInterval(int16_t thr) {
  int a = abs(thr);
  return BLINK_MAX_MS - (uint32_t)(BLINK_MAX_MS - BLINK_MIN_MS) * a / 1000;
}

// ----------------- Servo-Ausgabe -------------
static bool g_useTimerFallback = false;

// LEDC: µs → duty
inline uint32_t usToDuty(uint16_t us, uint8_t resBits) {
  const uint32_t maxDuty = (1u << resBits) - 1u;
  return (uint32_t)((uint64_t)us * maxDuty / 20000u); // 20 ms Periode
}

// Aktuelle Ziel-/Ist-Mikrosekunden
static int currentServoUs = SERVO_MID_US;

// ---- Timer-Fallback (nur wenn nötig) ----
hw_timer_t* servoTimer = nullptr;
volatile bool highPhase = false;
volatile uint32_t targetUs_timer = SERVO_MID_US;
volatile uint32_t currentUs_timer = SERVO_MID_US;

void IRAM_ATTR servoISR() {
  if (highPhase) {
    gpio_set_level((gpio_num_t)LENKUNG_PIN, 0);
    highPhase = false;
    uint32_t lowUs = 20000 - currentUs_timer;
    if (lowUs < 100) lowUs = 100;
    timerAlarmWrite(servoTimer, lowUs, false);
    timerAlarmEnable(servoTimer);
  } else {
    uint32_t us = targetUs_timer;
    if (us < 500)  us = 500;
    if (us > 2500) us = 2500;
    currentUs_timer = us;

    gpio_set_level((gpio_num_t)LENKUNG_PIN, 1);
    highPhase = true;

    timerAlarmWrite(servoTimer, currentUs_timer, false);
    timerAlarmEnable(servoTimer);
  }
}

void servoFallbackInit() {
  g_useTimerFallback = true;
  pinMode(LENKUNG_PIN, OUTPUT);
  digitalWrite(LENKUNG_PIN, LOW);

  if (servoTimer) { timerEnd(servoTimer); servoTimer = nullptr; }
  servoTimer = timerBegin(1, 80, true); // 1 MHz
  timerAttachInterrupt(servoTimer, &servoISR, true);
  highPhase = false;
  targetUs_timer = SERVO_MID_US;
  timerAlarmWrite(servoTimer, 1000, false);
  timerAlarmEnable(servoTimer);
  Serial.println("[SERVO] Fallback: HW-Timer aktiv");
}

bool servoLEDCInit() {
  ledcDetachPin(LENKUNG_PIN);
  bool ok = ledcSetup(SERVO_CH, SERVO_FREQ, SERVO_RES_BITS);
  if (!ok) {
    Serial.println("[SERVO] LEDC Setup FAIL");
    return false;
  }
  ledcAttachPin(LENKUNG_PIN, SERVO_CH);
  ledcWrite(SERVO_CH, usToDuty(SERVO_MID_US, SERVO_RES_BITS));
  Serial.println("[SERVO] LEDC aktiv");
  return true;
}

// gemeinsame Schreibfunktion (LEDC bevorzugt)
void servoWriteMicrosecondsUnified(int targetUs) {
  targetUs = constrain(targetUs, SERVO_MIN_US, SERVO_MAX_US);

  // Slew (aus, da 50-Hz-Takt genutzt wird)
  if (SERVO_SLEW_US_PER_LOOP > 0) {
    static int prevUs = SERVO_MID_US;
    int delta = targetUs - prevUs;
    if (delta > SERVO_SLEW_US_PER_LOOP) delta = SERVO_SLEW_US_PER_LOOP;
    else if (delta < -SERVO_SLEW_US_PER_LOOP) delta = -SERVO_SLEW_US_PER_LOOP;
    currentServoUs = prevUs + delta;
    prevUs = currentServoUs;
  } else {
    currentServoUs = targetUs;
  }

  if (!g_useTimerFallback) {
    ledcWrite(SERVO_CH, usToDuty((uint16_t)currentServoUs, SERVO_RES_BITS));
  } else {
    targetUs_timer = currentServoUs; // ISR übernimmt in der nächsten Periode
  }
}

// ---- Mapping & Filter (keine Ausgabe hier!) ----
void applySteering() {
  int s = lastCmd.steer;

  if (abs(s) < SERVO_DEADZONE) s = 0;
  if (SERVO_INVERT) s = -s;

  // EMA-Filter auf -1000..+1000
  steerFilt = FILTER_ALPHA * steerFilt + (1.0f - FILTER_ALPHA) * (float)s;

  // Map auf µs
  float halfSpan = 0.5f * (float)(SERVO_MAX_US - SERVO_MIN_US);
  int targetUs = SERVO_MID_US + (int)(steerFilt * SERVO_GAIN * (halfSpan / 1000.0f));
  currentServoUs = constrain(targetUs, SERVO_MIN_US, SERVO_MAX_US);
}

// ----------------- WebSocket ------------------
void onWs(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_CONNECTED) {
    IPAddress ip = ws.remoteIP(num);
    Serial.printf("[WS] Client %u connected from %s\n", num, ip.toString().c_str());
    return;
  }
  if (type == WStype_TEXT) {
    static char buf[64];
    size_t n = min(len, sizeof(buf)-1);
    memcpy(buf, payload, n);
    buf[n] = '\0';

    int thr=0, st=0, fl=0;
    int r = sscanf(buf, "%d,%d,%d", &thr, &st, &fl);
    if (r == 3) {
      lastCmd.throttle = (int16_t)constrain(thr, -1000, 1000);
      lastCmd.steer    = (int16_t)constrain(st,  -1000, 1000);
      lastCmd.flags    = (uint8_t)fl;
      lastCmdMs = millis(); // Failsafe reset
    } else {
      Serial.printf("[WS] Parse FAIL (r=%d) buf='%s'\n", r, buf);
    }
  }
}

// ----------------- LED Blink ------------------
static uint32_t ledTimer = 0;
static bool ledState = false;

// ----------------- Setup ----------------------
void setup(){
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  // ---- Multi Reset Detection (3x quick reset, NVS based) ----
  prefsMRD.begin("mrd", false);
  uint8_t cnt = prefsMRD.getUChar("cnt", 0);
  cnt++;
  prefsMRD.putUChar("cnt", cnt);
  Serial.printf("[MRD] boot count within window (NVS) = %u\n", (unsigned)cnt);
  mrdClearAtMs = millis() + MRD_TIMEOUT_MS;
  if (cnt >= MRD_REQUIRED) {
    startConfigPortal = true;
    prefsMRD.putUChar("cnt", 0);
    Serial.println("[MRD] threshold reached -> starting config portal");
  }
  prefsMRD.end();

  // Filesystem for later
  if (!LittleFS.begin()) {
    Serial.println("[FS] LittleFS mount failed");
  } else {
    listFS();
  }

  // Load saved WiFi networks
  loadSavedNetworks();

  // Decide: Config portal or connect as STA
  bool connected = false;
  if (!startConfigPortal && !savedNets.empty()) {
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    // Add networks to WiFiMulti
    for (auto &n : savedNets) {
      wifiMulti.addAP(n.ssid.c_str(), n.pass.c_str());
    }
    Serial.printf("[WiFi] Trying %u saved network(s) ...\n", (unsigned)savedNets.size());
    uint32_t t0 = millis();
    while (millis() - t0 < 15000) { // 15s overall
      if (wifiMulti.run() == WL_CONNECTED) { connected = true; break; }
      delay(250);
      Serial.print('.');
    }
    Serial.println();
  }

  if (connected) {
    IPAddress ip = WiFi.localIP();
    Serial.printf("[WiFi] Connected. IP=%s\n", ip.toString().c_str());
  } else {
    // Either requested by multi-reset or failed to connect -> open AP portal
    startAPPortal();
  }

  // Start WebSocket server for control in both STA and AP modes
  ws.begin();
  ws.onEvent(onWs);

  // Servo initialisieren (immer aktiv, auch in AP/Setup-Modus)
  if (!servoLEDCInit()) {
    servoFallbackInit();
  } else {
    g_useTimerFallback = false;
  }
  // Servo Mitte
  currentServoUs = SERVO_MID_US;
  servoWriteMicrosecondsUnified(SERVO_MID_US);

  lastCmdMs = millis();
  nextServoUpdateMs = millis(); // sofort erste Ausgabe erlauben
}

// ----------------- Loop -----------------------
void loop(){
  // Clear MRD counter in NVS after timeout if not yet cleared
  if (!mrdCleared && millis() >= mrdClearAtMs) {
    prefsMRD.begin("mrd", false);
    prefsMRD.putUChar("cnt", 0);
    prefsMRD.end();
    mrdCleared = true;
    Serial.println("[MRD] window expired; counter cleared (NVS)");
  }

  if (startConfigPortal) {
    http.handleClient();
  }

  ws.loop();

  // Failsafe: ohne Daten → Mitte
  if (millis() - lastCmdMs > FAILSAFE_MS) {
    lastCmd.steer = 0; // optional: später auch throttle failsafen
  }

  // LED-Blinken je nach Gas
  uint32_t now = millis();
  uint16_t interval = throttleToInterval(lastCmd.throttle);

  if (lastCmd.throttle == 0) {
    ledState = false;
    digitalWrite(LED_PIN, LOW);
  } else if (now - ledTimer >= interval) {
    ledTimer = now;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
  }

  // ---- Lenkung: filtern & mappen (keine Ausgabe) ----
  applySteering();

  // ---- Servo nur alle 20 ms aktualisieren (50 Hz) ----
  uint32_t nowMs = millis();
  if (nowMs >= nextServoUpdateMs) {
    nextServoUpdateMs = nowMs + 20;   // nächster Frame
    servoWriteMicrosecondsUnified(currentServoUs);
  }

  // Debug optional:
  // Serial.printf("thr=%d steer=%d filt=%.1f us=%d mode=%s\n",
  //   lastCmd.throttle, lastCmd.steer, steerFilt, currentServoUs,
  //   g_useTimerFallback ? "TIMER" : "LEDC");
}
