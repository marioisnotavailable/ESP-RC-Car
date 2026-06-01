# Motor Driver Rewrite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite drv8323.c (full register config) and rc_motor.c (open-loop 6-step commutation with soft-start ramp reading from cmd_queue) from scratch.

**Architecture:** DRV8323 driver configures all 5 writable registers on init (gate-drive current, OCP, CSA) and restores them on fault clear. Motor task reads throttle from cmd_queue, ramps commutation step-period and PWM duty from standstill values toward throttle-proportional targets, traversing the 6-step table forward or backward depending on sign.

**Tech Stack:** ESP-IDF, FreeRTOS, ESP32-S3, LEDC (20kHz 10-bit PWM), SPI2, DRV8323S gate driver

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `components/rc_motor/include/drv8323.h` | Modify | Add register address `#define`s |
| `components/rc_motor/drv8323.c` | Rewrite | Full register init, read-back log, fixed clear_faults |
| `components/rc_motor/include/rc_motor.h` | Modify | Add ramp constant `#define`s |
| `components/rc_motor/rc_motor.c` | Rewrite | 6-step commutation, soft-start ramp, cmd_queue, fault recovery |

---

## Task 1: drv8323.h — Register Address Defines

**Files:**
- Modify: `components/rc_motor/include/drv8323.h`

- [ ] **Step 1: Replace header with register defines added**

```c
#pragma once
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include <stdbool.h>

// Register addresses
#define ADR_FAULT_STAT   0x00
#define ADR_VGS_STAT     0x01
#define ADR_DRV_CTRL     0x02
#define ADR_GATE_DRV_HS  0x03
#define ADR_GATE_DRV_LS  0x04
#define ADR_OCP_CTRL     0x05
#define ADR_CSA_CTRL     0x06

typedef struct {
    spi_device_handle_t spi;
    gpio_num_t          en_pin;
    gpio_num_t          fault_pin;
} DRV8323;

esp_err_t drv8323_init(DRV8323 *drv,
                        gpio_num_t cs, gpio_num_t en, gpio_num_t fault,
                        gpio_num_t sclk, gpio_num_t miso, gpio_num_t mosi);
uint16_t  drv8323_read_reg(DRV8323 *drv, uint8_t reg);
esp_err_t drv8323_write_reg(DRV8323 *drv, uint8_t reg, uint16_t val);
bool      drv8323_has_fault(DRV8323 *drv);
uint16_t  drv8323_read_fault1(DRV8323 *drv);
uint16_t  drv8323_read_fault2(DRV8323 *drv);
esp_err_t drv8323_clear_faults(DRV8323 *drv);
```

---

## Task 2: drv8323.c — Full Register Init + Fixed clear_faults

**Files:**
- Rewrite: `components/rc_motor/drv8323.c`

Register values (based on ODrive/madcowswe reference):
| Register | Hex | Key bits |
|----------|-----|----------|
| DRV_CTRL | 0x100 | DIS_GDF=1, PWM_MODE=0 (6x PWM) |
| GATE_DRV_HS | 0x3BF | LOCK=3 (unlock), IDRIVEP=11, IDRIVEN=15 |
| GATE_DRV_LS | 0x6FF | CBC=1, TDRIVE=2, IDRIVEP=15, IDRIVEN=15 |
| OCP_CTRL | 0x160 | DEAD_TIME=1, OCP_MODE=1 (latch), OCP_DEG=2, VDS_LVL=0 |
| CSA_CTRL | 0x683 | CSA_FET=1, VREF_DIV=1, CSA_GAIN=10x, CSEN_LVL=3 |

- [ ] **Step 1: Write drv8323.c**

