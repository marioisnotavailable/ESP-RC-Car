// rc_console.h — Dual output: Serial + WebSocket terminal
#pragma once

#include <Arduino.h>

class RCConsole : public Print {
public:
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buf, size_t size) override;
private:
  static constexpr size_t WS_BUF_SIZE = 256;
  uint8_t wsBuf[WS_BUF_SIZE];
  size_t  wsLen = 0;
  void wsFlush();
  void wsBufAppend(const uint8_t* data, size_t len);
};

extern RCConsole console;
