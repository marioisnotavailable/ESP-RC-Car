#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WebSocketsServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <vector>
#include "driver/gpio.h"
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <WiFiUdp.h>

// ========== BATTERIEMONITORING ==========
// ── Pins & Hardware ──────────────────────────────────────────
#define ADC_UB_CHANNEL  ADC1_CHANNEL_0  // GPIO1 = ADC1_CH0

// ── Spannungsteiler ──────────────────────────────────────────
#define R1              1800000.0f
#define R2              1000000.0f
#define DIVIDER_RATIO   ((R1 + R2) / R2)
// ── Batterie-Kalibrierung (Prozent) ───────────────────────
#define BATT_MAX_VOLTAGE  8.39f  // 100%
#define BATT_MIN_VOLTAGE  7.5f   // 0%
#define BATT_VOLTAGE_RANGE (BATT_MAX_VOLTAGE - BATT_MIN_VOLTAGE)
// ── Batterie-Schwellen ────────────────────────────────────────
#define POWER_WARN_MODE 7.9f
#define POWER_OFF_MODE  7.4f

// ── Sampling ─────────────────────────────────────────────────
#define SAMPLE_INTERVAL_MS  2
#define SAMPLE_COUNT        500
#define RESULT_INTERVAL_MS  1000
#define LOW_CONFIRM_COUNT   3

// ── Battery Globals ──────────────────────────────────────────
volatile int batteryPercent = 0;  // Batterieprozentage (0-100%)
static float vBatt_float_last = 0.0f;  // Letzte Rohspannung für Logging
static float vAdc_last = 0.0f;  // Letzte kalibrierte ADC-Spannung für WebSocket
static int   sampleCount_last = 0;  // Letzte Sample-Anzahl für WebSocket

static esp_adc_cal_characteristics_t adc_chars;
static bool cali_ok        = false;
static int  lowVoltageHits = 0;
static unsigned long lastEval = 0;
static uint32_t      mvAccum  = 0;
static int           count    = 0;

void adc_cali_init()
{
    esp_adc_cal_value_t cal_type = esp_adc_cal_characterize(
        ADC_UNIT_1,
        ADC_ATTEN_DB_11,
        ADC_WIDTH_BIT_12,
        1100,
        &adc_chars
    );

    if (cal_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        Serial.println("[CAL] eFuse Vref Kalibrierung OK");
        cali_ok = true;
    } else if (cal_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        Serial.println("[CAL] eFuse Two Point Kalibrierung OK");
        cali_ok = true;
    } else {
        Serial.println("[CAL] Keine eFuse – Default Vref 1100mV");
        cali_ok = true;
    }
}

void batterie_setup()
{
    Serial.println("[BOOT] Batteriemonitor gestartet");

    gpio_config_t io_conf = {};
    io_conf.intr_type     = GPIO_INTR_DISABLE;
    io_conf.mode          = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask  = (1ULL << 1);
    io_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en    = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_UB_CHANNEL, ADC_ATTEN_DB_11);
    adc_cali_init();

    lastEval = millis();
}

void batterie_loop()
{
    int raw = adc1_get_raw(ADC_UB_CHANNEL);
    if (raw >= 0) {
        mvAccum += esp_adc_cal_raw_to_voltage(raw, &adc_chars);
        count++;
    }

    if (millis() - lastEval < RESULT_INTERVAL_MS) return;
    lastEval = millis();

    if (count == 0) {
        Serial.println("[ERR] Keine Samples");
        return;
    }

    float vAdc  = (float)(mvAccum / count) / 1000.0f;
    float vBatt_float = vAdc * DIVIDER_RATIO;

    // Berechne Batterieprozentage (8.39V = 100%, 7.5V = 0%)
    float percent = ((vBatt_float - BATT_MIN_VOLTAGE) / BATT_VOLTAGE_RANGE) * 100.0f;
    percent = constrain(percent, 0.0f, 100.0f);
    batteryPercent = (int)percent;
    vBatt_float_last = vBatt_float;  // Für Logging speichern
    vAdc_last = vAdc;  // Speichern für WebSocket-Übertragung
    sampleCount_last = count;  // Speichern für WebSocket-Übertragung

    Serial.printf("[ADC] V_ADC: %.3fV (%s) | V_Batt: %.2fV (vBatt=%d) | Samples: %d\n",
                  vAdc, cali_ok ? "kalibriert" : "unkalibriert", vBatt_float, batteryPercent, count);

    mvAccum = 0;
    count   = 0;

    if (vBatt_float <= POWER_OFF_MODE) {
        lowVoltageHits++;
        Serial.printf("[WARN] Unterspannung %d/%d\n", lowVoltageHits, LOW_CONFIRM_COUNT);
    } else {
        lowVoltageHits = 0;
        if (vBatt_float <= POWER_WARN_MODE) {
            Serial.println("[WARN] Batterie niedrig!");
        }
    }

    if (lowVoltageHits >= LOW_CONFIRM_COUNT) {
        Serial.println("[SLEEP] Batterie zu niedrig – Deep Sleep");
        esp_sleep_enable_timer_wakeup(4000000ULL);
        esp_deep_sleep_start();
    }
}

