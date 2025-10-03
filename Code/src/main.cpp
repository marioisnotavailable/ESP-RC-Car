#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <LittleFS.h>

#define WIFI_SSID "RC_Car"
#define WIFI_PASS "12345678"
const uint8_t WIFI_CH = 6;
#define LED_PIN 13

WebServer http(80);
WebSocketsServer ws(81);

//Steuerung
struct Cmd {
  int16_t throttle;
  int16_t steer;
  uint8_t flags;
};
volatile Cmd lastCmd = {0,0,0};

// --- WebSocket Event ---
void onWs(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_CONNECTED) {
    IPAddress ip = ws.remoteIP(num);
    Serial.printf("[WS] Client %u connected from %s\n", num, ip.toString().c_str());
    return;
  }
  if (type == WStype_TEXT) {
    // Payload in String umwandeln
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
      Serial.printf("[WS] OK thr=%d steer=%d flags=%d\n", thr, st, fl);
    } else {
      Serial.printf("[WS] Parse FAIL (r=%d) buf='%s'\n", r, buf);
    }
  }
}

// --- LittleFS Inhalte auflisten ---
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


static uint32_t ledTimer = 0;
static bool ledState = false;


const uint16_t BLINK_MIN_MS = 50;    // bei Vollgas
const uint16_t BLINK_MAX_MS = 800;   // bei 0 Gas

// Gaswert in Blinkintervall umrechnen
uint16_t throttleToInterval(int16_t thr) {
  int a = abs(thr);
  return BLINK_MAX_MS - (uint32_t)(BLINK_MAX_MS - BLINK_MIN_MS) * a / 1000;
}


void setup(){
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  // WLAN als Access Point starten
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.softAP(WIFI_SSID, WIFI_PASS, WIFI_CH);

  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP up. SSID="); Serial.print(WIFI_SSID);
  Serial.print("  IP="); Serial.println(ip); // -> http://192.168.4.1/

  // LittleFS starten
  if (!LittleFS.begin()) {
    Serial.println("[FS] LittleFS mount failed");
  } else {
    listFS();
  }

  // Root liefert controlle.html
  http.on("/", []() {
    if (!LittleFS.exists("/controlle.html")) {
      http.send(500, "text/plain", "controlle.html missing in LittleFS");
      return;
    }
    File f = LittleFS.open("/controlle.html", "r");
    http.streamFile(f, "text/html");
    f.close();
  });

  // falls die datei nicht gefunden wird
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

  // WebSocket starten
  ws.begin();
  ws.onEvent(onWs);
}

void loop(){
  http.handleClient();
  ws.loop();

  uint32_t now = millis();
  uint16_t interval = throttleToInterval(lastCmd.throttle);

    // Bei exakt 0 Gas LED aus, sonst blinkt es je nach geschw.
  if (lastCmd.throttle == 0) {
    ledState = false;
    digitalWrite(LED_PIN, LOW);
  } else if (now - ledTimer >= interval) {
    ledTimer = now;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
  }

  // Debug-Ausgabe
  Serial.printf("thr=%d steer=%d flags=%u\n", 
                lastCmd.throttle, lastCmd.steer, lastCmd.flags);


}
