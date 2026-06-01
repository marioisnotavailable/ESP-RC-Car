# Motor Driver Rewrite — Design Spec
**Date:** 2026-04-24  
**Branch:** esp-idf-migration  
**Target hardware:** ESP32-S3 + Mini1410 2500kv BLDC + DRV8323S

---

## Context

Current `drv8323.c` has correct SPI framing but never writes any configuration registers — gate-drive current, OCP threshold, and CSA gain stay at power-on defaults. `rc_motor.c` is a hardcoded test stub (one static phase, no `cmd_queue` reading). This rewrite replaces both files from scratch.

Reference: madcowswe's DRV8323 gists (ODrive project, public domain).

---

## Scope

- **drv8323.c / drv8323.h** — full register configuration, register address defines, corrected `clear_faults`
- **rc_motor.c / rc_motor.h** — open-loop 6-step trapezoidal commutation with soft-start ramp, `cmd_queue` input, direction reversal, DRV8323 fault handling

No new files. No new FreeRTOS tasks. Public API (`motor_task`, `DRV8323` struct, SPI functions) stays identical.

---

## Architecture

```
cmd_queue (throttle -1000..+1000)
       │
       ▼
  motor_task (FreeRTOS task)
       │
       ├─ DRV8323 (SPI2) ──── 6x LEDC channels ────► Motor phases (INH/INL A/B/C)
       │    registers          (GPIO 18,8,3,9,10,11)
       │    configured
       │
       └─ fault_pin (GPIO39) ── polled every 20 steps
```

---

## drv8323.c — Design

### Register addresses (new defines in drv8323.h)
```c
#define ADR_FAULT_STAT   0x00   // read-only
#define ADR_VGS_STAT     0x01   // read-only
#define ADR_DRV_CTRL     0x02
#define ADR_GATE_DRV_HS  0x03
#define ADR_GATE_DRV_LS  0x04
#define ADR_OCP_CTRL     0x05
#define ADR_CSA_CTRL     0x06
```

### Register values (based on madcowswe reference)
| Register | Value | Key settings |
|----------|-------|-------------|
| DRV_CTRL | `DIS_GDF=1, PWM_MODE=0` | 6x PWM mode, gate drive fault disabled |
| GATE_DRV_HS | `LOCK=3, IDRIVEP=11, IDRIVEN=15` | Unlocked, full gate current |
| GATE_DRV_LS | `CBC=1, TDRIVE=2, IDRIVEP=15, IDRIVEN=15` | Cycle-by-cycle OCP on LS |
| OCP_CTRL | `DEAD_TIME=1, OCP_MODE=1, OCP_DEG=2, VDS_LVL=0` | Latch shutdown, 0.06V VDS threshold |
| CSA_CTRL | `CSA_FET=1, VREF_DIV=1, CSA_GAIN=2 (10x), CSEN_LVL=3` | LS-shunt, 10x gain, all 3 phases sensed |

### Init sequence
1. Assert EN pin high, wait 1ms
2. Write all 5 registers in order (DRV_CTRL → GATE_DRV_HS → GATE_DRV_LS → OCP_CTRL → CSA_CTRL), 1ms delay between each
3. Read back all 7 registers (including fault status) and log for diagnostics

### clear_faults
Writes CLR_FLT to DRV_CTRL, then re-writes all 5 config registers to restore full state. Prevents register corruption after fault latch.

### SPI framing (unchanged — already correct)
- 16-bit word: bit15=R/W, bits[13:11]=addr (3-bit), bits[10:0]=data
- Mode 1, 1 MHz, CS via hardware (spics_io_num)

---

## rc_motor.c — Design

### Constants (rc_motor.h)
```c
#define PWM_FREQ_HZ         20000
#define PWM_BITS            LEDC_TIMER_10_BIT
#define PWM_DUTY_MAX        1023

#define RAMP_PERIOD_MAX_MS  50    // step period at standstill
#define RAMP_PERIOD_MIN_MS   5    // step period at full throttle
#define RAMP_DUTY_START     (PWM_DUTY_MAX / 10)   // 10% minimum duty
#define RAMP_DUTY_STEP       5    // duty increment per commutation step
#define RAMP_PERIOD_STEP_MS  1    // period decrement per commutation step
#define FAULT_CHECK_STEPS   20    // DRV8323 fault poll interval
```

### 6-Step commutation table (unchanged)
| Step | High-side | Low-side |
|------|-----------|----------|
| 0    | INH_A (PWM) | INL_B (HIGH) |
| 1    | INH_A (PWM) | INL_C (HIGH) |
| 2    | INH_B (PWM) | INL_C (HIGH) |
| 3    | INH_B (PWM) | INL_A (HIGH) |
| 4    | INH_C (PWM) | INL_A (HIGH) |
| 5    | INH_C (PWM) | INL_B (HIGH) |

All other channels = 0 (floating phase).

### Ramp state
```c
typedef struct {
    int     step;         // 0..5, wraps
    int8_t  direction;    // +1 or -1
    uint32_t duty;        // current duty (ramps toward target)
    uint32_t period_ms;   // current step period (ramps toward target)
} MotorState;
```

Reset when: throttle crosses zero, direction reverses, or fault clears.

### motor_task loop
```
init:
  ledc_init()
  drv8323_init()
  check startup fault → log + task delete on hard fault
  bootstrap_precharge()

loop:
  xQueueReceive(cmd_queue, &cmd, pdMS_TO_TICKS(10))
  
  if SAFE_MODE_BIT set:
    all_off(), vTaskSuspend(NULL)

  if throttle == 0 or sign changed:
    all_off(), reset MotorState

  else:
    target_duty   = abs(throttle) * PWM_DUTY_MAX / 1000
    target_period = RAMP_PERIOD_MIN_MS + (RAMP_PERIOD_MAX_MS - RAMP_PERIOD_MIN_MS)
                    * (1000 - abs(throttle)) / 1000

    ramp duty   toward target_duty   by RAMP_DUTY_STEP (clamp to target)
    ramp period toward target_period by RAMP_PERIOD_STEP_MS (clamp to target)

    apply_step(state.step, state.duty)
    state.step = (state.step + state.direction + 6) % 6

    vTaskDelay(pdMS_TO_TICKS(state.period_ms))

    if (++fault_counter >= FAULT_CHECK_STEPS):
      fault_counter = 0
      if drv8323_has_fault():
        log fault registers
        all_off(), vTaskDelay(500ms)
        drv8323_clear_faults()
        bootstrap_precharge()
        reset MotorState
```

### Direction
`throttle > 0` → `direction = +1` (step increments forward)  
`throttle < 0` → `direction = -1` (step decrements, same table traversed in reverse)  
Direction change → ramp reset (avoids mid-ramp reversal current spike).

### Error handling
- Startup fault: log hex fault registers, `vTaskDelete(NULL)` — hard stop, requires power cycle
- Runtime fault: all_off, 500ms cooldown, `drv8323_clear_faults()` (re-writes all registers), ramp reset, resume
- SAFE_MODE_BIT: all_off, task suspends until cleared externally

---

## What does NOT change
- Pin definitions in `rc_common.h`
- `DRV8323` struct fields (`spi`, `en_pin`, `fault_pin`)
- Public API: `drv8323_init`, `drv8323_read_reg`, `drv8323_write_reg`, `drv8323_has_fault`, `drv8323_read_fault1`, `drv8323_read_fault2`, `drv8323_clear_faults`, `motor_task`
- LEDC timer/channel assignment
- `CMakeLists.txt`
