#pragma once

#include <Arduino.h>

#define BLINK_MIN_MS 50
#define BLINK_MAX_MS 800

void rc_led_loop(int16_t throttle);
