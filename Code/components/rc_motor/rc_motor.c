#include "rc_motor.h"
#include "drv8323.h"
#include "rc_common.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "motor";
static DRV8323 drv;

// 6-Step Kommutierungstabelle
// Werte: 1=PWM, 0=LOW, -1=FLOAT (wir setzen FLOAT auch auf 0 duty)
static const int8_t COMM_TABLE[6][6] = {
    { 1, 0,  0, 1, -1, -1},  // Step 0: A+, B-
    { 1, 0, -1,-1,  0, 1},   // Step 1: A+, C-
    {-1,-1,  1, 0,  0, 1},   // Step 2: B+, C-
    { 0, 1,  1, 0, -1,-1},   // Step 3: B+, A-
    { 0, 1, -1,-1,  1, 0},   // Step 4: C+, A-
    {-1,-1,  0, 1,  1, 0},   // Step 5: C+, B-
};

// Reihenfolge: INH_A, INL_A, INH_B, INL_B, INH_C, INL_C
static const ledc_channel_t CHANNELS[6] = {
    LEDC_CHANNEL_0, LEDC_CHANNEL_1,
    LEDC_CHANNEL_2, LEDC_CHANNEL_3,
    LEDC_CHANNEL_4, LEDC_CHANNEL_5,
};
static const int PINS[6] = {
    PIN_INHA, PIN_INLA, PIN_INHB, PIN_INLB, PIN_INHC, PIN_INLC
};

static void ledc_init(void) {
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = PWM_BITS,
        .freq_hz         = PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    for (int i = 0; i < 6; i++) {
        ledc_channel_config_t ch = {
            .gpio_num   = PINS[i],
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel    = CHANNELS[i],
            .timer_sel  = LEDC_TIMER_0,
            .duty       = 0,
            .hpoint     = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ch));
    }
}

static void all_phases_off(void) {
    for (int i = 0; i < 6; i++) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, CHANNELS[i], 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, CHANNELS[i]);
    }
}

static void apply_phase(int step, uint32_t duty) {
    for (int i = 0; i < 6; i++) {
        int8_t val = COMM_TABLE[step][i];
        uint32_t d = (val == 1) ? duty : 0;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, CHANNELS[i], d);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, CHANNELS[i]);
    }
}

static void handle_fault(void) {
    ESP_LOGW(TAG, "DRV8323 FAULT: 0x%03X / 0x%03X",
             drv8323_read_fault1(&drv), drv8323_read_fault2(&drv));
    all_phases_off();
    xEventGroupSetBits(rc_events, MOTOR_FAULT_BIT);
    vTaskDelay(pdMS_TO_TICKS(500));
    drv8323_clear_faults(&drv);
    xEventGroupClearBits(rc_events, MOTOR_FAULT_BIT);
}

void motor_task(void *arg) {
    // Safe Mode: Motor deaktiviert
    if (xEventGroupGetBits(rc_events) & SAFE_MODE_BIT) {
        ESP_LOGW(TAG, "Safe Mode — motor_task suspended");
        vTaskSuspend(NULL);
    }

    drv8323_init(&drv,
                 PIN_DRV_CS, PIN_DRV_EN, PIN_DRV_FAULT,
                 PIN_DRV_SCLK, PIN_DRV_MISO, PIN_DRV_MOSI);
    ledc_init();

    int step = 0;
    Cmd cmd = {0};

    while (1) {
        if (xEventGroupGetBits(rc_events) & SAFE_MODE_BIT) {
            all_phases_off();
            vTaskSuspend(NULL);
        }

        // Fault prüfen
        if (drv8323_has_fault(&drv)) {
            handle_fault();
            continue;
        }

        // Befehl lesen (non-blocking)
        xQueueReceive(cmd_queue, &cmd, 0);

        if (cmd.throttle == 0) {
            all_phases_off();
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Duty aus Throttle: |throttle| (0..1000) → duty (0..PWM_DUTY_MAX)
        int32_t abs_thr = cmd.throttle < 0 ? -cmd.throttle : cmd.throttle;
        uint32_t duty = ((uint32_t)abs_thr * PWM_DUTY_MAX) / 1000;

        // Richtung: >0 vorwärts, <0 rückwärts
        if (cmd.throttle > 0) {
            step = (step + 1) % 6;
        } else {
            step = (step + 5) % 6;
        }

        apply_phase(step, duty);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
