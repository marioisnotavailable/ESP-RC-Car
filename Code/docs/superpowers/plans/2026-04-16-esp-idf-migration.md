# ESP-IDF Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Komplette Firmware-Neuentwicklung auf ESP-IDF mit PlatformIO, strukturiert als 4 FreeRTOS Tasks (motor, comms, system, bg) mit Queue/EventGroup Kommunikation.

**Architecture:** `app_main` initialisiert NVS/WiFi/Queues/EventGroup und spawnt 4 Tasks. `motor_task` (Prio 10) liest Befehle aus `cmd_queue` und steuert BLDC via LEDC + DRV8323 SPI. `comms_task` (Prio 7) betreibt WebSocket-Server und UDP-Discovery. `system_task` (Prio 5) misst Batterie-ADC, verwaltet Settings/Recovery/Steering. `bg_task` (Prio 2) prüft FOTA und Serial-Console.

**Tech Stack:** ESP-IDF 5.x, PlatformIO framework=espidf, FreeRTOS, esp_http_server (WebSocket), esp_https_ota, adc_oneshot, ledc, spi_master, nvs_flash, lwip sockets, esp_littlefs

---

## File Structure

```
Code/
├── platformio.ini                        MODIFY: framework=espidf, remove Arduino lib_deps
├── sdkconfig.defaults                    MODIFY: add required Kconfig options
├── partitions.csv                        KEEP unchanged
├── src/
│   └── main.c                            CREATE: app_main, init sequence, task spawning
└── components/
    ├── rc_common/
    │   ├── CMakeLists.txt                CREATE
    │   ├── include/rc_common.h           CREATE: Cmd struct, queue/event handles, shared externs
    │   └── rc_common.c                   CREATE: queue/eventgroup creation, pin definitions
    ├── rc_motor/
    │   ├── CMakeLists.txt                CREATE
    │   ├── include/rc_motor.h            CREATE
    │   ├── rc_motor.c                    CREATE: motor_task, 6-step commutation, LEDC
    │   ├── include/drv8323.h             CREATE
    │   └── drv8323.c                     CREATE: SPI init, register read/write, fault handling
    ├── rc_comms/
    │   ├── CMakeLists.txt                CREATE
    │   ├── include/rc_comms.h            CREATE
    │   └── rc_comms.c                    CREATE: comms_task, WiFi, WebSocket, UDP Discovery
    ├── rc_system/
    │   ├── CMakeLists.txt                CREATE
    │   ├── include/rc_system.h           CREATE
    │   └── rc_system.c                   CREATE: system_task, ADC, NVS settings, recovery, steering
    ├── rc_bg/
    │   ├── CMakeLists.txt                CREATE
    │   ├── include/rc_bg.h               CREATE
    │   └── rc_bg.c                       CREATE: bg_task, FOTA, serial console
    └── esp_littlefs/                     CLONE: joltwallet/esp_littlefs from GitHub
```

---

## Phase 1: Projekt-Setup

### Task 1: platformio.ini auf ESP-IDF umstellen

**Files:**
- Modify: `Code/platformio.ini`
- Modify: `Code/sdkconfig.defaults`

- [ ] **Schritt 1: platformio.ini ersetzen**

```ini
[env:espidf]
platform = espressif32
board = esp32-s3-devkitc-1
framework = espidf
board_build.flash_size = 16MB
board_upload.flash_size = 16MB
board_build.arduino.memory_type = qio_opi
board_build.partitions = partitions.csv
monitor_speed = 115200
build_flags =
    -DOTA_PASSWORD=\"esprccar\"

[env:espidf_ota]
extends = env:espidf
upload_protocol = espota
upload_port = esp-rc-car.local
upload_flags =
    --auth=esprccar

[env:espidf_factory]
extends = env:espidf
build_flags =
    -DFACTORY_BUILD
    -DOTA_PASSWORD=\"esprccar\"
```

- [ ] **Schritt 2: sdkconfig.defaults aktualisieren**

```
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP_TASK_WDT_EN=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
CONFIG_HTTPD_WS_SUPPORT=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_NVS_ALLOW_PAGE_SIZE_64=y
CONFIG_SPIRAM_SUPPORT=y
CONFIG_ESP32S3_SPIRAM_SUPPORT=y
```

- [ ] **Schritt 3: Build prüfen (wird noch fehlschlagen — kein src/main.c)**

```bash
cd Code && pio run -e espidf 2>&1 | head -30
```

Expected: Fehler "No such file src/main.c" oder ähnlich — das ist OK.

---

### Task 2: esp_littlefs als Komponente hinzufügen

**Files:**
- Create: `Code/components/esp_littlefs/` (geclont)

- [ ] **Schritt 1: esp_littlefs in components/ klonen**

```bash
cd Code/components && git clone https://github.com/joltwallet/esp_littlefs.git
```

