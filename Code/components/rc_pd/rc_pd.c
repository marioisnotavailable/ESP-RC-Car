#include "rc_pd.h"
#include "rc_common.h"
#include "driver/i2c.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "rc_pd";

#define PD_I2C_PORT   I2C_NUM_0
#define PD_I2C_ADDR   0x28
#define PD_I2C_FREQ   100000
#define PD_I2C_TMO    pdMS_TO_TICKS(100)

#define REG_DEVICE_ID      0x2F
#define REG_TX_HEADER_L    0x51
#define REG_PD_CMD_CTRL    0x1A
#define REG_DPM_PDO_NUMB   0x70
#define REG_DPM_SNK_PDO1   0x85
#define REG_RDO_STATUS     0x91

#define CMD_SEND_SOFT_RST  0x26
#define TX_HDR_SOFT_RST    0x0D

static bool s_ready;
static adc_oneshot_unit_handle_t s_adc;

// VBUS ueber Teiler (PIN_VBUSADC = GPIO14 = ADC2 CH3); -1 bei Fehler
static int vbus_mv(void)
{
    if (!s_adc) {
        adc_oneshot_unit_init_cfg_t u = { .unit_id = ADC_UNIT_2 };
        if (adc_oneshot_new_unit(&u, &s_adc) != ESP_OK) { s_adc = NULL; return -1; }
        adc_oneshot_chan_cfg_t c = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
        if (adc_oneshot_config_channel(s_adc, ADC_CHANNEL_3, &c) != ESP_OK) return -1;
    }
    int sum = 0, n = 0, raw;
    for (int i = 0; i < 16; i++) {
        if (adc_oneshot_read(s_adc, ADC_CHANNEL_3, &raw) == ESP_OK) { sum += raw; n++; }
    }
    if (!n) return -1;
    float v = ((float)sum / n / 4095.0f) * 3.1f * VBUS_DIVIDER;
    return (int)(v * 1000.0f);
}

static void dump_sink_pdos(void)
{
    uint8_t n = 0, b[4];
    pd_read(REG_DPM_PDO_NUMB, &n, 1);
    ESP_LOGI(TAG, "DPM_PDO_NUMB=%u", n & 7);
    for (int i = 0; i < 3; i++) {
        if (pd_read(REG_DPM_SNK_PDO1 + 4 * i, b, 4) != ESP_OK) continue;
        uint32_t p = b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
        ESP_LOGI(TAG, "sink PDO%d: %u mV %u mA", i + 1, ((p >> 10) & 0x3FF) * 50, (p & 0x3FF) * 10);
    }
}

static esp_err_t pd_write(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[5];
    if (len > 4) return ESP_ERR_INVALID_SIZE;
    buf[0] = reg;
    for (size_t i = 0; i < len; i++) buf[1 + i] = data[i];
    return i2c_master_write_to_device(PD_I2C_PORT, PD_I2C_ADDR, buf, len + 1, PD_I2C_TMO);
}

static esp_err_t pd_read(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(PD_I2C_PORT, PD_I2C_ADDR, &reg, 1, data, len, PD_I2C_TMO);
}

esp_err_t rc_pd_set_pdo(uint8_t pdo_num, uint16_t mv, uint16_t ma)
{
    if (pdo_num < 1 || pdo_num > 3) return ESP_ERR_INVALID_ARG;
    // bits[19:10] Spannung in 50 mV, bits[9:0] Strom in 10 mA
    uint32_t pdo = ((uint32_t)(mv / 50) << 10) | (ma / 10);
    uint8_t b[4] = { pdo & 0xFF, (pdo >> 8) & 0xFF, (pdo >> 16) & 0xFF, (pdo >> 24) & 0xFF };
    return pd_write(REG_DPM_SNK_PDO1 + 4 * (pdo_num - 1), b, 4);
}

esp_err_t rc_pd_request_renegotiate(void)
{
    uint8_t hdr = TX_HDR_SOFT_RST;
    uint8_t cmd = CMD_SEND_SOFT_RST;
    esp_err_t err = pd_write(REG_TX_HEADER_L, &hdr, 1);
    if (err != ESP_OK) return err;
    return pd_write(REG_PD_CMD_CTRL, &cmd, 1);
}

esp_err_t rc_pd_get_status(rc_pd_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    out->attached = false;
    out->object_pos = 0;
    out->voltage_mv = 0;

    uint8_t rdo[4];
    esp_err_t err = pd_read(REG_RDO_STATUS, rdo, 4);
    if (err != ESP_OK) return err;
    uint32_t v = rdo[0] | (rdo[1] << 8) | (rdo[2] << 16) | ((uint32_t)rdo[3] << 24);
    uint8_t pos = (v >> 28) & 0x7;
    out->object_pos = pos;
    if (pos < 1) return ESP_OK;

    // pos = Index im Source-Angebot des Netzteils (nicht Sink-Tabelle!)
    int mv = vbus_mv();
    out->voltage_mv = mv > 0 ? (uint16_t)mv : 0;
    out->attached = true;
    return ESP_OK;
}

esp_err_t rc_pd_init(void)
{
    // RESET_USBPD ist active-high: kurz ausloesen, dann low = Chip laeuft
    gpio_set_direction(PIN_PD_RESET, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_PD_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_PD_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(50));

    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_PD_SDA,
        .scl_io_num = PIN_PD_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = PD_I2C_FREQ,
    };
    ESP_ERROR_CHECK(i2c_param_config(PD_I2C_PORT, &cfg));
    esp_err_t err = i2c_driver_install(PD_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) return err;

    uint8_t id = 0;
    err = pd_read(REG_DEVICE_ID, &id, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "STUSB4500 not responding (SDA=%d SCL=%d): %s",
                 PIN_PD_SDA, PIN_PD_SCL, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "STUSB4500 device ID 0x%02X%s", id,
             (id == 0x25 || id == 0x21) ? "" : " (unexpected)");

    uint8_t n = 2;
    err = pd_write(REG_DPM_PDO_NUMB, &n, 1);
    if (err == ESP_OK) err = rc_pd_set_pdo(1, 5000, 1500);
    if (err == ESP_OK) err = rc_pd_set_pdo(2, 12000, 3000);
    if (err == ESP_OK) err = rc_pd_request_renegotiate();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PDO config failed: %s", esp_err_to_name(err));
        return err;
    }
    dump_sink_pdos();
    s_ready = true;
    return ESP_OK;
}

void pd_task(void *arg)
{
    if (rc_pd_init() != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }
    while (1) {
        rc_pd_status_t st;
        if (s_ready && rc_pd_get_status(&st) == ESP_OK) {
            ESP_LOGI(TAG, "contract=%d src_pdo=%u vbus=%d mV", st.attached, st.object_pos, vbus_mv());
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
