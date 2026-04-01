#include "rc_led.h"
#include "rc_pins.h"

static uint32_t ledTimer = 0;
static bool     ledState = false;

static uint16_t throttleToInterval(int16_t thr) {
  int a = abs(thr);
  return BLINK_MAX_MS - (uint32_t)(BLINK_MAX_MS - BLINK_MIN_MS) * a / 1000;
}

void rc_led_loop(int16_t throttle) {
  uint32_t now = millis();
  uint16_t interval = throttleToInterval(throttle);

  if (throttle == 0) {
    ledState = false;
    digitalWrite(LED_PIN, LOW);
  } else if (now - ledTimer >= interval) {
    ledTimer = now;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
  }
}
