#include "rc_comms.h"
#include "rc_system.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/sockets.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "comms";
static volatile uint32_t last_cmd_ms = 0;

#define AP_IP "192.168.4.1"

static void dns_task(void *arg);

/* ── Content-type helper ────────────────────────────────────────────────── */

static const char *content_type_for_path(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext)                          return "application/octet-stream";
    if (strcmp(ext, ".html") == 0)     return "text/html";
    if (strcmp(ext, ".js")   == 0)     return "application/javascript";
    if (strcmp(ext, ".css")  == 0)     return "text/css";
    if (strcmp(ext, ".ico")  == 0)     return "image/x-icon";
    if (strcmp(ext, ".json") == 0)     return "application/json";
    if (strcmp(ext, ".png")  == 0)     return "image/png";
    return "application/octet-stream";
}

/* ── Static file handler ────────────────────────────────────────────────── */

static esp_err_t static_file_handler(httpd_req_t *req)
{
    /* Build filesystem path: /littlefs + uri, or /littlefs/index.html for "/" */
    char filepath[128];
    const char *uri = req->uri;
    if (strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }
    snprintf(filepath, sizeof(filepath), "/littlefs%s", uri);

    FILE *f = fopen(filepath, "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type_for_path(filepath));

    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, (ssize_t)n) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0); /* terminate chunked response */
    return ESP_OK;
}

/* ── WiFi event handler ─────────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        xEventGroupClearBits(rc_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        xEventGroupSetBits(rc_events, WIFI_CONNECTED_BIT);
    }
}

/* ── wifi_init_common ───────────────────────────────────────────────────── */

static void wifi_init_common(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));
}

/* ── wifi_connect ───────────────────────────────────────────────────────── */

static bool wifi_connect(void)
{
    /* Load credentials from NVS namespace "wifi" — check before init */
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return false;
    }

    char ssid[64] = {0};
    char pass[64] = {0};
    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(pass);

    err = nvs_get_str(nvs, "ssid0", ssid, &ssid_len);
    nvs_get_str(nvs, "pass0", pass, &pass_len);
    nvs_close(nvs);

    if (err != ESP_OK || ssid[0] == '\0') {
        ESP_LOGE(TAG, "No SSID in NVS");
        return false;
    }

    esp_netif_create_default_wifi_sta();
    wifi_init_common();

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    EventBits_t bits = xEventGroupWaitBits(rc_events, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi STA connected");
        return true;
    }

    ESP_LOGE(TAG, "WiFi connect timeout");
    return false;
}

/* ── wifi_start_ap ──────────────────────────────────────────────────────── */

static void wifi_start_ap(void)
{
    esp_netif_create_default_wifi_ap();
    wifi_init_common();

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);

    wifi_config_t ap_cfg = {
        .ap = {
            .channel        = 1,
            .authmode       = WIFI_AUTH_OPEN,
            .max_connection = 4,
        },
    };
    snprintf((char *)ap_cfg.ap.ssid, sizeof(ap_cfg.ap.ssid),
             "%s%02X%02X%02X", settings.ap_prefix, mac[3], mac[4], mac[5]);
    ap_cfg.ap.ssid_len = (uint8_t)strlen((char *)ap_cfg.ap.ssid);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupSetBits(rc_events, WIFI_CONNECTED_BIT);
    ESP_LOGI(TAG, "SoftAP started: SSID=\"%s\" (open)", (char *)ap_cfg.ap.ssid);

    xTaskCreate(dns_task, "dns", 4096, NULL, 5, NULL);
}

/* ── Captive portal DNS task ────────────────────────────────────────────── */

