#include "rc_websocket.h"
#include "rc_settings.h"
#include "rc_battery.h"

#define WS_PORT            81
#define BATT_SEND_INTERVAL_MS 1000

WebSocketsServer ws(WS_PORT);
volatile Cmd lastCmd = {0, 0, 0};
uint32_t lastCmdMs   = 0;

static uint32_t nextBattSendMs = 0;

static void onWs(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_CONNECTED) {
    IPAddress ip = ws.remoteIP(num);
    Serial.printf("[WS] Client %u connected from %s\n", num, ip.toString().c_str());
    return;
  }
  if (type == WStype_TEXT) {
    static char buf[64];
    size_t n = min(len, sizeof(buf) - 1);
    memcpy(buf, payload, n);
    buf[n] = '\0';

    int thr = 0, st = 0, fl = 0;
    int r = sscanf(buf, "%d,%d,%d", &thr, &st, &fl);
    if (r == 3) {
      int16_t maxThr = (int16_t)(1000L * settings.maxThrottlePct / 100);
      lastCmd.throttle = (int16_t)constrain(thr, -maxThr, maxThr);
      lastCmd.steer    = (int16_t)constrain(st, -1000, 1000);
      lastCmd.flags    = (uint8_t)fl;
      lastCmdMs = millis();
    } else {
      Serial.printf("[WS] Parse FAIL (r=%d) buf='%s'\n", r, buf);
    }
  }
}

void rc_websocket_begin() {
  ws.begin();
  ws.onEvent(onWs);
}

void rc_websocket_loop() {
  ws.loop();
}

void rc_websocket_broadcast_batt() {
  uint32_t now = millis();
  if (now >= nextBattSendMs) {
    nextBattSendMs = now + BATT_SEND_INTERVAL_MS;
    char msg[64];
    snprintf(msg, sizeof(msg), "BATT:%d", batteryPercent);
    ws.broadcastTXT((uint8_t*)msg, strlen(msg));
  }
}

void rc_websocket_failsafe_check() {
  if (millis() - lastCmdMs > settings.failsafeMs) {
    lastCmd.steer = 0;
  }
}