- [ ] **Schritt 2: Prüfen ob CMakeLists.txt vorhanden**

```bash
ls Code/components/esp_littlefs/CMakeLists.txt
```

Expected: Datei existiert.

---

### Task 3: rc_common Komponente erstellen

**Files:**
- Create: `Code/components/rc_common/CMakeLists.txt`
- Create: `Code/components/rc_common/include/rc_common.h`
- Create: `Code/components/rc_common/rc_common.c`

- [ ] **Schritt 1: CMakeLists.txt erstellen**

```cmake
idf_component_register(
    SRCS "rc_common.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos
)
```

- [ ] **Schritt 2: rc_common.h erstellen**

```c
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"

// ── Shared Command struct ──
typedef struct {
    int16_t throttle;   // -1000 .. +1000
    int16_t steer;      // -1000 .. +1000
    uint8_t flags;
} Cmd;

// ── EventGroup Bits ──
#define WIFI_CONNECTED_BIT  BIT0
#define SAFE_MODE_BIT       BIT1
#define MOTOR_FAULT_BIT     BIT2

// ── Global Handles ──
extern QueueHandle_t       cmd_queue;
extern QueueHandle_t       batt_queue;
extern EventGroupHandle_t  rc_events;

// ── Pin Definitions ──
// DRV8323S SPI
#define PIN_DRV_MISO    5
#define PIN_DRV_MOSI    6
#define PIN_DRV_SCLK    7
#define PIN_DRV_EN      16
#define PIN_DRV_CS      15
#define PIN_DRV_FAULT   39

// Motor PWM (3-Phasen)
#define PIN_INHA   18
#define PIN_INLA   8
#define PIN_INHB   3
#define PIN_INLB   9
#define PIN_INHC   10
#define PIN_INLC   11

// Servo (war GPIO5 — Konflikt behoben)
#define PIN_SERVO       4

// Batterie ADC
#define BATT_ADC_CHANNEL  ADC_CHANNEL_0   // GPIO1

// Charge restart
#define PIN_CHARGE_RESTART  47

// ── Init ──
void rc_common_init(void);
```

- [ ] **Schritt 3: rc_common.c erstellen**

```c
#include "rc_common.h"

QueueHandle_t       cmd_queue;
QueueHandle_t       batt_queue;
EventGroupHandle_t  rc_events;

void rc_common_init(void) {
    cmd_queue  = xQueueCreate(1, sizeof(Cmd));  // length=1: xQueueOverwrite braucht das
    batt_queue = xQueueCreate(1, sizeof(int));
    rc_events  = xEventGroupCreate();

    configASSERT(cmd_queue);
    configASSERT(batt_queue);
    configASSERT(rc_events);
}
```

---

### Task 4: src/main.c Skeleton erstellen

**Files:**
- Create: `Code/src/main.c`

- [ ] **Schritt 1: main.c Skeleton schreiben**

```c
#include <stdio.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rc_common.h"

static const char *TAG = "main";

// Forward declarations (implementiert in jeweiliger Komponente)
void motor_task(void *arg);
void comms_task(void *arg);
void system_task(void *arg);
void bg_task(void *arg);

void app_main(void) {
    // NVS initialisieren
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Netzwerk-Stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Shared Queues + EventGroup
    rc_common_init();

    ESP_LOGI(TAG, "ESP-RC-Car starting...");

    // Tasks spawnen
    xTaskCreate(motor_task,  "motor",  4096,  NULL, 10, NULL);
    xTaskCreate(comms_task,  "comms",  8192,  NULL, 7,  NULL);
    xTaskCreate(system_task, "system", 6144,  NULL, 5,  NULL);
    xTaskCreate(bg_task,     "bg",     6144,  NULL, 2,  NULL);

    vTaskDelete(NULL);
}
```

- [ ] **Schritt 2: Build prüfen**

```bash
cd Code && pio run -e espidf 2>&1 | tail -20
```

Expected: Fehler über fehlende Komponenten (motor_task etc.) — OK bis Phase 2.

---

## Phase 2: rc_motor Komponente

### Task 5: DRV8323 SPI Treiber (ESP-IDF)

**Files:**
- Create: `Code/components/rc_motor/CMakeLists.txt`
- Create: `Code/components/rc_motor/include/drv8323.h`
- Create: `Code/components/rc_motor/drv8323.c`

- [ ] **Schritt 1: CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "rc_motor.c" "drv8323.c"
    INCLUDE_DIRS "include"
    REQUIRES rc_common driver esp_driver_ledc esp_driver_spi
)
```

- [ ] **Schritt 2: drv8323.h**

```c
#pragma once
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

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

- [ ] **Schritt 3: drv8323.c**

