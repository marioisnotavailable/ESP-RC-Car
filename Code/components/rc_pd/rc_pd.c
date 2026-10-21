#include "rc_pd.h"
#include "rc_common.h"
#include "rc_system.h"
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

// Register-Map nach STSW-STUSB003 (USB_PD_defines.h)
#define REG_ALERT_STATUS_1      0x0B
#define REG_ALERT_STATUS_MASK   0x0C
#define REG_PORT_STATUS_0       0x0D
#define REG_PORT_STATUS_1       0x0E
#define REG_CC_STATUS           0x11
#define REG_PRT_STATUS          0x16
#define REG_PD_COMMAND_CTRL     0x1A
#define REG_RESET_CTRL          0x23
#define REG_PE_FSM              0x29
#define REG_DEVICE_ID           0x2F
#define REG_RX_BYTE_CNT         0x30
#define REG_RX_HEADER_LOW       0x31
#define REG_RX_DATA_OBJ1        0x33
#define REG_TX_HEADER_LOW       0x51
#define REG_TX_HEADER_HIGH      0x52
#define REG_DPM_PDO_NUMB        0x70
#define REG_DPM_SNK_PDO1        0x85
#define REG_RDO_REG_STATUS      0x91

// Konstanten nach STSW-STUSB003
#define PE_SNK_READY            0x18
#define CMD_SEND_COMMAND        0x26
#define TX_HDR_SOFT_RESET       0x0D
#define SW_RESET_ON             0x01
#define SW_RESET_OFF            0x00

// Ziel-Profile fuer PD-Verhandlung (1000 mA damit jedes Netzteil akzeptiert wird):
// PDO3 (hoechste Prio) = 12 V, PDO2 (Fallback) = 9 V, PDO1 = 5 V
#define PD_PDO2_MV              9000
#define PD_PDO3_MV              12000
#define PD_TARGET_MA            1000

static bool s_ready;
static bool s_renegotiated_this_attach;
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
    float v = ((float)sum / n / 4095.0f) * 3.1f * VBUS_DIVIDER * settings.vbus_corr_factor;
    g_vbus = v;
    return (int)(v * 1000.0f);
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

// Loescht alle Transition-/Alert-Latches (0x0B..0x16) wie in STSW-STUSB003 usb_pd_init()
static void pd_clear_latches(void)
{
    uint8_t dummy[12];
    pd_read(REG_ALERT_STATUS_1, dummy, sizeof(dummy));
}

static uint16_t pd_read_sink_pdo_mv(uint8_t pdo_num)
{
    uint8_t b[4] = {0};
    if (pd_read(REG_DPM_SNK_PDO1 + 4 * (pdo_num - 1), b, 4) != ESP_OK) return 0;
    uint32_t p = b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
    return (uint16_t)(((p >> 10) & 0x3FF) * 50);
}

static void dump_sink_pdos(void)
{
    uint8_t n = 0, b[4];
    pd_read(REG_DPM_PDO_NUMB, &n, 1);
    ESP_LOGI(TAG, "DPM_PDO_NUMB=%u", n & 0x07);
    for (int i = 0; i < 3; i++) {
        if (pd_read(REG_DPM_SNK_PDO1 + 4 * i, b, 4) != ESP_OK) continue;
        uint32_t p = b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
        ESP_LOGI(TAG, "sink PDO%d: %u mV %u mA (raw=0x%08lX)",
                 i + 1,
                 (unsigned)(((p >> 10) & 0x3FF) * 50),
                 (unsigned)((p & 0x3FF) * 10),
                 (unsigned long)p);
    }
}

// Liest die vom Netzteil empfangenen Source-PDOs aus dem RX-Puffer (0x30..0x4E)
static void dump_source_pdos(uint8_t max_objs)
{
    uint8_t cnt = 0, hdr[2] = {0};
    pd_read(REG_RX_BYTE_CNT, &cnt, 1);
    pd_read(REG_RX_HEADER_LOW, hdr, 2);
    uint16_t h = hdr[0] | ((uint16_t)hdr[1] << 8);
    uint8_t hdr_objs = (h >> 12) & 0x07;
    uint8_t msg_type = h & 0x1F;
    ESP_LOGI(TAG, "RX_BUF: byte_cnt=%u hdr=0x%04X (type=%u objs=%u)", cnt, h, msg_type, hdr_objs);

    uint8_t n = max_objs ? max_objs : (hdr_objs ? hdr_objs : 7);
    if (n > 7) n = 7;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t b[4] = {0};
        if (pd_read(REG_RX_DATA_OBJ1 + 4 * i, b, 4) != ESP_OK) continue;
        uint32_t pdo = b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
        if (pdo == 0) continue;
        uint8_t type = (pdo >> 30) & 0x03;
        if (type == 0) {
            // Fixed Supply Source PDO
            uint32_t mv = ((pdo >> 10) & 0x3FF) * 50;
            uint32_t ma = (pdo & 0x3FF) * 10;
            ESP_LOGI(TAG, "  src PDO%u (Fixed): %lu mV %lu mA (raw=0x%08lX)",
                     i + 1, (unsigned long)mv, (unsigned long)ma, (unsigned long)pdo);
        } else if (type == 3) {
            // Augmented PDO (PPS)
            uint32_t min_mv = ((pdo >> 8) & 0xFF) * 100;
            uint32_t max_mv = ((pdo >> 17) & 0xFF) * 100;
            uint32_t ma     = (pdo & 0x7F) * 50;
            ESP_LOGI(TAG, "  src PDO%u (PPS):   %lu..%lu mV %lu mA (raw=0x%08lX)",
                     i + 1, (unsigned long)min_mv, (unsigned long)max_mv, (unsigned long)ma, (unsigned long)pdo);
        } else {
            ESP_LOGI(TAG, "  src PDO%u (type=%u): raw=0x%08lX", i + 1, type, (unsigned long)pdo);
        }
    }
}

