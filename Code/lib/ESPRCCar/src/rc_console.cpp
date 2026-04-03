#include "rc_console.h"
#include "rc_websocket.h"

RCConsole console;

size_t RCConsole::write(uint8_t c) {
  Serial.write(c);
  rc_websocket_terminal_broadcast(&c, 1);
  return 1;
}

size_t RCConsole::write(const uint8_t* buf, size_t size) {
  Serial.write(buf, size);
  rc_websocket_terminal_broadcast(buf, size);
  return size;
}