```c
#include "drv8323.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "drv8323";

static const uint16_t REG_DRV_CTRL    = (1 << 8);
static const uint16_t REG_GATE_DRV_HS = (3 << 8) | (11 << 4) | 15;
static const uint16_t REG_GATE_DRV_LS = (1 << 10) | (2 << 8) | (15 << 4) | 15;
static const uint16_t REG_OCP_CTRL    = (1 << 8) | (1 << 6) | (2 << 4);
static const uint16_t REG_CSA_CTRL    = (1 << 10) | (1 << 9) | (2 << 6) | 3;

static const spi_bus_config_t BUS_CFG_DEFAULTS = {
    .miso_io_num     = -1,
    .mosi_io_num     = -1,
    .sclk_io_num     = -1,
    .quadwp_io_num   = -1,
    .quadhd_io_num   = -1,
    .max_transfer_sz = 2,
};

static void write_all_regs(DRV8323 *drv)
{
    drv8323_write_reg(drv, ADR_DRV_CTRL,    REG_DRV_CTRL);    vTaskDelay(pdMS_TO_TICKS(1));
    drv8323_write_reg(drv, ADR_GATE_DRV_HS, REG_GATE_DRV_HS); vTaskDelay(pdMS_TO_TICKS(1));
    drv8323_write_reg(drv, ADR_GATE_DRV_LS, REG_GATE_DRV_LS); vTaskDelay(pdMS_TO_TICKS(1));
    drv8323_write_reg(drv, ADR_OCP_CTRL,    REG_OCP_CTRL);    vTaskDelay(pdMS_TO_TICKS(1));
    drv8323_write_reg(drv, ADR_CSA_CTRL,    REG_CSA_CTRL);    vTaskDelay(pdMS_TO_TICKS(1));
}

esp_err_t drv8323_init(DRV8323 *drv,
                        gpio_num_t cs, gpio_num_t en, gpio_num_t fault,
                        gpio_num_t sclk, gpio_num_t miso, gpio_num_t mosi)
{
    drv->en_pin    = en;
    drv->fault_pin = fault;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << en),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(en, 1);
    vTaskDelay(pdMS_TO_TICKS(1));

    io.pin_bit_mask = (1ULL << fault);
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    gpio_config(&io);

    spi_bus_config_t bus = BUS_CFG_DEFAULTS;
    bus.miso_io_num = miso;
    bus.mosi_io_num = mosi;
    bus.sclk_io_num = sclk;

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %d", ret);
        return ret;
    }

    spi_device_interface_config_t dev_cfg = {
        .command_bits   = 0,
        .address_bits   = 0,
        .dummy_bits     = 0,
        .mode           = 1,
        .clock_speed_hz = 1000000,
        .spics_io_num   = cs,
        .queue_size     = 1,
    };
    ret = spi_bus_add_device(SPI2_HOST, &dev_cfg, &drv->spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %d", ret);
        return ret;
    }

    write_all_regs(drv);

    ESP_LOGI(TAG, "DRV8323 regs: FAULT=%03X VGS=%03X CTRL=%03X HS=%03X LS=%03X OCP=%03X CSA=%03X",
             drv8323_read_reg(drv, ADR_FAULT_STAT),
             drv8323_read_reg(drv, ADR_VGS_STAT),
             drv8323_read_reg(drv, ADR_DRV_CTRL),
             drv8323_read_reg(drv, ADR_GATE_DRV_HS),
             drv8323_read_reg(drv, ADR_GATE_DRV_LS),
             drv8323_read_reg(drv, ADR_OCP_CTRL),
             drv8323_read_reg(drv, ADR_CSA_CTRL));

    return ESP_OK;
}

uint16_t drv8323_read_reg(DRV8323 *drv, uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(0x80 | (reg << 3)), 0x00 };
    uint8_t rx[2] = { 0 };
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_transmit(drv->spi, &t);
    return (uint16_t)(((rx[0] & 0x07) << 8) | rx[1]);
}

esp_err_t drv8323_write_reg(DRV8323 *drv, uint8_t reg, uint16_t val)
{
    uint8_t tx[2] = {
        (uint8_t)((reg << 3) | ((val >> 8) & 0x07)),
        (uint8_t)(val & 0xFF),
    };
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
        .rx_buffer = NULL,
    };
    return spi_device_transmit(drv->spi, &t);
}

bool drv8323_has_fault(DRV8323 *drv)
{
    return gpio_get_level(drv->fault_pin) == 0;
}

uint16_t drv8323_read_fault1(DRV8323 *drv)
{
    return drv8323_read_reg(drv, ADR_FAULT_STAT);
}

uint16_t drv8323_read_fault2(DRV8323 *drv)
{
    return drv8323_read_reg(drv, ADR_VGS_STAT);
}

esp_err_t drv8323_clear_faults(DRV8323 *drv)
{
    drv8323_write_reg(drv, ADR_DRV_CTRL, REG_DRV_CTRL | 0x0001);
    vTaskDelay(pdMS_TO_TICKS(1));
    write_all_regs(drv);
    return ESP_OK;
}
```