```c
#include "drv8323.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "drv8323";

esp_err_t drv8323_init(DRV8323 *drv,
                        gpio_num_t cs, gpio_num_t en, gpio_num_t fault,
                        gpio_num_t sclk, gpio_num_t miso, gpio_num_t mosi) {
    drv->en_pin    = en;
    drv->fault_pin = fault;

    // EN und nFAULT Pins konfigurieren
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << en),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    io.pin_bit_mask = (1ULL << fault);
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    gpio_config(&io);

    // SPI Bus initialisieren
    spi_bus_config_t buscfg = {
        .miso_io_num   = miso,
        .mosi_io_num   = mosi,
        .sclk_io_num   = sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // DRV8323 als SPI Device
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1000000,
        .mode           = 1,
        .spics_io_num   = cs,
        .queue_size     = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &drv->spi));

    // Driver aufwecken: EN low → high
    gpio_set_level(en, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(en, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "DRV8323 initialized");
    return ESP_OK;
}

static uint16_t transfer_frame(DRV8323 *drv, uint16_t frame) {
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = &frame,
        .rx_buffer = NULL,
        .flags     = SPI_TRANS_USE_RXDATA,
    };
    // Frame muss big-endian sein
    uint16_t tx = __builtin_bswap16(frame);
    t.tx_buffer = &tx;
    spi_device_transmit(drv->spi, &t);
    return __builtin_bswap16(*(uint16_t*)t.rx_data);
}

uint16_t drv8323_read_reg(DRV8323 *drv, uint8_t reg) {
    uint16_t frame = (1 << 15) | ((reg & 0x0F) << 11);
    return transfer_frame(drv, frame) & 0x7FF;
}

esp_err_t drv8323_write_reg(DRV8323 *drv, uint8_t reg, uint16_t val) {
    uint16_t frame = ((reg & 0x0F) << 11) | (val & 0x7FF);
    transfer_frame(drv, frame);
    return ESP_OK;
}

bool drv8323_has_fault(DRV8323 *drv) {
    return gpio_get_level(drv->fault_pin) == 0;
}

uint16_t drv8323_read_fault1(DRV8323 *drv) { return drv8323_read_reg(drv, 0x00); }
uint16_t drv8323_read_fault2(DRV8323 *drv) { return drv8323_read_reg(drv, 0x01); }

esp_err_t drv8323_clear_faults(DRV8323 *drv) {
    gpio_set_level(drv->en_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(drv->en_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}
```

---

### Task 6: rc_motor Task (LEDC + 6-Step Kommutierung)

**Files:**
- Create: `Code/components/rc_motor/include/rc_motor.h`
- Create: `Code/components/rc_motor/rc_motor.c`

- [ ] **Schritt 1: rc_motor.h**

```c
#pragma once
#include "rc_common.h"

#define PWM_FREQ_HZ    20000
#define PWM_BITS       LEDC_TIMER_10_BIT
#define PWM_DUTY_MAX   1023
#define PWM_DUTY_MID   512

void motor_task(void *arg);
```

- [ ] **Schritt 2: rc_motor.c**

```c
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

// 6-Step Kommutierungstabelle: {INH_A, INL_A, INH_B, INL_B, INH_C, INL_C}
// 1=PWM, 0=LOW, -1=FLOAT
static const int8_t COMM_TABLE[6][6] = {
    { 1, 0,  0, 1, -1, -1},  // Step 0: A+, B-
    { 1, 0, -1,-1,  0, 1},   // Step 1: A+, C-
    {-1,-1,  1, 0,  0, 1},   // Step 2: B+, C-
    { 0, 1,  1, 0, -1,-1},   // Step 3: B+, A-
    { 0, 1, -1,-1,  1, 0},   // Step 4: C+, A-
    {-1,-1,  0, 1,  1, 0},   // Step 5: C+, B-
};

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
    // Safe Mode check
    EventBits_t bits = xEventGroupGetBits(rc_events);
    if (bits & SAFE_MODE_BIT) {
        ESP_LOGW(TAG, "Safe Mode — motor_task suspended");
        vTaskSuspend(NULL);
    }

    // DRV8323 initialisieren
    drv8323_init(&drv,
                 PIN_DRV_CS, PIN_DRV_EN, PIN_DRV_FAULT,
                 PIN_DRV_SCLK, PIN_DRV_MISO, PIN_DRV_MOSI);

    ledc_init();

    int step = 0;
    Cmd cmd = {0};

    while (1) {
        // Fault prüfen
        if (drv8323_has_fault(&drv)) {
            handle_fault();
            continue;
        }

        // Befehl lesen (non-blocking)
        xQueueReceive(cmd_queue, &cmd, 0);

        // Throttle 0 → alles aus
        if (cmd.throttle == 0) {
            all_phases_off();
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Duty aus Throttle berechnen (0..1000 → 0..PWM_DUTY_MAX)
        int32_t abs_throttle = cmd.throttle < 0 ? -cmd.throttle : cmd.throttle;
        uint32_t duty = (abs_throttle * PWM_DUTY_MAX) / 1000;

        // Richtung: throttle > 0 = vorwärts, < 0 = rückwärts (step rückwärts)
        if (cmd.throttle > 0) {
            step = (step + 1) % 6;
        } else {
            step = (step + 5) % 6;
        }

        apply_phase(step, duty);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

- [ ] **Schritt 3: Build prüfen**

```bash
cd Code && pio run -e espidf 2>&1 | grep -E "error:|warning:|Compiling"
```

Expected: rc_motor kompiliert ohne Errors.

---

## Phase 3: rc_comms Komponente

### Task 7: WiFi + UDP Discovery

**Files:**
- Create: `Code/components/rc_comms/CMakeLists.txt`
- Create: `Code/components/rc_comms/include/rc_comms.h`
- Create: `Code/components/rc_comms/rc_comms.c` (WiFi + UDP Teil)

- [ ] **Schritt 1: CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "rc_comms.c"
    INCLUDE_DIRS "include"
    REQUIRES rc_common esp_wifi esp_http_server lwip nvs_flash esp_event
)
```

