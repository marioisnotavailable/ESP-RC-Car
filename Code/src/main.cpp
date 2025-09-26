#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#define WIFI_SSID "RC_Car"
#define WIFI_PASS "12345678"
const uint8_t WIFI_CH = 6;
#define LED_PIN 11

WebServer http(80);
WebSocketsServer ws(81);

void onWs(uint8_t , WStype_t , uint8_t* , size_t );

struct Cmd { int16_t throttle; int16_t steer; uint8_t flags; };
volatile Cmd lastCmd = {0,0,0};

//const int LED_PIN = 4; // Debug-LED an GND über 220–330 Ω

// Einfache Controller-Webseite (Touch-Pad) aus PROGMEM
const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><meta name=viewport content="width=device-width,initial-scale=1">
<title>RC Controller</title><style>
body{margin:0;background:#000;color:#fff;font-family:sans-serif;text-align:center}
#pad{margin:20px auto;width:min(90vw,90vh);height:min(90vw,90vh);background:#222;border-radius:50%;touch-action:none;position:relative}
#dot{position:absolute;width:22px;height:22px;margin:-11px 0 0 -11px;border-radius:50%;background:#0af;left:50%;top:50%}
</style><h2>RC Controller</h2><div id=pad><div id=dot></div></div>
<script>
let ws=new WebSocket("ws://192.168.4.1:81/");
const pad=document.getElementById('pad'),dot=document.getElementById('dot');
function send(thr,steer,flags){ if(ws.readyState===1) ws.send(`${thr},${steer},${flags}`); }
function norm(e){
  const r=pad.getBoundingClientRect(),t=(e.touches?e.touches[0]:e);
  let x=(t.clientX-(r.left+r.width/2))/(r.width/2);
  let y=((r.top+r.height/2)-t.clientY)/(r.height/2); // oben = Gas
  x=Math.max(-1,Math.min(1,x)); y=Math.max(-1,Math.min(1,y));
  dot.style.left=(50+x*50)+'%'; dot.style.top=(50-y*50)+'%';
  send(Math.round(y*1000), Math.round(x*1000), 0);
}
['touchstart','touchmove','mousedown','mousemove'].forEach(ev=>pad.addEventListener(ev,e=>{e.preventDefault();norm(e);},{passive:false}));
['touchend','mouseup','mouseleave'].forEach(ev=>pad.addEventListener(ev,e=>{e.preventDefault();send(0,0,0);dot.style.left='50%';dot.style.top='50%';}));
</script>
)HTML";

void onWs(uint8_t num, WStype_t type, uint8_t* payload, size_t len){
  if(type==WStype_TEXT){
    int thr=0, st=0, fl=0;
    sscanf((char*)payload, "%d,%d,%d", &thr, &st, &fl);
    lastCmd.throttle = (int16_t)constrain(thr,-1000,1000);
    lastCmd.steer    = (int16_t)constrain(st, -1000,1000);
    lastCmd.flags    = (uint8_t)fl;
  }
}

void setup(){
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_AP);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.softAP(WIFI_SSID, WIFI_PASS, WIFI_CH);

  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP up. SSID="); Serial.print(WIFI_SSID);
  Serial.print("  IP="); Serial.println(ip); // -> http://192.168.4.1/

  // HTTP: serve the page
  http.on("/", [](){ http.send_P(200, "text/html", INDEX_HTML); });
  http.begin();

  // WebSocket: empfängt Steuerwerte
  ws.begin();
  ws.onEvent(onWs);
}

void loop(){
  //Serial.print("  IP=");
  //digitalWrite(LED_PIN, HIGH); //delay(500);
  //Serial.print("  IP=");
  //digitalWrite(4, LOW);  delay(500);
  http.handleClient();
  ws.loop();

  // LED-Debug: Helligkeit ~ |throttle|
  // int duty = map(abs(lastCmd.throttle), 0, 1000, 0, 255);
  // analogWrite(LED_PIN, duty); // bei S3: nutzt LEDC intern
  // Optional: seriell schauen
   Serial.printf("thr=%d steer=%d flags=%u\n", lastCmd.throttle, lastCmd.steer, lastCmd.flags);
}


void onWs(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_CONNECTED) {
    IPAddress ip = ws.remoteIP(num);
    Serial.printf("[WS] Client %u connected from %s\n", num, ip.toString().c_str());
    return;
  }
  if (type == WStype_TEXT) {
    // payload ist NICHT 0-terminiert -> erst in Buffer kopieren
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