---

## Task 3: rc_motor.h — Ramp Constants

**Files:**
- Modify: `components/rc_motor/include/rc_motor.h`

- [ ] **Step 1: Write rc_motor.h**

```c
#pragma once
#include "rc_common.h"
#include "driver/ledc.h"

#define PWM_FREQ_HZ           20000
#define PWM_BITS              LEDC_TIMER_10_BIT
#define PWM_DUTY_MAX          1023

#define RAMP_PERIOD_MAX_MS    50
#define RAMP_PERIOD_MIN_MS     5
#define RAMP_DUTY_START       (PWM_DUTY_MAX / 10)
#define RAMP_DUTY_STEP         5
#define RAMP_PERIOD_STEP_MS    1
#define FAULT_CHECK_STEPS     20

void motor_task(void *arg);
```

---

## Task 4: rc_motor.c — Full Motor Task

**Files:**
- Rewrite: `components/rc_motor/rc_motor.c`

**6-step channel index lookup** (CH[] is 0=INH_A 1=INL_A 2=INH_B 3=INL_B 4=INH_C 5=INL_C):

| Step | STEP_INH | STEP_INL |
|------|----------|----------|
| 0 | 0 (INH_A) | 3 (INL_B) |
| 1 | 0 (INH_A) | 5 (INL_C) |
| 2 | 2 (INH_B) | 5 (INL_C) |
| 3 | 2 (INH_B) | 1 (INL_A) |
| 4 | 4 (INH_C) | 1 (INL_A) |
| 5 | 4 (INH_C) | 3 (INL_B) |

- [ ] **Step 1: Write rc_motor.c**