// ========== END BATTERIEMONITORING ==========

// ----------------- Konfiguration / Konstanten -----------------
#define HTTP_PORT                 80
#define WS_PORT                   81
#define WIFI_CONNECT_TOTAL_MS     15000UL  // Gesamtdauer für Verbindungsversuch
#define WIFI_CONNECT_POLL_MS      250      // Poll-Intervall
#define BEACON_INTERVAL_MS        2000UL   // UDP-Discovery-Beacon
#define SCAN_DWELL_MS             40       // passives Scannen pro Kanal
#define AP_SSID_PREFIX            "ESP-RC-Car-Setup-"

// ----------------- WLAN / AP -----------------

// ----------------- Pins / LED ----------------
#define LED_PIN          13

// ----------------- Servo (LEDC bevorzugt) ----
#define LENKUNG_PIN               5     // stabiler PWM-Pin
#define SERVO_CH                  6     // LEDC-Kanal 0..7
#define SERVO_FREQ                50    // 50 Hz
#define SERVO_RES_BITS            12    // <=14 bei 50 Hz

// Servo-Parametrisierung
#define SERVO_MIN_US              1000  // linker Anschlag
#define SERVO_MAX_US              2000  // rechter Anschlag
#define SERVO_MID_US              1500  // Mitte
#define SERVO_INVERT              0     // 0=false, 1=true (Richtung invertieren)
#define SERVO_GAIN                1.0f  // <1.0 weniger, >1.0 mehr Ausschlag

// Anti-Zappeln
#define SERVO_DEADZONE            30    // –1000..+1000
#define SERVO_SLEW_US_PER_LOOP    0     // 0 = aus (wir takten auf 50 Hz)
static float       steerFilt = 0.0f;         // EMA-Filterzustand
#define FILTER_ALPHA              0.85f // 0.8–0.9 = weich, 0.6 = direkter
static uint32_t    nextServoUpdateMs = 0;    // 20ms Scheduler

// Failsafe
#define FAILSAFE_MS               400UL // ohne WS → Mitte

// LED Blink (Gas)
#define BLINK_MIN_MS              50
#define BLINK_MAX_MS              800

// ----------------- Webserver / WS ------------
WebSocketsServer ws(WS_PORT);
WebServer http(HTTP_PORT);
WiFiMulti wifiMulti;
Preferences prefs;
DNSServer dnsServer; // for captive portal DNS redirect
WiFiUDP udp;         // UDP for discovery

// ---- UDP discovery ----
#define DISCOVERY_PORT        49352     // arbitrary app-specific
#define DISCOVERY_QUERY       "ESP_RC_DISCOVER"
#define DISCOVERY_RESP_PREFIX "ESP_RC_HERE "   // followed by ws URL
static uint32_t       nextBeaconAtMs = 0;

// Compute a best-effort broadcast address for the active interface (STA/AP)
static IPAddress calcBroadcastIP() {
  // Prefer STA broadcast when connected
  if ((WiFi.getMode() & WIFI_MODE_STA) && WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    IPAddress mask = WiFi.subnetMask();
    return IPAddress(
      (ip[0] & mask[0]) | ((~mask[0]) & 0xFF),
      (ip[1] & mask[1]) | ((~mask[1]) & 0xFF),
      (ip[2] & mask[2]) | ((~mask[2]) & 0xFF),
      (ip[3] & mask[3]) | ((~mask[3]) & 0xFF)
    );
  }
  // AP mode: ESP32 default AP uses /24; use x.y.z.255 as broadcast
  IPAddress ap = WiFi.softAPIP();
  return IPAddress(ap[0], ap[1], ap[2], 255);
}

IPAddress currentControlIP() {
  // Prefer STA IP if connected, otherwise AP IP (if portal)
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP();
  return WiFi.softAPIP();
}

void udpDiscoveryBegin() {
  udp.begin(DISCOVERY_PORT);
}

void udpDiscoveryHandle() {
  // Respond to queries
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    char buf[64];
    int n = udp.read(buf, sizeof(buf)-1);
    if (n < 0) n = 0; buf[n] = '\0';
    if (strcmp(buf, DISCOVERY_QUERY) == 0) {
      IPAddress ip = currentControlIP();
      char msg[96];
      // Response: "ESP_RC_HERE ws://<ip>:81/"
      snprintf(msg, sizeof(msg), "%sws://%s:81/", DISCOVERY_RESP_PREFIX, ip.toString().c_str());
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      udp.write((const uint8_t*)msg, strlen(msg));
      udp.endPacket();
    }
  }

  // Optional: periodic beacon (broadcast) every 2s to reduce discover latency
  uint32_t now = millis();
  if (now >= nextBeaconAtMs) {
    nextBeaconAtMs = now + BEACON_INTERVAL_MS;
    IPAddress ip = currentControlIP();
    char msg[96];
    snprintf(msg, sizeof(msg), "%sws://%s:81/", DISCOVERY_RESP_PREFIX, ip.toString().c_str());
    // Send to both limited broadcast and subnet-directed broadcast
    // Some stacks/routers drop one or the other; sending both improves reachability (notably iOS)
    IPAddress bcast = calcBroadcastIP();
    // Subnet-directed broadcast
    udp.beginPacket(bcast, DISCOVERY_PORT);
    udp.write((const uint8_t*)msg, strlen(msg));
    udp.endPacket();
    // Limited broadcast 255.255.255.255
    udp.beginPacket(IPAddress(255,255,255,255), DISCOVERY_PORT);
    udp.write((const uint8_t*)msg, strlen(msg));
    udp.endPacket();
  }
}