// Entspricht Update_PDO() aus STSW-STUSB003:
// Read-Modify-Write erhaelt die oberen Konfigurationsbits [31:20] des PDO-Registers.
esp_err_t rc_pd_set_pdo(uint8_t pdo_num, uint16_t mv, uint16_t ma)
{
    if (pdo_num < 1 || pdo_num > 3) return ESP_ERR_INVALID_ARG;
    uint8_t reg = REG_DPM_SNK_PDO1 + 4 * (pdo_num - 1);
    uint8_t b[4] = {0};
    uint32_t pdo = 0;
    if (pd_read(reg, b, 4) == ESP_OK) {
        pdo = b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
    }
    // Bits [31:30] = 00 (Fixed Supply), Bits [29:20] = bestehende Flags beibehalten
    pdo &= 0x3FF00000UL;
    pdo |= ((uint32_t)((mv / 50) & 0x3FF) << 10) | ((uint32_t)((ma / 10) & 0x3FF));
    b[0] = pdo & 0xFF;
    b[1] = (pdo >> 8) & 0xFF;
    b[2] = (pdo >> 16) & 0xFF;
    b[3] = (pdo >> 24) & 0xFF;
    return pd_write(reg, b, 4);
}

// Konfiguriert PDO1=5V, PDO2=9V (Fallback), PDO3=12V (Prio 1) und DPM_PDO_NUMB=3
static esp_err_t pd_apply_12v_profile(void)
{
    esp_err_t err = rc_pd_set_pdo(1, 5000, 1500);
    if (err == ESP_OK) err = rc_pd_set_pdo(2, PD_PDO2_MV, PD_TARGET_MA);
    if (err == ESP_OK) err = rc_pd_set_pdo(3, PD_PDO3_MV, PD_TARGET_MA);
    uint8_t n = 3;
    if (err == ESP_OK) err = pd_write(REG_DPM_PDO_NUMB, &n, 1);
    return err;
}

// Entspricht Send_Soft_reset_Message() aus STSW-STUSB003:
// Wartet bis PE_FSM == PE_SNK_READY (0x18), loescht Latches und sendet PD Soft_Reset.
esp_err_t rc_pd_request_renegotiate(void)
{
    uint8_t pe_fsm = 0;
    for (int i = 0; i < 30; i++) {
        if (pd_read(REG_PE_FSM, &pe_fsm, 1) == ESP_OK && pe_fsm == PE_SNK_READY) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (pe_fsm != PE_SNK_READY) {
        ESP_LOGW(TAG, "Soft-Reset angefordert bei PE_FSM=0x%02X (!= PE_SNK_READY 0x18)", pe_fsm);
    }

    pd_clear_latches();

    uint8_t hdr_h = 0x00;
    uint8_t hdr_l = TX_HDR_SOFT_RESET;
    uint8_t cmd   = CMD_SEND_COMMAND;
    pd_write(REG_TX_HEADER_HIGH, &hdr_h, 1);
    esp_err_t err = pd_write(REG_TX_HEADER_LOW, &hdr_l, 1);
    if (err != ESP_OK) return err;
    err = pd_write(REG_PD_COMMAND_CTRL, &cmd, 1);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "PD Soft_Reset gesendet (PE_FSM=0x%02X)", pe_fsm);

    // Warte auf Source_Capabilities & neuen Vertrag (bis zu 500 ms)
    uint8_t last_pe = pe_fsm;
    uint8_t captured_objs = 0;
    for (int i = 0; i < 100; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
        uint8_t hdr[2] = {0};
        if (!captured_objs && pd_read(REG_RX_HEADER_LOW, hdr, 2) == ESP_OK) {
            uint16_t h = hdr[0] | ((uint16_t)hdr[1] << 8);
            uint8_t objs = (h >> 12) & 0x07;
            uint8_t type = h & 0x1F;
            if (type == 1 && objs > 0) { // 1 = Source_Capabilities
                captured_objs = objs;
                dump_source_pdos(objs);
            }
        }
        if (pd_read(REG_PE_FSM, &pe_fsm, 1) == ESP_OK && pe_fsm != last_pe) {
            last_pe = pe_fsm;
        }
    }
    if (!captured_objs) {
        dump_source_pdos(0);
    }
    return ESP_OK;
}

