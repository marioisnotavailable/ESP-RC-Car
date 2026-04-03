// rc_console.h — Dual output: Serial + WebSocket terminal
#pragma once

#include <Arduino.h>

class RCConsole : public Print {
public:
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buf, size_t size) override;
};

extern RCConsole console;