- [ ] **Schritt 2: rc_comms.h**

```c
#pragma once
#include "rc_common.h"
#include "esp_http_server.h"

#define WS_PORT           80
#define DISCOVERY_PORT    49352
#define DISCOVERY_QUERY   "ESP_RC_DISCOVER"
#define DISCOVERY_RESP    "ESP_RC_HERE "

#define WIFI_CONNECT_TIMEOUT_MS  30000
#define FAILSAFE_DEFAULT_MS      500

void comms_task(void *arg);
```

- [ ] **Schritt 3: WiFi Initialisierung in rc_comms.c**

```c
#include "rc_comms.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "comms";
static uint32_t last_cmd_ms = 0;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(rc_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(rc_events, WIFI_CONNECTED_BIT);
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "WiFi connected, IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));
    }
}

static bool wifi_connect(void) {
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                wifi_event_handler, NULL);

    // Gespeicherte SSIDs aus NVS laden (vereinfacht: erste SSID)
    nvs_handle_t nvs;
    char ssid[33] = {0}, pass[65] = {0};
    if (nvs_open("wifi", NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(ssid);
        nvs_get_str(nvs, "ssid0", ssid, &len);
        len = sizeof(pass);
        nvs_get_str(nvs, "pass0", pass, &len);
        nvs_close(nvs);
    }

    if (strlen(ssid) == 0) {
        ESP_LOGW(TAG, "No WiFi credentials — starting AP");
        return false;
    }

    wifi_config_t wifi_cfg = {0};
    strlcpy((char*)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char*)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();

    // Warten bis verbunden oder Timeout
    EventBits_t bits = xEventGroupWaitBits(rc_events, WIFI_CONNECTED_BIT,
                                            pdFALSE, pdTRUE,
                                            pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

static void udp_discovery_task(void *arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(DISCOVERY_PORT),
        .sin_addr.s_addr = INADDR_ANY,
    };
    bind(sock, (struct sockaddr*)&addr, sizeof(addr));

    char buf[64];
    struct sockaddr_in client;
    socklen_t clen = sizeof(client);

    while (1) {
        int len = recvfrom(sock, buf, sizeof(buf)-1, 0,
                           (struct sockaddr*)&client, &clen);
        if (len <= 0) continue;
        buf[len] = '\0';

        if (strcmp(buf, DISCOVERY_QUERY) == 0) {
            esp_netif_ip_info_t ip_info;
            esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"),
                                  &ip_info);
            char resp[32];
            snprintf(resp, sizeof(resp), DISCOVERY_RESP IPSTR,
                     IP2STR(&ip_info.ip));
            sendto(sock, resp, strlen(resp), 0,
                   (struct sockaddr*)&client, clen);
        }
    }
}
```

---

### Task 8: WebSocket Server + Failsafe

**Files:**
- Modify: `Code/components/rc_comms/rc_comms.c` (WS + comms_task hinzufügen)

- [ ] **Schritt 1: WebSocket Handler und comms_task an rc_comms.c anhängen**

