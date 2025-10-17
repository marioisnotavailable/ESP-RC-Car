#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <LittleFS.h>
#include "driver/gpio.h"

// ----------------- WLAN / AP -----------------
#define WIFI_SSID "HTL-WLAN-IoT"
#define WIFI_PASS "HTL2IoT!"
const uint8_t WIFI_CH = 6;

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
WebServer http(80);
WebSocketsServer ws(81);

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

  // WLAN AP
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_CH);

  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  IPAddress ip = WiFi.localIP();
  Serial.print("AP up. SSID="); Serial.print(WIFI_SSID);
  Serial.print("  IP="); Serial.println(ip); // -> http://192.168.4.1/

  // LittleFS
  if (!LittleFS.begin()) {
    Serial.println("[FS] LittleFS mount failed");
  } else {
    listFS();
  }

  // Root -> controlle.html
  http.on("/", []() {
    if (!LittleFS.exists("/controlle.html")) {
      http.send(500, "text/plain", "controlle.html missing in LittleFS");
      return;
    }
    File f = LittleFS.open("/controlle.html", "r");
    http.streamFile(f, "text/html");
    f.close();
  });

  // Static Files
  http.onNotFound([](){
    String path = http.uri();
    File f = LittleFS.open(path, "r");
    if (!f) {
      http.send(404, "text/plain", "Not found");
      return;
    }
    String ct = "text/plain";
    if (path.endsWith(".html")) ct = "text/html";
    else if (path.endsWith(".css")) ct = "text/css";
    else if (path.endsWith(".js")) ct = "application/javascript";
    else if (path.endsWith(".png")) ct = "image/png";
    else if (path.endsWith(".jpg") || path.endsWith(".jpeg")) ct = "image/jpeg";
    http.streamFile(f, ct);
    f.close();
  });

  http.begin();

  // WebSocket
  ws.begin();
  ws.onEvent(onWs);

  // Servo initialisieren: LEDC → Fallback
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
  http.handleClient();
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
