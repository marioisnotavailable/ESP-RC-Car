#include "rc_motor.h"
#include "rc_serial.h"
#include "rc_console.h"
#include "rc_pins.h"

DRV8323 drv(PIN_DRV_CS, PIN_DRV_EN, PIN_DRV_FAULT,
            PIN_DRV_SCLK, PIN_DRV_MISO, PIN_DRV_MOSI);

// ── Commutation table (6-step trapezoidal) ──────────────────────────────────
struct CommStep {
  uint16_t inh_a, inh_b, inh_c;
  uint8_t  inl_a, inl_b, inl_c;
};

static const CommStep COMM_TABLE[6] = {
  { PWM_DUTY, 0,        0,        0, 1, 0 },  // Step 0: A+→B-
  { PWM_DUTY, 0,        0,        0, 0, 1 },  // Step 1: A+→C-
  { 0,        PWM_DUTY, 0,        0, 0, 1 },  // Step 2: B+→C-
  { 0,        PWM_DUTY, 0,        1, 0, 0 },  // Step 3: B+→A-
  { 0,        0,        PWM_DUTY, 1, 0, 0 },  // Step 4: C+→A-
  { 0,        0,        PWM_DUTY, 0, 1, 0 },  // Step 5: C+→B-
};

// ── State ────────────────────────────────────────────────────────────────────
enum MotorState { RAMP_UP, HOLD };
enum MotorDir   { FWD, REV };

static MotorState motorState      = RAMP_UP;
static MotorDir   motorDir        = FWD;
static uint8_t    rampStep        = 0;
static uint8_t    currentCommStep = 0;
static uint32_t   nextStepMs      = 0;
static uint32_t   holdStartMs     = 0;

// ── Helpers ──────────────────────────────────────────────────────────────────
static int nextCommStep(int current, MotorDir dir) {
  if (dir == FWD) return (current + 1) % 6;
  else            return (current + 5) % 6;  // = (current - 1 + 6) % 6
}

void rc_motor_all_off() {
  ledcWrite(PIN_INHA, 0);
  ledcWrite(PIN_INHB, 0);
  ledcWrite(PIN_INHC, 0);
  digitalWrite(PIN_INLA, LOW);
  digitalWrite(PIN_INLB, LOW);
  digitalWrite(PIN_INLC, LOW);
}

void rc_motor_apply_phase(int step) {
  rc_motor_all_off();
  delayMicroseconds(200);
  const CommStep& s = COMM_TABLE[step];
  ledcWrite(PIN_INHA, s.inh_a);
  ledcWrite(PIN_INHB, s.inh_b);
  ledcWrite(PIN_INHC, s.inh_c);
  digitalWrite(PIN_INLA, s.inl_a ? HIGH : LOW);
  digitalWrite(PIN_INLB, s.inl_b ? HIGH : LOW);
  digitalWrite(PIN_INLC, s.inl_c ? HIGH : LOW);
  if (logFlags.drv)
    console.printf("[DRV] Step %d | INH:%d%d%d INL:%d%d%d\n",
      step,
      s.inh_a > 0, s.inh_b > 0, s.inh_c > 0,
      s.inl_a, s.inl_b, s.inl_c);
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

  bool setup_a = ledcAttach(PIN_INHA, PWM_FREQ, PWM_BITS);
  bool setup_b = ledcAttach(PIN_INHB, PWM_FREQ, PWM_BITS);
  bool setup_c = ledcAttach(PIN_INHC, PWM_FREQ, PWM_BITS);

  console.printf("[DRV] LEDC Setup A:%s B:%s C:%s | pins %d,%d,%d at %u Hz\n",
    setup_a ? "OK" : "FAIL",
    setup_b ? "OK" : "FAIL",
    setup_c ? "OK" : "FAIL",
    PIN_INHA, PIN_INHB, PIN_INHC, PWM_FREQ);

  ledcWrite(PIN_INHA, 0);
  ledcWrite(PIN_INHB, 0);
  ledcWrite(PIN_INHC, 0);

  motorState      = RAMP_UP;
  motorDir        = FWD;
  rampStep        = 0;
  currentCommStep = 0;
  rc_motor_apply_phase(currentCommStep);
  nextStepMs = millis() + STEP_SLOW_MS;
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