// ----------------- Multi-Reset Detector (3x, NVS) -------
// Detect three quick resets within a short window to open the Wi-Fi config portal
#define MRD_TIMEOUT_MS  8000UL  // window to count resets
#define MRD_REQUIRED    3       // number of resets required
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
  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true, /*passive=*/true, /*max_ms_per_chan=*/SCAN_DWELL_MS);
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
  if (path.endsWith(".ico"))  return "image/x-icon";
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

  String apSsid = String(AP_SSID_PREFIX) + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF), HEX);
  WiFi.mode(WIFI_AP_STA); // allow scanning while AP is active
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.softAP(apSsid.c_str());
  IPAddress ip = WiFi.softAPIP();

  Serial.printf("[WiFi] Config AP started: SSID=%s IP=%s\n", apSsid.c_str(), ip.toString().c_str());

  // Pre-scan once at startup (before clients are connected) to avoid on-demand scan disconnects
  runWiFiScan();

  // Start DNS captive portal: resolve all hostnames to the AP IP
  dnsServer.start(53, "*", ip);

  // Static files
  http.on("/", HTTP_GET, [](){ if (!serveFile("/index.html")) http.send(404, "text/plain", "index.html not found"); });
  http.on("/app.js", HTTP_GET, [](){ if (!serveFile("/app.js")) http.send(404, "text/plain", "app.js not found"); });
  http.on("/styles.css", HTTP_GET, [](){ if (!serveFile("/styles.css")) http.send(404, "text/plain", "styles.css not found"); });

  // Captive portal probes from various OS: redirect to portal root to force captive browser
  auto portalRedirect = [](){
    http.sendHeader("Cache-Control", "no-store");
    http.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
    http.send(302, "text/plain", "");
  };
  // Android (Google/Samsung variants)
  http.on("/generate_204", HTTP_GET, portalRedirect);   // connectivitycheck.gstatic.com / android.com / samsung.com
  http.on("/gen_204", HTTP_GET, portalRedirect);        // www.google.com/gen_204
  // iOS/macOS
  http.on("/hotspot-detect.html", HTTP_GET, portalRedirect); // captive.apple.com
  http.on("/library/test/success.html", HTTP_GET, portalRedirect); // legacy probe
  http.on("/canonical.html", HTTP_GET, portalRedirect);
  // Windows
  http.on("/ncsi.txt", HTTP_GET, portalRedirect);       // msftncsi.com
  http.on("/connecttest.txt", HTTP_GET, portalRedirect);
  http.on("/fwlink", HTTP_GET, portalRedirect);
  // Mozilla/Firefox captive portal detection
  http.on("/success.txt", HTTP_GET, portalRedirect);    // detectportal.firefox.com

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

  // Default: redirect any unknown path to the root to help with captive portal
  http.onNotFound([](){
    http.sendHeader("Cache-Control", "no-store");
    http.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
    http.send(302, "text/plain", "");
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

// ---- Batterie-Sendeintervall ----
static uint32_t nextBattSendMs = 0;
#define BATT_SEND_INTERVAL_MS 1000

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
    while (millis() - t0 < WIFI_CONNECT_TOTAL_MS) {
      if (wifiMulti.run() == WL_CONNECTED) { connected = true; break; }
      delay(WIFI_CONNECT_POLL_MS);
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

  // Start UDP discovery service
  udpDiscoveryBegin();

  // Initialize battery monitoring
  batterie_setup();

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
    // service DNS for captive portal
    dnsServer.processNextRequest();
    http.handleClient();
  }

  ws.loop();

  // UDP discovery
  udpDiscoveryHandle();

  // Battery monitoring
  batterie_loop();

  // Send battery percentage periodically to all connected clients
  uint32_t now = millis();
  if (now >= nextBattSendMs) {
    nextBattSendMs = now + BATT_SEND_INTERVAL_MS;
    // Format: "BATT:XX" where XX is battery percentage (0-100)
    char battMsg[16];
    snprintf(battMsg, sizeof(battMsg), "BATT:%d", batteryPercent);
    ws.broadcastTXT((uint8_t*)battMsg, strlen(battMsg));
  }
  if (millis() - lastCmdMs > FAILSAFE_MS) {
    lastCmd.steer = 0; // optional: später auch throttle failsafen
  }

  // LED-Blinken je nach Gas
  uint32_t nowLed = millis();
  uint16_t interval = throttleToInterval(lastCmd.throttle);

  if (lastCmd.throttle == 0) {
    ledState = false;
    digitalWrite(LED_PIN, LOW);
  } else if (nowLed - ledTimer >= interval) {
    ledTimer = nowLed;
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