static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket create failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bind_addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons(53),
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Captive portal DNS listening");

    uint8_t buf[256];
    struct sockaddr_in src;
    socklen_t src_len = sizeof(src);

    for (;;) {
        int len = recvfrom(sock, buf, sizeof(buf) - 20, 0,
                           (struct sockaddr *)&src, &src_len);
        if (len < 12) continue;

        /* Build DNS reply: QR=1 AA=1 RD copy RA=1, ANCOUNT=1 */
        buf[2] = 0x81; buf[3] = 0x80;
        buf[6] = 0x00; buf[7] = 0x01; /* answer count */
        buf[8] = 0x00; buf[9] = 0x00;
        buf[10] = 0x00; buf[11] = 0x00;

        /* Append A record answer */
        int pos = len;
        buf[pos++] = 0xc0; buf[pos++] = 0x0c; /* name: ptr to offset 12 */
        buf[pos++] = 0x00; buf[pos++] = 0x01; /* type A */
        buf[pos++] = 0x00; buf[pos++] = 0x01; /* class IN */
        buf[pos++] = 0x00; buf[pos++] = 0x00;
        buf[pos++] = 0x00; buf[pos++] = 0x3c; /* TTL 60 */
        buf[pos++] = 0x00; buf[pos++] = 0x04; /* rdlength */
        buf[pos++] = 192; buf[pos++] = 168; buf[pos++] = 4; buf[pos++] = 1;

        sendto(sock, buf, pos, 0, (struct sockaddr *)&src, src_len);
    }

    close(sock);
    vTaskDelete(NULL);
}

/* ── UDP discovery task ─────────────────────────────────────────────────── */

static void udp_discovery_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "UDP socket create failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bind_addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons(DISCOVERY_PORT),
    };

    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "UDP bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP discovery listening on port %d", DISCOVERY_PORT);

    char rx_buf[64];
    struct sockaddr_in src_addr;
    socklen_t src_len = sizeof(src_addr);

    for (;;) {
        int len = recvfrom(sock, rx_buf, sizeof(rx_buf) - 1, 0,
                           (struct sockaddr *)&src_addr, &src_len);
        if (len <= 0) {
            continue;
        }
        rx_buf[len] = '\0';

        if (strcmp(rx_buf, DISCOVERY_QUERY) == 0) {
            /* Get own IP */
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            esp_netif_ip_info_t ip_info = {0};
            if (netif) {
                esp_netif_get_ip_info(netif, &ip_info);
            }

            char resp[64];
            snprintf(resp, sizeof(resp), DISCOVERY_RESP IPSTR,
                     IP2STR(&ip_info.ip));

            sendto(sock, resp, strlen(resp), 0,
                   (struct sockaddr *)&src_addr, src_len);
            ESP_LOGI(TAG, "Discovery response sent: %s", resp);
        }
    }

    close(sock);
    vTaskDelete(NULL);
}

/* ── WebSocket handler ──────────────────────────────────────────────────── */

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket handshake");
        return ESP_OK;
    }

    /* First recv: get frame size */
    httpd_ws_frame_t pkt = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = NULL,
    };
    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ws recv (len probe) failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if (pkt.len == 0) {
        return ESP_OK; /* empty frame, ping/pong etc. */
    }

    /* Allocate and receive payload */
    uint8_t *buf = calloc(1, pkt.len + 1);
    if (!buf) {
        ESP_LOGE(TAG, "ws buf alloc failed");
        return ESP_ERR_NO_MEM;
    }
    pkt.payload = buf;

    ret = httpd_ws_recv_frame(req, &pkt, pkt.len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ws recv failed: %s", esp_err_to_name(ret));
        free(buf);
        return ret;
    }

    /* Parse JSON {throttle, steer} */
    cJSON *root = cJSON_ParseWithLength((char *)buf, pkt.len);
    if (root) {
        Cmd cmd = {0};
        cJSON *t = cJSON_GetObjectItem(root, "throttle");
        cJSON *s = cJSON_GetObjectItem(root, "steer");
        if (cJSON_IsNumber(t)) cmd.throttle = (int16_t)t->valuedouble;
        if (cJSON_IsNumber(s)) cmd.steer    = (int16_t)s->valuedouble;

        xQueueOverwrite(cmd_queue, &cmd);
        last_cmd_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        cJSON_Delete(root);
    } else {
        ESP_LOGW(TAG, "ws JSON parse error");
    }

    free(buf);
    return ESP_OK;
}

/* ── Start WebSocket server ─────────────────────────────────────────────── */

/* ── Captive portal redirect handler ────────────────────────────────────── */

