# BLDC Motor Test Spin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the 2-phase diagnostic loop with a full 6-step trapezoidal commutation routine that spins the BLDC motor forward 7s → ramp-up → reverse 7s → repeat, open-loop.

**Architecture:** All changes are confined to `rc_motor.h` and `rc_motor.cpp`. A commutation lookup table drives 6 INH/INL pins via the DRV8323S gate driver. `rc_motor_loop()` runs a two-state machine (RAMP_UP → HOLD) that flips direction after each 7s hold.

**Tech Stack:** ESP32 Arduino (ESP-IDF), LEDC PWM, DRV8323S SPI gate driver, PlatformIO

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `lib/ESPRCCar/src/rc_motor.h` | Modify | Constants, enums, function declarations |
| `lib/ESPRCCar/src/rc_motor.cpp` | Modify | Commutation table, setup, state machine loop |

---

### Task 1: Update constants and declarations in `rc_motor.h`

**Files:**
- Modify: `lib/ESPRCCar/src/rc_motor.h`

- [ ] **Step 1: Replace the file content**

Replace the entire file with:

```cpp
#pragma once

#include <Arduino.h>
#include "drv8323.h"

// PWM configuration
static constexpr uint32_t PWM_FREQ      = 20000;   // 20 kHz
static constexpr uint8_t  PWM_BITS      = 10;      // 0..1023
static constexpr uint16_t PWM_DUTY      = 512;     // ~50%

// Commutation timing
static constexpr uint32_t MOTOR_HOLD_MS = 7000UL;  // hold per direction
static constexpr uint32_t STEP_SLOW_MS  = 100UL;   // ramp start interval
static constexpr uint32_t STEP_FAST_MS  = 10UL;    // ramp end / hold interval
static constexpr uint8_t  RAMP_STEPS    = 20;      // steps in ramp

extern DRV8323 drv;

void rc_motor_setup();
void rc_motor_loop();
void rc_motor_all_off();
void rc_motor_apply_phase(int step);
void rc_motor_fault_check();
```

- [ ] **Step 2: Build to check header compiles cleanly**

```bash
cd /home/leon/Documents/GitHub/ESP-RC-Car/Code
pio run -e esp32dev 2>&1 | tail -20
```

Expected: errors in `rc_motor.cpp` about missing `MOTOR_DIR_SWITCH_MS`, `motorTestPhase`, `nextMotorDirSwitchMs` — that is correct, those are removed next. Header itself must not produce errors.

- [ ] **Step 3: Commit**

```bash
git add lib/ESPRCCar/src/rc_motor.h
git commit -m "refactor(motor): replace diagnostic constants with commutation timing"
```

---

### Task 2: Rewrite `rc_motor.cpp` — commutation table and `rc_motor_apply_phase`

**Files:**
- Modify: `lib/ESPRCCar/src/rc_motor.cpp`

- [ ] **Step 1: Replace the top of the file (globals + all_off + apply_phase)**

Replace from line 1 through the end of `rc_motor_apply_phase` with:

```cpp
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
```

**Note:** `rc_motor_all_off` now uses `ledcWrite(PIN_INHC, 0)` instead of the old `digitalWrite(PIN_INHC, LOW)` — required because `PIN_INHC` will have a LEDC channel attached after Task 3's setup change.

- [ ] **Step 2: Build to check these functions compile**

```bash
pio run -e esp32dev 2>&1 | tail -20
```

Expected: errors about `rc_motor_setup` and `rc_motor_loop` still referencing old variables — that is fine, those are replaced in the next tasks.

---

### Task 3: Rewrite `rc_motor_setup()`

**Files:**
- Modify: `lib/ESPRCCar/src/rc_motor.cpp`

- [ ] **Step 1: Replace `rc_motor_setup()` with**

```cpp
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
```

- [ ] **Step 2: Build to check setup compiles**

```bash
pio run -e esp32dev 2>&1 | tail -20
```

Expected: only `rc_motor_loop` errors remain (old variables).

---

### Task 4: Rewrite `rc_motor_loop()` — state machine

**Files:**
- Modify: `lib/ESPRCCar/src/rc_motor.cpp`

- [ ] **Step 1: Replace `rc_motor_loop()` with**

```cpp
void rc_motor_loop() {
  uint32_t now = millis();
  if (now < nextStepMs) return;

  if (motorState == RAMP_UP) {
    uint32_t interval = STEP_SLOW_MS
      - (uint32_t)rampStep * (STEP_SLOW_MS - STEP_FAST_MS) / RAMP_STEPS;

    currentCommStep = nextCommStep(currentCommStep, motorDir);
    rc_motor_apply_phase(currentCommStep);
    nextStepMs = now + interval;
    rampStep++;

    if (rampStep >= RAMP_STEPS) {
      motorState  = HOLD;
      holdStartMs = now;
      if (logFlags.drv)
        console.printf("[DRV] Ramp done → HOLD dir=%s\n",
          motorDir == FWD ? "FWD" : "REV");
    }
  } else {  // HOLD
    currentCommStep = nextCommStep(currentCommStep, motorDir);
    rc_motor_apply_phase(currentCommStep);
    nextStepMs = now + STEP_FAST_MS;

    if (now - holdStartMs >= MOTOR_HOLD_MS) {
      motorDir   = (motorDir == FWD) ? REV : FWD;
      motorState = RAMP_UP;
      rampStep   = 0;
      if (logFlags.drv)
        console.printf("[DRV] Direction → %s, starting ramp\n",
          motorDir == FWD ? "FWD" : "REV");
    }
  }
}
```

- [ ] **Step 2: Full build — must succeed with zero errors**

```bash
pio run -e esp32dev 2>&1 | tail -30
```

Expected: `SUCCESS` / `[env:esp32dev] Building... done` with no errors. Warnings about unused variables are acceptable.

- [ ] **Step 3: Also build ota env to catch any regressions**

```bash
pio run -e ota 2>&1 | tail -10
```

Expected: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add lib/ESPRCCar/src/rc_motor.cpp
git commit -m "feat(motor): implement 6-step trapezoidal commutation with ramp-up and direction reversal"
```

---

### Task 5: Flash and verify on hardware

**Files:** none (verification only)

- [ ] **Step 1: Flash to device**

```bash
pio run -e esp32dev --target upload 2>&1 | tail -20
```

Expected: upload success.

- [ ] **Step 2: Open serial monitor**

```bash
pio device monitor --baud 115200
```

- [ ] **Step 3: Enable DRV logging if not already on**

Send via serial terminal:
```
log drv
```

Expected serial output pattern:
```
[DRV] LEDC Setup A:OK B:OK C:OK | pins 18,3,10 at 20000 Hz
[DRV] Step 0 | INH:100 INL:010
[DRV] Step 1 | INH:100 INL:001
...
[DRV] Ramp done → HOLD dir=FWD
...
[DRV] Direction → REV, starting ramp
```

- [ ] **Step 4: Confirm motor behavior**

Observe motor physically:
1. Ramp-up period (~2s): motor accelerates from slow to target speed
2. Hold ~7s in one direction
3. Ramp-up in opposite direction
4. Hold ~7s reverse
5. Repeats

If motor does not spin:
- Check `[DRV] Reg 0x0` and `Reg 0x1` for non-zero fault bits
- Check `[DRV] LEDC Setup` lines — all must be `OK`
- Verify power supply to DRV8323S and motor

- [ ] **Step 5: Commit verification note (optional)**

```bash
git commit --allow-empty -m "chore: verified 6-step commutation on hardware"
```