```c
// WebSocket Handler
static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WS client connected");
        return ESP_OK;
    }

    httpd_ws_frame_t pkt = {.type = HTTPD_WS_TYPE_TEXT};
    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK || pkt.len == 0) return ret;

    uint8_t *buf = calloc(1, pkt.len + 1);
    pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &pkt, pkt.len);
    if (ret != ESP_OK) { free(buf); return ret; }

    // JSON parsen: {"throttle": X, "steer": Y}
    cJSON *json = cJSON_ParseWithLength((char*)buf, pkt.len);
    if (json) {
        Cmd cmd = {0};
        cJSON *t = cJSON_GetObjectItem(json, "throttle");
        cJSON *s = cJSON_GetObjectItem(json, "steer");
        if (cJSON_IsNumber(t)) cmd.throttle = (int16_t)t->valueint;
        if (cJSON_IsNumber(s)) cmd.steer    = (int16_t)s->valueint;
        xQueueOverwrite(cmd_queue, &cmd);
        last_cmd_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
        cJSON_Delete(json);
    }
    free(buf);
    return ESP_OK;
}

static httpd_handle_t start_websocket_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WS_PORT;
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    httpd_uri_t ws_uri = {
        .uri          = "/ws",
        .method       = HTTP_GET,
        .handler      = ws_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(server, &ws_uri);
    return server;
}

static void check_failsafe(uint32_t failsafe_ms) {
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (last_cmd_ms > 0 && (now - last_cmd_ms) > failsafe_ms) {
        Cmd zero = {0};
        xQueueOverwrite(cmd_queue, &zero);
        last_cmd_ms = now;  // Nicht wiederholt senden
    }
}

// Batterie % an WS Client senden
static void broadcast_battery(httpd_handle_t server) {
    int pct = 0;
    if (xQueuePeek(batt_queue, &pct, 0) != pdTRUE) return;

    char json[32];
    snprintf(json, sizeof(json), "{\"batt\":%d}", pct);
    httpd_ws_frame_t pkt = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t*)json,
        .len     = strlen(json),
    };
    // An alle verbundenen Clients senden
    size_t clients = 10;
    int   fds[10];
    httpd_get_client_list(server, &clients, fds);
    for (size_t i = 0; i < clients; i++) {
        if (httpd_ws_get_fd_info(server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(server, fds[i], &pkt);
        }
    }
}

void comms_task(void *arg) {
    bool connected = wifi_connect();
    if (!connected) {
        ESP_LOGW(TAG, "No WiFi — only UDP Discovery disabled");
    }

    // UDP Discovery in eigenem Task
    if (connected) {
        xTaskCreate(udp_discovery_task, "udp", 4096, NULL, 4, NULL);
    }

    httpd_handle_t server = start_websocket_server();
    uint32_t batt_last_ms = 0;

    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Failsafe: 500ms ohne Kommando → stop
        check_failsafe(500);

        // Batterie jede Sekunde broadcasten
        if (now - batt_last_ms > 1000) {
            broadcast_battery(server);
            batt_last_ms = now;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
```

- [ ] **Schritt 2: Build prüfen**

```bash
cd Code && pio run -e espidf 2>&1 | grep -E "^.*error:"
```

Expected: Keine Errors in rc_comms.

---

## Phase 4: rc_system Komponente

### Task 9: NVS Settings

**Files:**
- Create: `Code/components/rc_system/CMakeLists.txt`
- Create: `Code/components/rc_system/include/rc_system.h`
- Create: `Code/components/rc_system/rc_system.c`

- [ ] **Schritt 1: CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "rc_system.c"
    INCLUDE_DIRS "include"
    REQUIRES rc_common nvs_flash esp_driver_ledc esp_adc esp_littlefs
             esp_task_wdt driver
)
```

- [ ] **Schritt 2: rc_system.h**

```c
#pragma once
#include "rc_common.h"

typedef struct {
    bool     ota_enabled;
    uint32_t ota_interval_ms;
    uint8_t  wifi_tx_power;
    uint16_t failsafe_ms;
    uint32_t beacon_interval_ms;
    char     ap_prefix[32];
    bool     always_start_panel;
    bool     steer_invert;
    float    steer_gain;
    uint16_t steer_deadzone;
    float    steer_filter;
    float    batt_warn_v;
    float    batt_off_v;
    uint8_t  max_throttle_pct;
    float    adc_corr_factor;
} DeviceSettings;

extern DeviceSettings settings;
extern volatile int battery_percent;

void rc_settings_load(void);
void rc_settings_save(void);
void rc_recovery_check(void);
void rc_recovery_mark_stable(void);
void system_task(void *arg);
```

- [ ] **Schritt 3: Settings + Recovery in rc_system.c**

```c
#include "rc_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "system";

DeviceSettings settings = {
    .ota_enabled        = true,
    .ota_interval_ms    = 300000,
    .wifi_tx_power      = 3,
    .failsafe_ms        = 500,
    .beacon_interval_ms = 1000,
    .ap_prefix          = "ESP-RC-",
    .always_start_panel = false,
    .steer_invert       = false,
    .steer_gain         = 1.0f,
    .steer_deadzone     = 50,
    .steer_filter       = 0.7f,
    .batt_warn_v        = 7.6f,
    .batt_off_v         = 7.5f,
    .max_throttle_pct   = 100,
    .adc_corr_factor    = 1.0f,
};

