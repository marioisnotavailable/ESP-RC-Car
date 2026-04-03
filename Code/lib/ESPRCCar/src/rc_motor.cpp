#include "rc_motor.h"
#include "rc_serial.h"
#include "rc_console.h"
#include "rc_pins.h"

DRV8323 drv(PIN_DRV_CS, PIN_DRV_EN, PIN_DRV_FAULT,
            PIN_DRV_SCLK, PIN_DRV_MISO, PIN_DRV_MOSI);

static int      motorTestPhase       = 1;
static uint32_t nextMotorDirSwitchMs = 0;

void rc_motor_all_off() {
  ledcWrite(CH_PWM_A, 0);
  ledcWrite(CH_PWM_B, 0);
  digitalWrite(PIN_INLA, LOW);
  digitalWrite(PIN_INLB, LOW);
  digitalWrite(PIN_INHC, LOW);
  digitalWrite(PIN_INLC, LOW);
}

void rc_motor_apply_phase(int phase) {
  rc_motor_all_off();
  delayMicroseconds(200);

  if (phase == 0) {
    ledcWrite(CH_PWM_A, PWM_DUTY);
    ledcWrite(CH_PWM_B, 0);
    if (logFlags.drv)
      console.printf("[DRV] Motor DIAG: Phase A active (PIN_INHA=%d, CH=%d duty=%d)\n",
                    PIN_INHA, CH_PWM_A, PWM_DUTY);
  } else if (phase == 1) {
    ledcWrite(CH_PWM_A, 0);
    ledcWrite(CH_PWM_B, PWM_DUTY);
    if (logFlags.drv)
      console.printf("[DRV] Motor DIAG: Phase B active (PIN_INHB=%d, CH=%d duty=%d)\n",
                    PIN_INHB, CH_PWM_B, PWM_DUTY);
  } else {
    if (logFlags.drv) console.println("[DRV] Motor DIAG: All off");
  }
}

void rc_motor_setup() {
  console.println("[DRV] Initializing DRV8323S...");
  drv.begin();

  drv.writeRegister(0x2, 0x080);
  console.printf("[DRV] DRV_CTRL (0x2) set to 0x%03X\n", drv.readRegister(0x2));

  console.println("[DRV] Register dump (0x0-0x6):");
  for (uint8_t reg = 0; reg <= 6; ++reg) {
    uint16_t val = drv.readRegister(reg);
    console.printf("[DRV] Reg 0x%X = 0x%03X\n", reg, val);
  }

  pinMode(PIN_INHA, OUTPUT); digitalWrite(PIN_INHA, LOW);
  pinMode(PIN_INLA, OUTPUT); digitalWrite(PIN_INLA, LOW);
  pinMode(PIN_INHB, OUTPUT); digitalWrite(PIN_INHB, LOW);
  pinMode(PIN_INLB, OUTPUT); digitalWrite(PIN_INLB, LOW);
  pinMode(PIN_INHC, OUTPUT); digitalWrite(PIN_INHC, LOW);
  pinMode(PIN_INLC, OUTPUT); digitalWrite(PIN_INLC, LOW);

  bool setup_a = ledcSetup(CH_PWM_A, PWM_FREQ, PWM_BITS);
  bool setup_b = ledcSetup(CH_PWM_B, PWM_FREQ, PWM_BITS);
  ledcAttachPin(PIN_INHA, CH_PWM_A);
  ledcAttachPin(PIN_INHB, CH_PWM_B);

  console.printf("[DRV] LEDC Setup A: %s, B: %s | Attached A->CH%d, B->CH%d at %u Hz\n",
    setup_a ? "OK" : "FAIL", setup_b ? "OK" : "FAIL",
    CH_PWM_A, CH_PWM_B, PWM_FREQ);

  ledcWrite(CH_PWM_A, 0);
  ledcWrite(CH_PWM_B, 0);

  motorTestPhase = 0;
  rc_motor_apply_phase(motorTestPhase);
  nextMotorDirSwitchMs = millis() + MOTOR_DIR_SWITCH_MS;
}

void rc_motor_loop() {
  uint32_t now = millis();
  if (now >= nextMotorDirSwitchMs) {
    motorTestPhase = (motorTestPhase + 1) % 3;
    rc_motor_apply_phase(motorTestPhase);
    nextMotorDirSwitchMs = now + MOTOR_DIR_SWITCH_MS;
    if (logFlags.drv)
      console.printf("[DRV] Next test phase in %lu ms\n", (unsigned long)MOTOR_DIR_SWITCH_MS);
  }
}

void rc_motor_fault_check() {
  if (drv.hasFault()) {
    uint16_t f1 = drv.readFault1();
    uint16_t f2 = drv.readFault2();
    if (logFlags.drv)
      console.printf("[DRV] Fault1: 0x%03X | Fault2: 0x%03X | nFAULT: LOW (Fault detected)\n", f1, f2);
    drv.clearFaults();
  }
}