```c
#include "rc_motor.h"
#include "drv8323.h"
#include "driver/ledc.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

static const char *TAG = "motor";

static const ledc_channel_t CH[6] = {
    LEDC_CHANNEL_0, LEDC_CHANNEL_1,
    LEDC_CHANNEL_2, LEDC_CHANNEL_3,
    LEDC_CHANNEL_4, LEDC_CHANNEL_5,
};

static const gpio_num_t PINS[6] = {
    PIN_INHA, PIN_INLA,
    PIN_INHB, PIN_INLB,
    PIN_INHC, PIN_INLC,
};

static const int STEP_INH[6] = { 0, 0, 2, 2, 4, 4 };
static const int STEP_INL[6] = { 3, 5, 5, 1, 1, 3 };

static void ledc_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = PWM_BITS,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel  = LEDC_TIMER_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .duty       = 0,
        .hpoint     = 0,
    };
    for (int i = 0; i < 6; i++) {
        ch.channel  = CH[i];
        ch.gpio_num = PINS[i];
        ledc_channel_config(&ch);
    }
}

static void set_duty(ledc_channel_t ch, uint32_t duty)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
}

static void all_off(void)
{
    for (int i = 0; i < 6; i++) set_duty(CH[i], 0);
}

static void bootstrap_precharge(void)
{
    set_duty(CH[1], PWM_DUTY_MAX);
    set_duty(CH[3], PWM_DUTY_MAX);
    set_duty(CH[5], PWM_DUTY_MAX);
    vTaskDelay(pdMS_TO_TICKS(5));
    all_off();
    vTaskDelay(pdMS_TO_TICKS(1));
}

static void apply_step(int step, uint32_t duty)
{
    all_off();
    set_duty(CH[STEP_INH[step]], duty);
    set_duty(CH[STEP_INL[step]], PWM_DUTY_MAX);
}

typedef struct {
    int      step;
    int8_t   direction;
    uint32_t duty;
    uint32_t period_ms;
} MotorState;

static void state_reset(MotorState *s)
{
    s->duty      = RAMP_DUTY_START;
    s->period_ms = RAMP_PERIOD_MAX_MS;
}

void motor_task(void *arg)
{
    DRV8323 drv;
    ledc_init();
    drv8323_init(&drv, PIN_DRV_CS, PIN_DRV_EN, PIN_DRV_FAULT,
                 PIN_DRV_SCLK, PIN_DRV_MISO, PIN_DRV_MOSI);
    all_off();

    if (drv8323_has_fault(&drv)) {
        ESP_LOGE(TAG, "DRV8323 startup fault: 0x%03X 0x%03X",
                 drv8323_read_fault1(&drv), drv8323_read_fault2(&drv));
        vTaskDelete(NULL);
        return;
    }

    bootstrap_precharge();

    MotorState state = { .step = 0, .direction = 1 };
    state_reset(&state);

    Cmd     cmd           = { 0 };
    int     fault_counter = 0;
    int8_t  last_dir      = 0;

    while (1) {
        xQueueReceive(cmd_queue, &cmd, 0);

        if (xEventGroupGetBits(rc_events) & SAFE_MODE_BIT) {
            all_off();
            vTaskSuspend(NULL);
            continue;
        }

        int16_t throttle     = cmd.throttle;
        int     abs_throttle = throttle < 0 ? -throttle : throttle;

        if (throttle == 0) {
            all_off();
            state_reset(&state);
            last_dir = 0;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int8_t new_dir = throttle > 0 ? 1 : -1;
        if (new_dir != last_dir) {
            all_off();
            state_reset(&state);
            state.direction = new_dir;
            last_dir        = new_dir;
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        uint32_t target_duty = (uint32_t)abs_throttle * PWM_DUTY_MAX / 1000;
        if (target_duty < RAMP_DUTY_START) target_duty = RAMP_DUTY_START;

        uint32_t target_period = RAMP_PERIOD_MIN_MS +
            (uint32_t)(RAMP_PERIOD_MAX_MS - RAMP_PERIOD_MIN_MS) * (1000 - abs_throttle) / 1000;

        if (state.duty < target_duty) {
            state.duty += RAMP_DUTY_STEP;
            if (state.duty > target_duty) state.duty = target_duty;
        } else {
            state.duty = target_duty;
        }

        if (state.period_ms > target_period + RAMP_PERIOD_STEP_MS) {
            state.period_ms -= RAMP_PERIOD_STEP_MS;
        } else {
            state.period_ms = target_period;
        }

        apply_step(state.step, state.duty);
        state.step = (state.step + state.direction + 6) % 6;

        vTaskDelay(pdMS_TO_TICKS(state.period_ms));

        if (++fault_counter >= FAULT_CHECK_STEPS) {
            fault_counter = 0;
            if (drv8323_has_fault(&drv)) {
                ESP_LOGE(TAG, "DRV8323 fault: 0x%03X 0x%03X",
                         drv8323_read_fault1(&drv), drv8323_read_fault2(&drv));
                all_off();
                vTaskDelay(pdMS_TO_TICKS(500));
                drv8323_clear_faults(&drv);
                bootstrap_precharge();
                state_reset(&state);
                last_dir     = 0;
                cmd.throttle = 0;
            }
        }
    }
}
```

---

## Task 5: Build Verification

**Files:** none — verify only

- [ ] **Step 1: Build**

```bash
cd /home/leon/Documents/GitHub/ESP-RC-Car/Code
pio run -e espidf
```

Expected: `SUCCESS` with no errors. Warnings about unused variables are acceptable.

- [ ] **Step 2: Check serial log on flash (optional — requires hardware)**

Flash and monitor:
```bash
pio run -e espidf -t upload && pio device monitor -b 115200
```

Expected log on startup:
```
I (xxx) drv8323: DRV8323 regs: FAULT=000 VGS=000 CTRL=100 HS=3BF LS=6FF OCP=160 CSA=683
```

FAULT=000 and VGS=000 → no fault at startup. CTRL/HS/LS/OCP/CSA values confirm register write succeeded.