volatile int battery_percent = 0;

void rc_settings_load(void) {
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READONLY, &h) != ESP_OK) return;

    uint8_t u8;
    uint32_t u32;
    float f;
    size_t len;

    if (nvs_get_u8(h,  "ota_en",    &u8)  == ESP_OK) settings.ota_enabled      = u8;
    if (nvs_get_u32(h, "ota_iv",    &u32) == ESP_OK) settings.ota_interval_ms  = u32;
    if (nvs_get_u8(h,  "tx_pwr",    &u8)  == ESP_OK) settings.wifi_tx_power    = u8;
    if (nvs_get_u32(h, "fs_ms",     &u32) == ESP_OK) settings.failsafe_ms      = u32;
    if (nvs_get_u8(h,  "steer_inv", &u8)  == ESP_OK) settings.steer_invert     = u8;
    if (nvs_get_u8(h,  "max_thr",   &u8)  == ESP_OK) settings.max_throttle_pct = u8;

    // floats als blob speichern
    len = sizeof(float);
    nvs_get_blob(h, "adc_corr", &f, &len); if (len == sizeof(float)) settings.adc_corr_factor = f;
    nvs_get_blob(h, "batt_warn",&f, &len); if (len == sizeof(float)) settings.batt_warn_v = f;
    nvs_get_blob(h, "batt_off", &f, &len); if (len == sizeof(float)) settings.batt_off_v  = f;

    nvs_close(h);
    ESP_LOGI(TAG, "Settings loaded from NVS");
}

void rc_settings_save(void) {
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h,  "ota_en",    settings.ota_enabled);
    nvs_set_u32(h, "ota_iv",    settings.ota_interval_ms);
    nvs_set_u8(h,  "tx_pwr",    settings.wifi_tx_power);
    nvs_set_u32(h, "fs_ms",     settings.failsafe_ms);
    nvs_set_u8(h,  "steer_inv", settings.steer_invert);
    nvs_set_u8(h,  "max_thr",   settings.max_throttle_pct);
    nvs_set_blob(h, "adc_corr", &settings.adc_corr_factor, sizeof(float));
    nvs_set_blob(h, "batt_warn",&settings.batt_warn_v, sizeof(float));
    nvs_set_blob(h, "batt_off", &settings.batt_off_v,  sizeof(float));
    nvs_commit(h);
    nvs_close(h);
}

// ── Recovery ──
#define CRASH_THRESHOLD  3
#define BOOT_STABLE_MS   30000

void rc_recovery_check(void) {
    nvs_handle_t h;
    if (nvs_open("recovery", NVS_READWRITE, &h) != ESP_OK) return;

    uint8_t count = 0;
    nvs_get_u8(h, "crash_cnt", &count);
    count++;
    nvs_set_u8(h, "crash_cnt", count);
    nvs_commit(h);
    nvs_close(h);

    if (count >= CRASH_THRESHOLD) {
        ESP_LOGW(TAG, "Crash count %d >= %d — entering Safe Mode", count, CRASH_THRESHOLD);
        xEventGroupSetBits(rc_events, SAFE_MODE_BIT);
    }
}

void rc_recovery_mark_stable(void) {
    nvs_handle_t h;
    if (nvs_open("recovery", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "crash_cnt", 0);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "System stable — crash counter reset");
}
```

---

### Task 10: Battery ADC + Steering LEDC + system_task

**Files:**
- Modify: `Code/components/rc_system/rc_system.c` (system_task anhängen)

- [ ] **Schritt 1: ADC + Steering + system_task an rc_system.c anhängen**

```c
// ── Battery ADC ──
#define BATT_MAX_V   8.39f
#define BATT_MIN_V   7.50f
#define SAMPLE_COUNT 500

static adc_oneshot_unit_handle_t adc_handle;

static void adc_init(void) {
    adc_oneshot_unit_init_cfg_t init = {.unit_id = ADC_UNIT_1};
    adc_oneshot_new_unit(&init, &adc_handle);

    adc_oneshot_chan_cfg_t cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,
    };
    adc_oneshot_config_channel(adc_handle, BATT_ADC_CHANNEL, &cfg);
}

static int read_battery_percent(void) {
    int32_t sum = 0;
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        int raw;
        adc_oneshot_read(adc_handle, BATT_ADC_CHANNEL, &raw);
        sum += raw;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    float avg_raw = (float)sum / SAMPLE_COUNT;
    // ADC raw → Volt (ESP32-S3, 12-bit, Vref~3.1V, Voltage divider factor aus corr)
    float v_adc = (avg_raw / 4095.0f) * 3.1f * settings.adc_corr_factor;
    float v_batt = v_adc * 3.0f;  // Spannungsteiler 1:3 (anpassen nach PCB)
    float pct = (v_batt - BATT_MIN_V) / (BATT_MAX_V - BATT_MIN_V) * 100.0f;
    if (pct > 100.0f) pct = 100.0f;
    if (pct < 0.0f)   pct = 0.0f;
    return (int)pct;
}

