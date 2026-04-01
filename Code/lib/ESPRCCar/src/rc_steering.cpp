#include "rc_steering.h"
#include "rc_settings.h"
#include "rc_pins.h"
#include "driver/gpio.h"

float steerFilt      = 0.0f;
int   currentServoUs = SERVO_MID_US;

static bool g_useTimerFallback = false;

// LEDC: us -> duty
static inline uint32_t usToDuty(uint16_t us, uint8_t resBits) {
  const uint32_t maxDuty = (1u << resBits) - 1u;
  return (uint32_t)((uint64_t)us * maxDuty / 20000u);
}

// ---- Timer fallback ----
static hw_timer_t* servoTimer = nullptr;
static volatile bool     highPhase       = false;
static volatile uint32_t targetUs_timer  = SERVO_MID_US;
static volatile uint32_t currentUs_timer = SERVO_MID_US;

static void IRAM_ATTR servoISR() {
  if (highPhase) {
    gpio_set_level((gpio_num_t)LENKUNG_PIN, 0);
    highPhase = false;
    uint32_t lowUs = 20000 - currentUs_timer;
    if (lowUs < 100) lowUs = 100;
    timerAlarmWrite(servoTimer, lowUs, false);
    timerAlarmEnable(servoTimer);
  } else {
    uint32_t us = targetUs_timer;
    if (us < 500)  us = 500;
    if (us > 2500) us = 2500;
    currentUs_timer = us;
    gpio_set_level((gpio_num_t)LENKUNG_PIN, 1);
    highPhase = true;
    timerAlarmWrite(servoTimer, currentUs_timer, false);
    timerAlarmEnable(servoTimer);
  }
}

void rc_steering_init_fallback() {
  g_useTimerFallback = true;
  pinMode(LENKUNG_PIN, OUTPUT);
  digitalWrite(LENKUNG_PIN, LOW);

  if (servoTimer) { timerEnd(servoTimer); servoTimer = nullptr; }
  servoTimer = timerBegin(1, 80, true);
  timerAttachInterrupt(servoTimer, &servoISR, true);
  highPhase = false;
  targetUs_timer = SERVO_MID_US;
  timerAlarmWrite(servoTimer, 1000, false);
  timerAlarmEnable(servoTimer);
  Serial.println("[SERVO] Fallback: HW-Timer aktiv");
}

void rc_steering_init_ledc() {
  ledcDetachPin(LENKUNG_PIN);
  bool ok = ledcSetup(SERVO_CH, SERVO_FREQ, SERVO_RES_BITS);
  if (!ok) {
    Serial.println("[SERVO] LEDC Setup FAIL");
    return;
  }
  ledcAttachPin(LENKUNG_PIN, SERVO_CH);
  ledcWrite(SERVO_CH, usToDuty(SERVO_MID_US, SERVO_RES_BITS));
  Serial.println("[SERVO] LEDC aktiv");
}

void rc_steering_write_us(int targetUs) {
  targetUs = constrain(targetUs, SERVO_MIN_US, SERVO_MAX_US);

  if (SERVO_SLEW_US_PER_LOOP > 0) {
    static int prevUs = SERVO_MID_US;
    int delta = targetUs - prevUs;
    if (delta > SERVO_SLEW_US_PER_LOOP) delta = SERVO_SLEW_US_PER_LOOP;
    else if (delta < -SERVO_SLEW_US_PER_LOOP) delta = -SERVO_SLEW_US_PER_LOOP;
    currentServoUs = prevUs + delta;
    prevUs = currentServoUs;
  } else {
    currentServoUs = targetUs;
  }

  if (!g_useTimerFallback) {
    ledcWrite(SERVO_CH, usToDuty((uint16_t)currentServoUs, SERVO_RES_BITS));
  } else {
    targetUs_timer = currentServoUs;
  }
}

void rc_steering_apply(int16_t steerInput) {
  int s = steerInput;

  if (abs(s) < (int)settings.steerDeadzone) s = 0;
  if (settings.steerInvert) s = -s;

  steerFilt = settings.steerFilter * steerFilt + (1.0f - settings.steerFilter) * (float)s;

  float halfSpan = 0.5f * (float)(SERVO_MAX_US - SERVO_MIN_US);
  int targetUs = SERVO_MID_US + (int)(steerFilt * settings.steerGain * (halfSpan / 1000.0f));
  currentServoUs = constrain(targetUs, SERVO_MIN_US, SERVO_MAX_US);
}