esp_err_t rc_pd_get_status(rc_pd_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = (rc_pd_status_t){0};

    uint8_t port1 = 0, pe_fsm = 0, rdo[4] = {0};
    if (pd_read(REG_PORT_STATUS_1, &port1, 1) == ESP_OK) {
        out->cable_attached = (port1 & 0x01) != 0;
    }
    if (pd_read(REG_PE_FSM, &pe_fsm, 1) == ESP_OK) {
        out->pe_fsm = pe_fsm;
    }

    esp_err_t err = pd_read(REG_RDO_REG_STATUS, rdo, 4);
    if (err != ESP_OK) return err;

    uint32_t v = rdo[0] | (rdo[1] << 8) | (rdo[2] << 16) | ((uint32_t)rdo[3] << 24);
    uint8_t pos = (v >> 28) & 0x07;
    out->object_pos     = pos;
    out->cap_mismatch   = ((v >> 26) & 0x01) != 0;
    out->op_current_ma  = (uint16_t)(((v >> 10) & 0x3FF) * 10);
    out->max_current_ma = (uint16_t)((v & 0x3FF) * 10);
    out->attached       = (pos >= 1);

    int mv = vbus_mv();
    out->voltage_mv = mv > 0 ? (uint16_t)mv : 0;
    return ESP_OK;
}

esp_err_t rc_pd_init(void)
{
    // PIN_PD_RESET sicher auf 0 halten (niemals auf 1 ziehen, da sonst VBUS abfaellt!)
    gpio_set_level(PIN_PD_RESET, 0);
    gpio_set_direction(PIN_PD_RESET, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_PD_RESET, 0);

    // 100 ms warten, damit der STUSB4500 nach Power-On sein NVM vollstaendig geladen hat
    vTaskDelay(pdMS_TO_TICKS(100));

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
    for (int attempt = 0; attempt < 10; attempt++) {
        err = pd_read(REG_DEVICE_ID, &id, 1);
        if (err == ESP_OK && (id == 0x25 || id == 0x21)) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "STUSB4500 not responding (SDA=%d SCL=%d): %s",
                 PIN_PD_SDA, PIN_PD_SCL, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "STUSB4500 device ID 0x%02X%s", id,
             (id == 0x25 || id == 0x21) ? "" : " (unexpected)");

    // Alert-Maske setzen und Latches (0x0B..0x16) leeren wie in STSW-STUSB003 usb_pd_init()
    uint8_t mask = 0x1B; // CC_DETECTION, MONITORING, PRT_STATUS unmaskiert
    pd_write(REG_ALERT_STATUS_MASK, &mask, 1);
    pd_clear_latches();

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
            if (!st.cable_attached) {
                s_renegotiated_this_attach = false;
            } else {
                // Sobald Kabel steckt und der erste Handshake (PE_SNK_READY) steht
                // oder das NVM (PDO3=20000 mV) geladen wurde: 12V/9V-Profil setzen & neu verhandeln
                uint16_t pdo3_mv = pd_read_sink_pdo_mv(3);
                if (pdo3_mv != PD_PDO3_MV ||
                    (!s_renegotiated_this_attach && st.pe_fsm == PE_SNK_READY)) {
                    ESP_LOGI(TAG, "Vor 12V-Neuverhandlung (PDO3=%u mV, PE_FSM=0x%02X, src_pdo=%u):",
                             pdo3_mv, st.pe_fsm, st.object_pos);
                    dump_sink_pdos();
                    dump_source_pdos(0);

                    pd_apply_12v_profile();
                    ESP_LOGI(TAG, "Nach 12V-PDO-Update:");
                    dump_sink_pdos();

                    rc_pd_request_renegotiate();
                    s_renegotiated_this_attach = true;
                    rc_pd_get_status(&st);
                }
            }
            {   // Rohdump zur Diagnose: 0x0B..0x16 (Alert/CC/Monitoring/HW-Fault/PRT), PE_FSM, RDO
                uint8_t raw[12] = {0}, pe = 0, rdo[4] = {0};
                pd_read(REG_ALERT_STATUS_1, raw, sizeof(raw));
                pd_read(REG_PE_FSM, &pe, 1);
                pd_read(REG_RDO_REG_STATUS, rdo, 4);
                ESP_LOGI(TAG, "raw 0B..16: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X | PE=%02X RDO=%02X%02X%02X%02X",
                         raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7],
                         raw[8], raw[9], raw[10], raw[11], pe, rdo[3], rdo[2], rdo[1], rdo[0]);
            }
            ESP_LOGI(TAG, "cc=%d pe=0x%02X contract=%d src_pdo=%u mismatch=%d cur=%u/%u mA vbus=%u mV",
                     st.cable_attached, st.pe_fsm, st.attached, st.object_pos,
                     st.cap_mismatch, st.op_current_ma, st.max_current_ma, st.voltage_mv);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