// ── Steering LEDC ──
#define SERVO_FREQ_HZ   50
#define SERVO_MIN_US    1000
#define SERVO_MAX_US    2000
#define SERVO_MID_US    1500

static void steering_init(void) {
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_12_BIT,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .gpio_num   = PIN_SERVO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_6,
        .timer_sel  = LEDC_TIMER_1,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);
}

static void steering_apply(int16_t steer_input) {
    // steer_input: -1000..+1000 → 1000..2000 µs
    float gain   = settings.steer_invert ? -settings.steer_gain : settings.steer_gain;
    float scaled = steer_input * gain;
    if (scaled >  1000.0f) scaled =  1000.0f;
    if (scaled < -1000.0f) scaled = -1000.0f;

    int us = SERVO_MID_US + (int)(scaled * 0.5f);  // 0.5 = 500µs / 1000
    // µs → LEDC duty (12-bit, 50Hz → Periode 20ms = 20000µs)
    uint32_t duty = (uint32_t)((us * 4095UL) / 20000UL);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_6, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_6);
}

void system_task(void *arg) {
    adc_init();

    EventBits_t bits = xEventGroupGetBits(rc_events);
    if (!(bits & SAFE_MODE_BIT)) {
        steering_init();
    }

    esp_task_wdt_add(NULL);

    uint32_t stable_start = xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool marked_stable = false;
    Cmd cmd = {0};

    while (1) {
        esp_task_wdt_reset();

        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Batterie messen (blockiert ~1s wegen 500 Samples × 2ms)
        int pct = read_battery_percent();
        battery_percent = pct;
        xQueueOverwrite(batt_queue, &pct);

        // Lenkung anwenden
        if (!(xEventGroupGetBits(rc_events) & SAFE_MODE_BIT)) {
            if (xQueuePeek(cmd_queue, &cmd, 0) == pdTRUE) {
                steering_apply(cmd.steer);
            }
        }

        // Stabilitäts-Timer
        if (!marked_stable && (now - stable_start) > BOOT_STABLE_MS) {
            rc_recovery_mark_stable();
            marked_stable = true;
        }

        // Unterspannung check
        if (pct == 0) {
            ESP_LOGW(TAG, "Battery critical! Deep sleep...");
            // esp_deep_sleep_start();  // aktivieren wenn gewünscht
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

- [ ] **Schritt 2: Build prüfen**

```bash
cd Code && pio run -e espidf 2>&1 | grep -E "^.*error:"
```

Expected: Keine Errors in rc_system.

---

## Phase 5: rc_bg Komponente

### Task 11: FOTA + Serial Console

**Files:**
- Create: `Code/components/rc_bg/CMakeLists.txt`
- Create: `Code/components/rc_bg/include/rc_bg.h`
- Create: `Code/components/rc_bg/rc_bg.c`

- [ ] **Schritt 1: CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "rc_bg.c"
    INCLUDE_DIRS "include"
    REQUIRES rc_common rc_system esp_https_ota esp_http_client
             esp_tls json esp_ota_ops
)
```

- [ ] **Schritt 2: rc_bg.h**

```c
#pragma once
#include "rc_common.h"

#define FOTA_API_URL  "https://api.github.com/repos/marioisnotavailable/ESP-RC-Car/releases/latest"
#define FOTA_FW_URL   "https://github.com/marioisnotavailable/ESP-RC-Car/releases/latest/download/esp-rc-car.bin"

void bg_task(void *arg);
```

- [ ] **Schritt 3: rc_bg.c**

```c
#include "rc_bg.h"
#include "rc_system.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "bg";

static void do_fota(void) {
    ESP_LOGI(TAG, "Checking for firmware update...");

    esp_http_client_config_t http_cfg = {
        .url                    = FOTA_FW_URL,
        .skip_cert_common_name_check = true,
    };
    esp_https_ota_config_t ota_cfg = {.http_config = &http_cfg};

    esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA success — restarting");
        esp_restart();
    } else if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "Firmware up to date");
    } else {
        ESP_LOGW(TAG, "OTA failed: %s", esp_err_to_name(ret));
    }
}

