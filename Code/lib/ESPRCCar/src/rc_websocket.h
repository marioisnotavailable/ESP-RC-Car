#pragma once

#include <Arduino.h>
#include <WebSocketsServer.h>

struct Cmd {
  int16_t throttle;  // -1000..+1000
  int16_t steer;     // -1000..+1000
  uint8_t flags;
};

extern WebSocketsServer ws;
extern volatile Cmd lastCmd;
extern uint32_t lastCmdMs;

void rc_websocket_begin();
void rc_websocket_loop();
void rc_websocket_broadcast_batt();
void rc_websocket_failsafe_check();
void rc_websocket_terminal_store(const uint8_t* buf, size_t size);
void rc_websocket_terminal_send_backlog(uint8_t clientNum);
void rc_websocket_terminal_broadcast(const uint8_t* buf, size_t size);
