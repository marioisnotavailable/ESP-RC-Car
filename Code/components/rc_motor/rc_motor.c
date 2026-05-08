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

// CH index: INH_A=0 INL_A=1 INH_B=2 INL_B=3 INH_C=4 INL_C=5
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

#ifdef SPI_TEST_ONLY
    drv8323_init(&drv, PIN_DRV_CS, PIN_DRV_EN, PIN_DRV_FAULT,
                 PIN_DRV_SCLK, PIN_DRV_MISO, PIN_DRV_MOSI);
    while (1) {
        ESP_LOGI(TAG, "--- DRV8323 SPI test ---");
        ESP_LOGI(TAG, "FAULT=%03X VGS=%03X CTRL=%03X HS=%03X LS=%03X OCP=%03X CSA=%03X",
                 drv8323_read_reg(&drv, ADR_FAULT_STAT),
                 drv8323_read_reg(&drv, ADR_VGS_STAT),
                 drv8323_read_reg(&drv, ADR_DRV_CTRL),
                 drv8323_read_reg(&drv, ADR_GATE_DRV_HS),
                 drv8323_read_reg(&drv, ADR_GATE_DRV_LS),
                 drv8323_read_reg(&drv, ADR_OCP_CTRL),
                 drv8323_read_reg(&drv, ADR_CSA_CTRL));
        ESP_LOGI(TAG, "FAULT pin: %d", gpio_get_level(PIN_DRV_FAULT));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
#endif

    ledc_init();
    drv8323_init(&drv, PIN_DRV_CS, PIN_DRV_EN, PIN_DRV_FAULT,
                 PIN_DRV_SCLK, PIN_DRV_MISO, PIN_DRV_MOSI);
    all_off();

#ifdef STEP_TEST
    if (drv8323_has_fault(&drv)) {
        ESP_LOGE(TAG, "DRV8323 startup fault: 0x%03X 0x%03X",
                 drv8323_read_fault1(&drv), drv8323_read_fault2(&drv));
    }
    bootstrap_precharge();
    uint32_t test_duty = PWM_DUTY_MAX / 2;
    while (1) {
        for (int s = 0; s < 6; s++) {
            ESP_LOGI(TAG, "STEP_TEST step=%d INH=%d INL=%d duty=%lu",
                     s, STEP_INH[s], STEP_INL[s], (unsigned long)test_duty);
            apply_step(s, test_duty);
            vTaskDelay(pdMS_TO_TICKS(800));
            if (drv8323_has_fault(&drv)) {
                ESP_LOGE(TAG, "FAULT step=%d: 0x%03X 0x%03X",
                         s, drv8323_read_fault1(&drv), drv8323_read_fault2(&drv));
                drv8323_clear_faults(&drv);
            }
        }
    }
#endif

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
            state.step      = 0;
            state.duty      = PWM_DUTY_MAX / 3;
            last_dir        = new_dir;
            apply_step(0, PWM_DUTY_MAX / 3);
            vTaskDelay(pdMS_TO_TICKS(500));
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

        if (state.duty >= (target_duty * 2) / 3) {
            if (state.period_ms > target_period + RAMP_PERIOD_STEP_MS) {
                state.period_ms -= RAMP_PERIOD_STEP_MS;
            } else {
                state.period_ms = target_period;
            }
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