static void console_task(void *arg) {
    char buf[128];
    int pos = 0;
    while (1) {
        int c = getchar();
        if (c == EOF) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        if (c == '\r' || c == '\n') {
            buf[pos] = '\0';
            if (pos > 0) {
                if (strcmp(buf, "status") == 0) {
                    ESP_LOGI(TAG, "Battery: %d%%", battery_percent);
                    EventBits_t bits = xEventGroupGetBits(rc_events);
                    ESP_LOGI(TAG, "Safe mode: %s", (bits & SAFE_MODE_BIT) ? "YES" : "NO");
                    ESP_LOGI(TAG, "WiFi: %s", (bits & WIFI_CONNECTED_BIT) ? "connected" : "disconnected");
                } else if (strcmp(buf, "reset") == 0) {
                    esp_restart();
                } else if (strcmp(buf, "ota") == 0) {
                    do_fota();
                } else {
                    ESP_LOGI(TAG, "Commands: status, reset, ota");
                }
            }
            pos = 0;
        } else if (pos < (int)sizeof(buf) - 1) {
            buf[pos++] = c;
        }
    }
}

void bg_task(void *arg) {
    // OTA nach dem Boot als gültig markieren
    esp_ota_mark_app_valid_cancel_rollback();

    // Serial Console
    xTaskCreate(console_task, "console", 4096, NULL, 1, NULL);

    uint32_t last_ota_check = 0;

    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Warten bis WiFi verbunden
        xEventGroupWaitBits(rc_events, WIFI_CONNECTED_BIT,
                            pdFALSE, pdTRUE, portMAX_DELAY);

        // FOTA periodisch prüfen
        if (settings.ota_enabled &&
            (now - last_ota_check) > settings.ota_interval_ms) {
            do_fota();
            last_ota_check = now;
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
```

---

## Phase 6: Integration & Full Build

### Task 12: app_main finalisieren + alle Komponenten verdrahten

**Files:**
- Modify: `Code/src/main.c` (rc_system + rc_bg includes hinzufügen)

- [ ] **Schritt 1: main.c mit allen Komponenten aktualisieren**

```c
#include <stdio.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rc_common.h"
#include "rc_motor.h"
#include "rc_comms.h"
#include "rc_system.h"
#include "rc_bg.h"

static const char *TAG = "main";

void app_main(void) {
    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Netzwerk-Stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Shared Ressourcen
    rc_common_init();

    // Recovery prüfen (vor allen anderen Inits)
    rc_recovery_check();

    // Settings laden
    rc_settings_load();

    ESP_LOGI(TAG, "ESP-RC-Car (ESP-IDF) starting...");
    EventBits_t bits = xEventGroupGetBits(rc_events);
    if (bits & SAFE_MODE_BIT) {
        ESP_LOGW(TAG, "*** SAFE MODE ACTIVE ***");
    }

    // Tasks spawnen
    xTaskCreate(motor_task,  "motor",  4096, NULL, 10, NULL);
    xTaskCreate(comms_task,  "comms",  8192, NULL, 7,  NULL);
    xTaskCreate(system_task, "system", 6144, NULL, 5,  NULL);
    xTaskCreate(bg_task,     "bg",     6144, NULL, 2,  NULL);

    vTaskDelete(NULL);
}
```

- [ ] **Schritt 2: Vollständigen Build durchführen**

```bash
cd Code && pio run -e espidf 2>&1
```

Expected: `SUCCESS` — keine Errors.

- [ ] **Schritt 3: Firmware flashen**

```bash
cd Code && pio run -e espidf -t upload
```

Expected: Upload erfolgreich, ESP32 bootet.

- [ ] **Schritt 4: Serial Monitor öffnen und Boot-Log prüfen**

```bash
cd Code && pio device monitor -e espidf
```

Expected Output:
```
I (xxx) main: ESP-RC-Car (ESP-IDF) starting...
I (xxx) system: Settings loaded from NVS
I (xxx) drv8323: DRV8323 initialized
I (xxx) comms: WiFi connected, IP: 192.168.x.x
```

- [ ] **Schritt 5: WebSocket-Verbindung mit App testen**

App öffnen → UDP Discovery → Verbinden → Joystick bewegen → Motor dreht sich.

- [ ] **Schritt 6: Serial Console testen**

Im Monitor eingeben:
```
status
```
Expected:
```
I (xxx) bg: Battery: 85%
I (xxx) bg: Safe mode: NO
I (xxx) bg: WiFi: connected
```

---

## Obsidian Brain aktualisieren

- [ ] **Schritt 7: Vault Notes updaten**

Die neuen Komponenten in den Obsidian-Notes reflektieren:
```bash
obsidian reload vault="Esp-RC-Car"
```

Notes die geupdated werden müssen:
- `Firmware/Firmware Overview.md` — neue Task-Architektur
- `Firmware/rc_motor.md` — ESP-IDF APIs
- `Firmware/rc_websocket.md` — esp_http_server
- `Firmware/rc_network.md` — esp_wifi + lwip
- `Firmware/rc_battery.md` — adc_oneshot
- `Firmware/rc_ota.md` — esp_https_ota
- `Firmware/rc_recovery.md` — NVS + EventGroup
- `Firmware/rc_settings.md` — DeviceSettings struct (unverändert)
