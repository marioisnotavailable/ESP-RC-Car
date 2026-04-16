# BLDC Motor Test Spin Design

**Date:** 2026-04-13  
**Scope:** `rc_motor.h`, `rc_motor.cpp` only

---

## Goal

Replace the existing 2-phase diagnostic loop with a real 6-step trapezoidal commutation routine that spins the 3-phase BLDC motor forward for 7s, then reverse for 7s, repeating indefinitely. Open-loop (no hall sensors).

---

## Hardware Context

- **Gate driver:** DRV8323S, 6x PWM independent mode
- **Pins:**
  - High-side: `PIN_INHA` (18), `PIN_INHB` (3), `PIN_INHC` (10)
  - Low-side: `PIN_INLA` (8), `PIN_INLB` (9), `PIN_INLC` (11)
- **Current limitation:** `PIN_INHC` has no LEDC attached yet — must be added in setup

---

## Commutation Table (6-step trapezoidal)

| Step | INH_A | INH_B | INH_C | INL_A | INL_B | INL_C | Path   |
|------|-------|-------|-------|-------|-------|-------|--------|
| 0    | PWM   | 0     | 0     | 0     | 1     | 0     | A+→B-  |
| 1    | PWM   | 0     | 0     | 0     | 0     | 1     | A+→C-  |
| 2    | 0     | PWM   | 0     | 0     | 0     | 1     | B+→C-  |
| 3    | 0     | PWM   | 0     | 1     | 0     | 0     | B+→A-  |
| 4    | 0     | 0     | PWM   | 1     | 0     | 0     | C+→A-  |
| 5    | 0     | 0     | PWM   | 0     | 1     | 0     | C+→B-  |

- **Forward:** steps 0 → 1 → 2 → 3 → 4 → 5 → 0 …
- **Reverse:** steps 5 → 4 → 3 → 2 → 1 → 0 → 5 …

---

## State Machine

```
RAMP_UP(FWD) → HOLD(FWD, 7s) → RAMP_UP(REV) → HOLD(REV, 7s) → repeat
```

### RAMP_UP
- 20 steps (`RAMP_STEPS`)
- Step interval linear from `STEP_SLOW_MS=100ms` down to `STEP_FAST_MS=10ms`
- Formula: `interval = STEP_SLOW_MS - rampStep * (STEP_SLOW_MS - STEP_FAST_MS) / RAMP_STEPS`
- On completion → transition to HOLD

### HOLD
- Continue commutating at `STEP_FAST_MS` interval
- After `MOTOR_HOLD_MS=7000ms` elapsed → flip direction, restart RAMP_UP

---

## Constants (rc_motor.h)

| Constant | Value | Purpose |
|----------|-------|---------|
| `MOTOR_HOLD_MS` | 7000 | Duration per direction |
| `STEP_SLOW_MS` | 100 | Ramp start interval |
| `STEP_FAST_MS` | 10 | Ramp end / hold interval |
| `RAMP_STEPS` | 20 | Steps in ramp |
| `PWM_FREQ` | 20000 | Unchanged |
| `PWM_BITS` | 10 | Unchanged |
| `PWM_DUTY` | 512 | ~50%, unchanged |

Remove: `MOTOR_DIR_SWITCH_MS`

---

## Code Changes

### `rc_motor_setup()`
- Add `ledcAttach(PIN_INHC, PWM_FREQ, PWM_BITS)` alongside existing A and B
- Initialize state: `motorState = RAMP_UP`, `motorDir = FWD`, `rampStep = 0`, `currentStep = 0`
- Start first commutation step

### `rc_motor_apply_phase(int step)`
- Replace 2-phase if/else with 6-entry lookup table
- All-off dead-time (200µs) kept before applying new step
- INH pins: `ledcWrite(pin, PWM_DUTY or 0)`
- INL pins: `digitalWrite(pin, HIGH or LOW)`

### `rc_motor_loop()`
- Replace diagnostic timer with state machine
- RAMP_UP: advance commutation step on interval, decrease interval each step, count rampStep
- HOLD: advance commutation step on STEP_FAST_MS, check if MOTOR_HOLD_MS elapsed

### `rc_motor_all_off()`
- No change needed

### `rc_motor_fault_check()`
- No change needed

---

## Error Handling

- DRV8323S fault detection unchanged (`rc_motor_fault_check()` already handles it)
- No additional validation needed — all inputs are internal constants