static esp_err_t captive_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" AP_IP "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static httpd_handle_t start_ws_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port       = WS_PORT;
    config.uri_match_fn      = httpd_uri_match_wildcard;
    config.max_uri_handlers  = 16;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return NULL;
    }

    static const httpd_uri_t ws_uri = {
        .uri          = "/ws",
        .method       = HTTP_GET,
        .handler      = ws_handler,
        .user_ctx     = NULL,
        .is_websocket = true,
    };
    httpd_register_uri_handler(server, &ws_uri);

    /* Captive portal detection endpoints (Apple / Android / Windows) */
    static const char *captive_uris[] = {
        "/hotspot-detect.html",         /* Apple */
        "/library/test/success.html",   /* Apple */
        "/generate_204",                /* Android */
        "/gen_204",                     /* Android alt */
        "/ncsi.txt",                    /* Windows */
        "/connecttest.txt",             /* Windows */
        "/redirect",                    /* Windows */
        "/canonical.html",              /* Firefox */
    };
    static httpd_uri_t cap_uri = {
        .method   = HTTP_GET,
        .handler  = captive_redirect_handler,
        .user_ctx = NULL,
    };
    for (int i = 0; i < (int)(sizeof(captive_uris) / sizeof(captive_uris[0])); i++) {
        cap_uri.uri = captive_uris[i];
        httpd_register_uri_handler(server, &cap_uri);
    }

    static const httpd_uri_t file_uri = {
        .uri      = "/*",
        .method   = HTTP_GET,
        .handler  = static_file_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &file_uri);

    ESP_LOGI(TAG, "HTTP server started on port %d", WS_PORT);
    return server;
}

/* ── Broadcast battery percentage to all WS clients ────────────────────── */

static void broadcast_battery(httpd_handle_t server)
{
    if (!server) return;

    int pct = 0;
    xQueuePeek(batt_queue, &pct, 0);

    char json[32];
    int json_len = snprintf(json, sizeof(json), "{\"batt\":%d}", pct);

    size_t  clients     = 10;           /* max clients to query */
    int     client_fds[10];
    esp_err_t err = httpd_get_client_list(server, &clients, client_fds);
    if (err != ESP_OK) return;

    for (size_t i = 0; i < clients; i++) {
        httpd_ws_client_info_t info = httpd_ws_get_fd_info(server, client_fds[i]);
        if (info != HTTPD_WS_CLIENT_WEBSOCKET) continue;

        httpd_ws_frame_t pkt = {
            .final   = true,
            .fragmented = false,
            .type    = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)json,
            .len     = (size_t)json_len,
        };
        httpd_ws_send_frame_async(server, client_fds[i], &pkt);
    }
}

/* ── Failsafe check ─────────────────────────────────────────────────────── */

static void check_failsafe(void)
{
    if (last_cmd_ms == 0) return;

    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if ((now - last_cmd_ms) > FAILSAFE_DEFAULT_MS) {
        Cmd zero = {0};
        xQueueOverwrite(cmd_queue, &zero);
        last_cmd_ms = 0; /* suppress repeated triggers until next real cmd */
        ESP_LOGW(TAG, "Failsafe triggered");
    }
}

/* ── comms_task ─────────────────────────────────────────────────────────── */

void comms_task(void *arg)
{
    ESP_LOGI(TAG, "comms_task started");

    /* esp_netif and event loop are expected to be initialised in app_main */

    bool connected = wifi_connect();

    if (connected) {
        xTaskCreate(udp_discovery_task, "udp_disc", 4096, NULL, 5, NULL);
    } else {
        ESP_LOGW(TAG, "No STA credentials — starting SoftAP");
        wifi_start_ap();
    }

    httpd_handle_t server = start_ws_server();

    TickType_t last_batt_tick = xTaskGetTickCount();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));

        check_failsafe();

        /* Broadcast battery every ~1 s */
        TickType_t now = xTaskGetTickCount();
        if ((now - last_batt_tick) >= pdMS_TO_TICKS(1000)) {
            last_batt_tick = now;
            broadcast_battery(server);
        }
    }
}
