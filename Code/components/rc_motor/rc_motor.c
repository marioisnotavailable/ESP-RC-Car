#include "rc_motor.h"
#include "drv8323.h"
#include "driver/ledc.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"

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

// CH: INH_A=0 INL_A=1 INH_B=2 INL_B=3 INH_C=4 INL_C=5
// Motor B/C physically swapped — driver C drives motor B and vice versa
static const int STEP_INH[6] = { 0, 0, 4, 4, 2, 2 };
static const int STEP_INL[6] = { 5, 3, 3, 1, 1, 5 };

static void ledc_init(void)
{
    // GPIO3 is a strapping pin — force-reset before LEDC claims it
    gpio_reset_pin(GPIO_NUM_3);

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

static void commutation_delay(uint32_t period_us)
{
    uint64_t deadline = esp_timer_get_time() + period_us;
    while (1) {
        uint64_t now = esp_timer_get_time();
        if (now >= deadline) break;
        if (deadline - now >= 2000)
            vTaskDelay(1);
        else
            taskYIELD();
    }
}

static void apply_step(int step, uint32_t duty)
{
    all_off();
    set_duty(CH[STEP_INH[step]], duty);
    set_duty(CH[STEP_INL[step]], PWM_DUTY_MAX);
}

void motor_task(void *arg)
{
    DRV8323 drv;

#ifdef SPI_TEST_ONLY
    drv8323_init(&drv, PIN_DRV_CS, PIN_DRV_EN, PIN_DRV_FAULT,
                 PIN_DRV_SCLK, PIN_DRV_MISO, PIN_DRV_MOSI);
    while (1) {
        ESP_LOGI(TAG, "FAULT=%03X VGS=%03X",
                 drv8323_read_reg(&drv, ADR_FAULT_STAT),
                 drv8323_read_reg(&drv, ADR_VGS_STAT));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
#endif

    ledc_init();
    drv8323_init(&drv, PIN_DRV_CS, PIN_DRV_EN, PIN_DRV_FAULT,
                 PIN_DRV_SCLK, PIN_DRV_MISO, PIN_DRV_MOSI);
    all_off();

#ifdef STEP_TEST
    bootstrap_precharge();
    uint32_t test_duty = PWM_DUTY_MAX / 10;
    while (1) {
        for (int s = 0; s < 6; s++) {
            ESP_LOGI(TAG, "STEP %d  INH=%d INL=%d", s, STEP_INH[s], STEP_INL[s]);
            apply_step(s, test_duty);
            vTaskDelay(pdMS_TO_TICKS(3000));
            if (drv8323_has_fault(&drv)) {
                ESP_LOGE(TAG, "FAULT step=%d: 0x%03X 0x%03X", s,
                         drv8323_read_fault1(&drv), drv8323_read_fault2(&drv));
                drv8323_clear_faults(&drv);
            }
        }
    }
#endif

    if (drv8323_has_fault(&drv)) {
        ESP_LOGE(TAG, "Startup fault: 0x%03X 0x%03X",
                 drv8323_read_fault1(&drv), drv8323_read_fault2(&drv));
        vTaskDelete(NULL);
        return;
    }

    bootstrap_precharge();

    int      step          = 0;
    int8_t   direction     = 1;
    int8_t   last_dir      = 0;
    uint32_t duty          = 0;
    uint32_t period_us     = RAMP_START_US;
    int      fault_counter = 0;
    Cmd      cmd           = { 0 };

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
            period_us = RAMP_START_US;
            last_dir  = 0;
            step      = 0;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int8_t new_dir = throttle > 0 ? 1 : -1;

        if (new_dir != last_dir) {
            all_off();
            direction = new_dir;
            last_dir  = new_dir;
            period_us = RAMP_START_US;
            apply_step(0, PWM_DUTY_MAX * 4 / 10);
            vTaskDelay(pdMS_TO_TICKS(800));
            step = 0;
        }

        // Proportional formula: duty = target × (RAMP_END / period)
        // At RAMP_START=5000: duty=10.5% → T_motor=5145µs > 5000µs, field leads ✓
        // At RAMP_END=1500:   duty=35%   → T_motor=1802µs > 1500µs, field leads ✓
        uint32_t target_duty = (uint32_t)abs_throttle * PWM_DUTY_MAX / 1000;
        if (target_duty < PWM_DUTY_MAX * 60 / 100) target_duty = PWM_DUTY_MAX * 60 / 100;

        if (period_us > RAMP_END_US) {
            duty = target_duty * RAMP_END_US / period_us;
        } else {
            duty = target_duty;
        }

        apply_step(step, duty);
        step = (step + direction + 6) % 6;

        commutation_delay(period_us);

        if (period_us > RAMP_END_US) {
            period_us -= RAMP_STEP_US;
            if (period_us < RAMP_END_US) period_us = RAMP_END_US;
        }

        if (++fault_counter >= FAULT_CHECK_STEPS) {
            fault_counter = 0;
            if (drv8323_has_fault(&drv)) {
                ESP_LOGE(TAG, "Fault: 0x%03X 0x%03X",
                         drv8323_read_fault1(&drv), drv8323_read_fault2(&drv));
                all_off();
                vTaskDelay(pdMS_TO_TICKS(200));
                drv8323_clear_faults(&drv);
                bootstrap_precharge();
                period_us = RAMP_START_US;
            }
        }
    }
}
