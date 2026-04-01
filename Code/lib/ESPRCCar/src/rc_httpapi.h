#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>

extern WebServer httpServer;

// Start AP + captive portal + all HTTP routes
void rc_start_portal();

// Call in loop() when portal is active
void rc_portal_loop();
