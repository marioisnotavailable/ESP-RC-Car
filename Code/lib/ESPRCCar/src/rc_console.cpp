#include "rc_console.h"
#include "rc_websocket.h"

RCConsole console;

void RCConsole::wsFlush() {
  if (wsLen == 0) return;
  rc_websocket_terminal_store(wsBuf, wsLen);
  rc_websocket_terminal_broadcast(wsBuf, wsLen);
  wsLen = 0;
}

void RCConsole::wsBufAppend(const uint8_t* data, size_t len) {
  size_t pos = 0;
  while (pos < len) {
    size_t room = WS_BUF_SIZE - wsLen;
    size_t chunk = len - pos;
    if (chunk > room) chunk = room;
    memcpy(wsBuf + wsLen, data + pos, chunk);
    wsLen += chunk;
    pos += chunk;

    // Flush on buffer full
    if (wsLen >= WS_BUF_SIZE) {
      wsFlush();
      continue;
    }

    // Flush on newline (scan the chunk we just added)
    for (size_t i = wsLen - chunk; i < wsLen; ++i) {
      if (wsBuf[i] == '\n') {
        wsFlush();
        break;
      }
    }
  }
}

size_t RCConsole::write(uint8_t c) {
  Serial.write(c);
  wsBufAppend(&c, 1);
  return 1;
}

size_t RCConsole::write(const uint8_t* buf, size_t size) {
  Serial.write(buf, size);
  wsBufAppend(buf, size);
  return size;
}
