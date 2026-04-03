#include "rc_websocket.h"
#include "rc_settings.h"
#include "rc_battery.h"
#include "rc_serial.h"
#include "rc_console.h"
#include <cstring>

#define WS_PORT            81
#define BATT_SEND_INTERVAL_MS 1000

WebSocketsServer ws(WS_PORT);
volatile Cmd lastCmd = {0, 0, 0};
uint32_t lastCmdMs   = 0;

static uint32_t nextBattSendMs = 0;
static bool failsafeActive = false;
static bool wsStarted = false;

static void onWs(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_CONNECTED) {
    IPAddress ip = ws.remoteIP(num);
    if (logFlags.ws)
      console.printf("[WS] Client #%u connected from %s (total: %u)\n",
        num, ip.toString().c_str(), ws.connectedClients());
    return;
  }
  if (type == WStype_DISCONNECTED) {
    if (logFlags.ws)
      console.printf("[WS] Client #%u disconnected (remaining: %u)\n",
        num, ws.connectedClients());
    return;
  }
  if (type == WStype_TEXT) {
    if (len >= 4 && memcmp(payload, "CMD:", 4) == 0) {
      static char cmdBuf[192];
      size_t cmdLen = min(len - 4, sizeof(cmdBuf) - 1);
      memcpy(cmdBuf, payload + 4, cmdLen);
      cmdBuf[cmdLen] = '\0';

      String cmd = String(cmdBuf);
      cmd.trim();
      if (cmd.length() > 0) {
        if (logFlags.ws)
          console.printf("[WS] TERM CMD from #%u: %s\n", num, cmd.c_str());
        rc_handle_command(cmd);
      }
      return;
    }

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
      if (failsafeActive) {
        failsafeActive = false;
        if (logFlags.ws) console.println("[WS] Failsafe aufgehoben — Steuerung aktiv");
      }
      if (logFlags.ws)
        console.printf("[WS] CMD thr=%d steer=%d flags=%d (max=%d)\n",
          lastCmd.throttle, lastCmd.steer, lastCmd.flags, maxThr);
    } else {
      console.printf("[WS] Parse FAIL (r=%d) buf='%s'\n", r, buf);
    }
  }
  if (type == WStype_ERROR) {
    if (logFlags.ws) console.printf("[WS] Error on client #%u\n", num);
  }
  if (type == WStype_PING) {
    if (logFlags.ws) console.printf("[WS] Ping from #%u\n", num);
  }
  if (type == WStype_PONG) {
    if (logFlags.ws) console.printf("[WS] Pong from #%u\n", num);
  }
}

void rc_websocket_begin() {
  ws.begin();
  ws.onEvent(onWs);
  wsStarted = true;
  lastCmdMs = millis();
  console.printf("[WS] Server gestartet auf Port %d\n", WS_PORT);
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
    if (logFlags.ws)
      console.printf("[WS] Broadcast BATT:%d an %u Clients\n", batteryPercent, ws.connectedClients());
  }
}

void rc_websocket_failsafe_check() {
  if (millis() - lastCmdMs > settings.failsafeMs) {
    if (!failsafeActive) {
      failsafeActive = true;
      if (logFlags.ws)
        console.printf("[WS] FAILSAFE! Kein Befehl seit %dms — Lenkung=0\n", settings.failsafeMs);
    }
    lastCmd.steer = 0;
  }
}

void rc_websocket_terminal_broadcast(const uint8_t* buf, size_t size) {
  if (!wsStarted || size == 0 || ws.connectedClients() == 0) return;

  const size_t chunk = 180;
  uint8_t frame[5 + chunk];
  memcpy(frame, "TERM:", 5);

  size_t off = 0;
  while (off < size) {
    size_t n = min(chunk, size - off);
    memcpy(frame + 5, buf + off, n);
    ws.broadcastTXT(frame, n + 5);
    off += n;
  }
}
