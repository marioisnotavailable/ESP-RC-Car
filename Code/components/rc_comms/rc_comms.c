/* rc_comms.c – WiFi / WebSocket / UDP-discovery / API / Port-81 WS
 *
 * Port 80  – native-app WebSocket at /ws (JSON {"throttle":X,"steer":Y})
 *          – all /api/ ENDPOINTS REST endpoints
 *          – captive-portal redirects
 *          – static file handler  (LittleFS)
 *
 * Port 81  – web-UI WebSocket at /  (CMD:xxx -> BATT:XX)
 *
 * UDP 49352 – discovery (ESP_RC_DISCOVER -> ESP_RC_HERE <ip>)
 */

#include "rc_comms.h"
#include "rc_system.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "esp_http_server.h"
#include "driver/gpio.h"

static const char *TAG = "rc_comms";

/* ─── scan state ─────────────────────────────────────────────────────────── */
static bool      s_scan_started   = false;
static TickType_t s_scan_start_tick = 0;

/* ═══════════════════════════════════════════════════════════════════════════
 * Helper: URL-decode / body parsing
 * ═══════════════════════════════════════════════════════════════════════════ */

static void url_decode(const char *src, char *dst, int dst_sz)
{
    int di = 0;
    while (*src && di < dst_sz - 1) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], '\0' };
            dst[di++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[di++] = ' ';
            src++;
        } else {
            dst[di++] = *src++;
        }
    }
    dst[di] = '\0';
}

/* Find key=value in URL-encoded body. Returns true if found. */
static bool get_param(const char *body, const char *key, char *out, int out_sz)
{
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            p += klen + 1;
            const char *end = strchr(p, '&');
            int vlen = end ? (int)(end - p) : (int)strlen(p);
            if (vlen >= out_sz) vlen = out_sz - 1;
            char raw[256] = {0};
            if (vlen >= (int)sizeof(raw)) vlen = (int)sizeof(raw) - 1;
            memcpy(raw, p, vlen);
            raw[vlen] = '\0';
            url_decode(raw, out, out_sz);
            return true;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    return false;
}

/* Read full POST body into buf (null-terminated). Returns bytes read or -1. */
static int recv_body(httpd_req_t *req, char *buf, int buf_sz)
{
    int total = req->content_len;
    if (total <= 0) { buf[0] = '\0'; return 0; }
    if (total >= buf_sz) total = buf_sz - 1;
    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, buf + received, total - received);
        if (r <= 0) return -1;
        received += r;
    }
    buf[received] = '\0';
    return received;
}

/* ─── JSON helpers ──────────────────────────────────────────────────────── */
static void json_ok(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
}

static void json_fail(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":false}");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * WiFi helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static void on_sta_got_ip(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    xEventGroupSetBits(rc_events, WIFI_CONNECTED_BIT);
}

static bool wifi_connect(const char *ssid, const char *pass)
{
    xEventGroupClearBits(rc_events, WIFI_CONNECTED_BIT);

    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid,     ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_start();
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(rc_events, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

static void wifi_start_ap(const char *ssid)
{
    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.ap.ssid, ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len      = (uint8_t)strlen(ssid);
    cfg.ap.max_connection = 4;
    cfg.ap.authmode      = WIFI_AUTH_OPEN;

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &cfg);
    esp_wifi_start();
    ESP_LOGI(TAG, "AP started: %s", ssid);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * DNS captive-portal task
 * ═══════════════════════════════════════════════════════════════════════════ */

static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { vTaskDelete(NULL); return; }

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    uint8_t buf[512];
    for (;;) {
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &slen);
        if (len < 12) continue;

        /* Build minimal DNS response that points to 192.168.4.1 */
        uint8_t resp[512];
        memcpy(resp, buf, len);
        resp[2] = 0x81; resp[3] = 0x80; /* QR=1, RA=1 */
        resp[6] = 0x00; resp[7] = 0x01; /* ANCOUNT=1 */

        int rlen = len;
        /* pointer to question name */
        resp[rlen++] = 0xC0; resp[rlen++] = 0x0C;
        /* TYPE A */
        resp[rlen++] = 0x00; resp[rlen++] = 0x01;
        /* CLASS IN */
        resp[rlen++] = 0x00; resp[rlen++] = 0x01;
        /* TTL 60 */
        resp[rlen++] = 0x00; resp[rlen++] = 0x00;
        resp[rlen++] = 0x00; resp[rlen++] = 0x3C;
        /* RDLENGTH 4 */
        resp[rlen++] = 0x00; resp[rlen++] = 0x04;
        /* 192.168.4.1 */
        resp[rlen++] = 192; resp[rlen++] = 168;
        resp[rlen++] = 4;   resp[rlen++] = 1;

        sendto(sock, resp, rlen, 0, (struct sockaddr *)&src, slen);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * UDP discovery task
 * ═══════════════════════════════════════════════════════════════════════════ */

static void get_own_ip(char *out, size_t len)
{
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK)
        snprintf(out, len, IPSTR, IP2STR(&ip_info.ip));
    else
        strlcpy(out, "0.0.0.0", len);
}

static void udp_discovery_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { vTaskDelete(NULL); return; }

    int reuse = 1, bcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,    &reuse, sizeof(reuse));
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST,    &bcast, sizeof(bcast));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(DISCOVERY_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bcast_addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(DISCOVERY_PORT),
        .sin_addr.s_addr = htonl(INADDR_BROADCAST),
    };

    char buf[64];
    for (;;) {
        /* Wait up to 1 s for an incoming query, then beacon regardless */
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int ready = select(sock + 1, &fds, NULL, NULL, &tv);

        char ip_str[16];
        get_own_ip(ip_str, sizeof(ip_str));

        char resp[64];
        snprintf(resp, sizeof(resp), DISCOVERY_RESP "%s", ip_str);

        if (ready > 0 && FD_ISSET(sock, &fds)) {
            struct sockaddr_in src;
            socklen_t slen = sizeof(src);
            int len = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                               (struct sockaddr *)&src, &slen);
            if (len > 0) {
                buf[len] = '\0';
                if (strcmp(buf, DISCOVERY_QUERY) == 0)
                    sendto(sock, resp, strlen(resp), 0,
                           (struct sockaddr *)&src, slen);
            }
        }

        /* Periodic broadcast so the app finds the ESP without querying */
        sendto(sock, resp, strlen(resp), 0,
               (struct sockaddr *)&bcast_addr, sizeof(bcast_addr));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Port-80 WebSocket handler  (/ws)  – native app
 * ═══════════════════════════════════════════════════════════════════════════ */

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WS handshake /ws");
        return ESP_OK;
    }

    httpd_ws_frame_t pkt = { .type = HTTPD_WS_TYPE_TEXT };
    uint8_t buf[128] = {0};
    pkt.payload = buf;

    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, sizeof(buf) - 1);
    if (ret != ESP_OK) return ret;

    /* Expect {"throttle":X,"steer":Y} */
    int16_t throttle = 0, steer = 0;
    sscanf((char *)pkt.payload, "{\"throttle\":%hd,\"steer\":%hd}", &throttle, &steer);

    Cmd cmd = { .throttle = throttle, .steer = steer, .flags = 0 };
    xQueueOverwrite(cmd_queue, &cmd);

    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * broadcast_battery – port 80 JSON  {"batt":XX}
 * ═══════════════════════════════════════════════════════════════════════════ */

static void broadcast_battery(httpd_handle_t server)
{
    char msg[32];
    snprintf(msg, sizeof(msg), "{\"batt\":%d}", battery_percent);

    httpd_ws_frame_t pkt = {
        .type        = HTTPD_WS_TYPE_TEXT,
        .payload     = (uint8_t *)msg,
        .len         = strlen(msg),
        .final       = true,
    };

    size_t      fds   = 0;
    int        *clients = NULL;
    httpd_get_client_list(server, &fds, NULL);
    if (fds == 0) return;
    clients = malloc(fds * sizeof(int));
    if (!clients) return;
    httpd_get_client_list(server, &fds, clients);

    for (size_t i = 0; i < fds; i++) {
        if (httpd_ws_get_fd_info(server, clients[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(server, clients[i], &pkt);
        }
    }
    free(clients);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Failsafe check
 * ═══════════════════════════════════════════════════════════════════════════ */

static TickType_t s_last_cmd_tick = 0;

static void check_failsafe(void)
{
    TickType_t now = xTaskGetTickCount();
    uint32_t   ms  = (uint32_t)((now - s_last_cmd_tick) * portTICK_PERIOD_MS);
    if (ms > settings.failsafe_ms) {
        Cmd zero = {0};
        xQueueOverwrite(cmd_queue, &zero);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Captive-portal handlers
 * ═══════════════════════════════════════════════════════════════════════════ */

static esp_err_t redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_sendstr(req, "");
    return ESP_OK;
}

static esp_err_t generate_204_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_sendstr(req, "");
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Static file handler
 * ═══════════════════════════════════════════════════════════════════════════ */

static esp_err_t static_file_handler(httpd_req_t *req)
{
    /* Map / → /index.html */
    const char *uri = req->uri;
    char path[512];
    if (strcmp(uri, "/") == 0) {
        strlcpy(path, "/littlefs/index.html", sizeof(path));
    } else {
        if (snprintf(path, sizeof(path), "/littlefs%s", uri) >= sizeof(path)) {
            // Path was truncated, handle gracefully
            path[sizeof(path) - 1] = '\0';
        }
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    /* Guess content-type */
    const char *ct = "application/octet-stream";
    if (strstr(path, ".html")) ct = "text/html";
    else if (strstr(path, ".css"))  ct = "text/css";
    else if (strstr(path, ".js"))   ct = "application/javascript";
    else if (strstr(path, ".json")) ct = "application/json";
    else if (strstr(path, ".png"))  ct = "image/png";
    else if (strstr(path, ".ico"))  ct = "image/x-icon";
    else if (strstr(path, ".svg"))  ct = "image/svg+xml";

    httpd_resp_set_type(req, ct);

    char fbuf[512];
    size_t n;
    while ((n = fread(fbuf, 1, sizeof(fbuf), f)) > 0) {
        httpd_resp_send_chunk(req, fbuf, (ssize_t)n);
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * NVS WiFi helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

#define NVS_WIFI_NS  "wifi"
#define NVS_MAX_NETS 5

typedef struct {
    uint8_t  cnt;
    char     ssid[NVS_MAX_NETS][64];
    char     pass[NVS_MAX_NETS][64];
    uint32_t last[NVS_MAX_NETS];
} WifiList;

static esp_err_t nvs_load_wifi_list(WifiList *wl)
{
    memset(wl, 0, sizeof(*wl));
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_WIFI_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    nvs_get_u8(h, "cnt", &wl->cnt);
    if (wl->cnt > NVS_MAX_NETS) wl->cnt = NVS_MAX_NETS;

    for (int i = 0; i < wl->cnt; i++) {
        char key[8];
        size_t sz = sizeof(wl->ssid[i]);
        snprintf(key, sizeof(key), "ssid%d", i); nvs_get_str(h, key, wl->ssid[i], &sz);
        sz = sizeof(wl->pass[i]);
        snprintf(key, sizeof(key), "pass%d", i); nvs_get_str(h, key, wl->pass[i], &sz);
        snprintf(key, sizeof(key), "last%d", i); nvs_get_u32(h, key, &wl->last[i]);
    }
    nvs_close(h);
    return ESP_OK;
}

static esp_err_t nvs_save_wifi_list(const WifiList *wl)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_WIFI_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    nvs_set_u8(h, "cnt", wl->cnt);
    for (int i = 0; i < wl->cnt; i++) {
        char key[8];
        snprintf(key, sizeof(key), "ssid%d", i); nvs_set_str(h, key, wl->ssid[i]);
        snprintf(key, sizeof(key), "pass%d", i); nvs_set_str(h, key, wl->pass[i]);
        snprintf(key, sizeof(key), "last%d", i); nvs_set_u32(h, key, wl->last[i]);
    }
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * API handlers – port 80
 * ═══════════════════════════════════════════════════════════════════════════ */

/* GET /api/saved */
static esp_err_t api_saved_handler(httpd_req_t *req)
{
    WifiList wl;
    nvs_load_wifi_list(&wl);

    char buf[512];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "[");
    for (int i = 0; i < wl.cnt; i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "%s{\"i\":%d,\"ssid\":\"%s\",\"lastConn\":%lu}",
                        i ? "," : "", i, wl.ssid[i], (unsigned long)wl.last[i]);
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "]");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

/* POST /api/save  body: ssid=X&pass=Y */
static esp_err_t api_save_handler(httpd_req_t *req)
{
    char body[256];
    if (recv_body(req, body, sizeof(body)) < 0) { json_fail(req); return ESP_OK; }

    char ssid[64] = {0}, pass[64] = {0};
    if (!get_param(body, "ssid", ssid, sizeof(ssid))) { json_fail(req); return ESP_OK; }
    get_param(body, "pass", pass, sizeof(pass));

    WifiList wl;
    nvs_load_wifi_list(&wl);
    if (wl.cnt >= NVS_MAX_NETS) { json_fail(req); return ESP_OK; }

    int idx = wl.cnt++;
    strlcpy(wl.ssid[idx], ssid, sizeof(wl.ssid[idx]));
    strlcpy(wl.pass[idx], pass, sizeof(wl.pass[idx]));
    wl.last[idx] = 0;
    nvs_save_wifi_list(&wl);

    json_ok(req);
    return ESP_OK;
}

/* POST /api/delete  body: i=N */
static esp_err_t api_delete_handler(httpd_req_t *req)
{
    char body[64];
    if (recv_body(req, body, sizeof(body)) < 0) { json_fail(req); return ESP_OK; }

    char idx_s[8] = {0};
    if (!get_param(body, "i", idx_s, sizeof(idx_s))) { json_fail(req); return ESP_OK; }
    int idx = atoi(idx_s);

    WifiList wl;
    nvs_load_wifi_list(&wl);
    if (idx < 0 || idx >= wl.cnt) { json_fail(req); return ESP_OK; }

    /* Shift entries down */
    for (int i = idx; i < wl.cnt - 1; i++) {
        memcpy(wl.ssid[i], wl.ssid[i + 1], sizeof(wl.ssid[i]));
        memcpy(wl.pass[i], wl.pass[i + 1], sizeof(wl.pass[i]));
        wl.last[i] = wl.last[i + 1];
    }
    wl.cnt--;
    nvs_save_wifi_list(&wl);

    json_ok(req);
    return ESP_OK;
}

/* POST /api/move  body: from=N&to=M */
static esp_err_t api_move_handler(httpd_req_t *req)
{
    char body[64];
    if (recv_body(req, body, sizeof(body)) < 0) { json_fail(req); return ESP_OK; }

    char from_s[8] = {0}, to_s[8] = {0};
    if (!get_param(body, "from", from_s, sizeof(from_s)) ||
        !get_param(body, "to",   to_s,   sizeof(to_s))) {
        json_fail(req); return ESP_OK;
    }
    int from = atoi(from_s), to = atoi(to_s);

    WifiList wl;
    nvs_load_wifi_list(&wl);
    if (from < 0 || from >= wl.cnt || to < 0 || to >= wl.cnt) {
        json_fail(req); return ESP_OK;
    }

    /* Swap */
    char   tmp_ssid[64]; memcpy(tmp_ssid, wl.ssid[from], 64);
    char   tmp_pass[64]; memcpy(tmp_pass, wl.pass[from], 64);
    uint32_t tmp_last = wl.last[from];

    memcpy(wl.ssid[from], wl.ssid[to], 64);
    memcpy(wl.pass[from], wl.pass[to], 64);
    wl.last[from] = wl.last[to];

    memcpy(wl.ssid[to], tmp_ssid, 64);
    memcpy(wl.pass[to], tmp_pass, 64);
    wl.last[to] = tmp_last;

    nvs_save_wifi_list(&wl);
    json_ok(req);
    return ESP_OK;
}

/* POST /api/scan */
static esp_err_t api_scan_handler(httpd_req_t *req)
{
    TickType_t now = xTaskGetTickCount();

    if (!s_scan_started ||
        (uint32_t)((now - s_scan_start_tick) * portTICK_PERIOD_MS) < 2000) {

        if (!s_scan_started) {
            esp_wifi_scan_start(NULL, false);
            s_scan_started   = true;
            s_scan_start_tick = now;
        }

        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }

    /* Scan done – retrieve results */
    s_scan_started = false;

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }
    if (ap_count > 20) ap_count = 20;

    wifi_ap_record_t *records = malloc(ap_count * sizeof(wifi_ap_record_t));
    if (!records) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }

    esp_wifi_scan_get_ap_records(&ap_count, records);

    /* Build JSON array – 128 bytes per AP entry is plenty */
    char *out = malloc((size_t)ap_count * 128 + 8);
    if (!out) { free(records); json_fail(req); return ESP_OK; }

    int pos = 0;
    out[pos++] = '[';
    for (int i = 0; i < ap_count; i++) {
        const char *enc;
        switch (records[i].authmode) {
            case WIFI_AUTH_OPEN:    enc = "Open";  break;
            case WIFI_AUTH_WEP:     enc = "WEP";   break;
            case WIFI_AUTH_WPA_PSK: enc = "WPA";   break;
            case WIFI_AUTH_WPA2_ENTERPRISE:
            case WIFI_AUTH_WPA2_PSK: enc = "WPA2"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: enc = "WPA/WPA2"; break;
            case WIFI_AUTH_WPA3_PSK: enc = "WPA3"; break;
            default:                enc = "Other"; break;
        }
        pos += sprintf(out + pos, "%s{\"ssid\":\"%s\",\"rssi\":%d,\"enc\":\"%s\"}",
                       i ? "," : "",
                       (char *)records[i].ssid, records[i].rssi, enc);
    }
    out[pos++] = ']';
    out[pos]   = '\0';

    free(records);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    free(out);
    return ESP_OK;
}

/* POST /api/reboot */
static esp_err_t api_reboot_handler(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "");
    esp_restart();
    return ESP_OK;
}

/* GET /api/adc */
static esp_err_t api_adc_handler(httpd_req_t *req)
{
    float v_adc  = g_vbatt / 3.0f;  /* reverse 1:3 divider */
    float v_batt = g_vbatt;
    int   pct    = battery_percent;

    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"vAdc\":%.3f,\"vBatt\":%.3f,\"percent\":%d}",
             (double)v_adc, (double)v_batt, pct);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

/* GET /api/settings */
static esp_err_t api_settings_get_handler(httpd_req_t *req)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{"
             "\"version\":\"1.0\","
             "\"otaEnabled\":%s,"
             "\"otaIntervalMs\":%lu,"
             "\"wifiTxPower\":%u,"
             "\"failsafeMs\":%u,"
             "\"beaconIntervalMs\":%lu,"
             "\"apPrefix\":\"%s\","
             "\"alwaysStartPanel\":%s,"
             "\"steerInvert\":%s,"
             "\"steerGain\":%.4f,"
             "\"steerDeadzone\":%u,"
             "\"steerFilter\":%.4f,"
             "\"battWarnV\":%.4f,"
             "\"battOffV\":%.4f,"
             "\"maxThrottlePct\":%u,"
             "\"adcCorrFactor\":%.6f,"
             "\"vBatt\":%.3f"
             "}",
             settings.ota_enabled        ? "true" : "false",
             (unsigned long)settings.ota_interval_ms,
             settings.wifi_tx_power,
             settings.failsafe_ms,
             (unsigned long)settings.beacon_interval_ms,
             settings.ap_prefix,
             settings.always_start_panel ? "true" : "false",
             settings.steer_invert       ? "true" : "false",
             (double)settings.steer_gain,
             settings.steer_deadzone,
             (double)settings.steer_filter,
             (double)settings.batt_warn_v,
             (double)settings.batt_off_v,
             settings.max_throttle_pct,
             (double)settings.adc_corr_factor,
             (double)g_vbatt);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

/* POST /api/settings */
static esp_err_t api_settings_post_handler(httpd_req_t *req)
{
    char body[512];
    if (recv_body(req, body, sizeof(body)) < 0) { json_fail(req); return ESP_OK; }

    char val[64];

    if (get_param(body, "otaEnabled",      val, sizeof(val))) settings.ota_enabled        = atoi(val) != 0;
    if (get_param(body, "otaIntervalMs",   val, sizeof(val))) settings.ota_interval_ms    = (uint32_t)atol(val);
    if (get_param(body, "wifiTxPower",     val, sizeof(val))) settings.wifi_tx_power       = (uint8_t)atoi(val);
    if (get_param(body, "failsafeMs",      val, sizeof(val))) settings.failsafe_ms         = (uint16_t)atoi(val);
    if (get_param(body, "beaconIntervalMs",val, sizeof(val))) settings.beacon_interval_ms  = (uint32_t)atol(val);
    if (get_param(body, "apPrefix",        val, sizeof(val))) strlcpy(settings.ap_prefix, val, sizeof(settings.ap_prefix));
    if (get_param(body, "alwaysStartPanel",val, sizeof(val))) settings.always_start_panel  = atoi(val) != 0;
    if (get_param(body, "steerInvert",     val, sizeof(val))) settings.steer_invert        = atoi(val) != 0;
    if (get_param(body, "steerGain",       val, sizeof(val))) settings.steer_gain          = strtof(val, NULL);
    if (get_param(body, "steerDeadzone",   val, sizeof(val))) settings.steer_deadzone      = (uint16_t)atoi(val);
    if (get_param(body, "steerFilter",     val, sizeof(val))) settings.steer_filter        = strtof(val, NULL);
    if (get_param(body, "battWarnV",       val, sizeof(val))) settings.batt_warn_v         = strtof(val, NULL);
    if (get_param(body, "battOffV",        val, sizeof(val))) settings.batt_off_v          = strtof(val, NULL);
    if (get_param(body, "maxThrottlePct",  val, sizeof(val))) settings.max_throttle_pct    = (uint8_t)atoi(val);

    /* Special: recalibrate ADC correction factor */
    if (get_param(body, "adcRealVoltage", val, sizeof(val)) && g_vbatt > 0.0f) {
        float real_v = strtof(val, NULL);
        settings.adc_corr_factor = real_v * settings.adc_corr_factor / g_vbatt;
    }

    rc_settings_save();
    json_ok(req);
    return ESP_OK;
}

/* POST /api/restart-charge */
static esp_err_t api_restart_charge_handler(httpd_req_t *req)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_CHARGE_RESTART),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(PIN_CHARGE_RESTART, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(PIN_CHARGE_RESTART, 0);

    json_ok(req);
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Port-81 WebSocket server  –  web UI
 * ═══════════════════════════════════════════════════════════════════════════ */

static httpd_handle_t server81 = NULL;

static esp_err_t ws81_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WS81 handshake /");
        return ESP_OK;
    }

    httpd_ws_frame_t pkt = { .type = HTTPD_WS_TYPE_TEXT };
    uint8_t buf[128] = {0};
    pkt.payload = buf;

    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, sizeof(buf) - 1);
    if (ret != ESP_OK) return ret;

    char *text = (char *)pkt.payload;
    text[pkt.len] = '\0';

    if (strncmp(text, "CMD:", 4) == 0) {
        const char *cmd = text + 4;
        if (strcmp(cmd, "reboot") == 0) {
            esp_restart();
        }
        /* Other CMD: messages are silently ignored */
    }

    return ESP_OK;
}

static void broadcast_battery81(void)
{
    if (!server81) return;

    char msg[16];
    snprintf(msg, sizeof(msg), "BATT:%d", battery_percent);

    httpd_ws_frame_t pkt = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)msg,
        .len     = strlen(msg),
        .final   = true,
    };

    size_t fds = 0;
    httpd_get_client_list(server81, &fds, NULL);
    if (fds == 0) return;

    int *clients = malloc(fds * sizeof(int));
    if (!clients) return;
    httpd_get_client_list(server81, &fds, clients);

    for (size_t i = 0; i < fds; i++) {
        if (httpd_ws_get_fd_info(server81, clients[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(server81, clients[i], &pkt);
        }
    }
    free(clients);
}

static httpd_handle_t start_ws81_server(void)
{
    httpd_config_t cfg   = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = 81;
    cfg.ctrl_port        = 32769;
    cfg.max_uri_handlers = 4;
    cfg.max_open_sockets = 3;
    cfg.lru_purge_enable = true;

    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start port-81 HTTP server");
        return NULL;
    }

    static const httpd_uri_t ws81_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = ws81_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(srv, &ws81_uri);

    ESP_LOGI(TAG, "Port-81 WS server started");
    return srv;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Port-80 server start
 * ═══════════════════════════════════════════════════════════════════════════ */

static httpd_handle_t start_ws_server(void)
{
    httpd_config_t cfg   = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = WS_PORT;
    cfg.max_uri_handlers = 24;
    cfg.max_open_sockets = 5;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn     = httpd_uri_match_wildcard;

    httpd_handle_t srv = NULL;
    if (httpd_start(&srv, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start port-80 HTTP server");
        return NULL;
    }

    /* ── WebSocket /ws ── */
    static const httpd_uri_t ws_uri = {
        .uri          = "/ws",
        .method       = HTTP_GET,
        .handler      = ws_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(srv, &ws_uri);

    /* ── Captive portal URIs ── */
    static const httpd_uri_t gen204 = {
        .uri     = "/generate_204",
        .method  = HTTP_GET,
        .handler = generate_204_handler,
    };
    httpd_register_uri_handler(srv, &gen204);

    static const httpd_uri_t gen204b = {
        .uri     = "/gen_204",
        .method  = HTTP_GET,
        .handler = generate_204_handler,
    };
    httpd_register_uri_handler(srv, &gen204b);

    static const httpd_uri_t hotspot_detect = {
        .uri     = "/hotspot-detect.html",
        .method  = HTTP_GET,
        .handler = redirect_handler,
    };
    httpd_register_uri_handler(srv, &hotspot_detect);

    static const httpd_uri_t ncsi = {
        .uri     = "/ncsi.txt",
        .method  = HTTP_GET,
        .handler = redirect_handler,
    };
    httpd_register_uri_handler(srv, &ncsi);

    static const httpd_uri_t connecttest = {
        .uri     = "/connecttest.txt",
        .method  = HTTP_GET,
        .handler = redirect_handler,
    };
    httpd_register_uri_handler(srv, &connecttest);

    /* ── API endpoints ── */
    static const httpd_uri_t api_saved = {
        .uri     = "/api/saved",
        .method  = HTTP_GET,
        .handler = api_saved_handler,
    };
    httpd_register_uri_handler(srv, &api_saved);

    static const httpd_uri_t api_save = {
        .uri     = "/api/save",
        .method  = HTTP_POST,
        .handler = api_save_handler,
    };
    httpd_register_uri_handler(srv, &api_save);

    static const httpd_uri_t api_delete = {
        .uri     = "/api/delete",
        .method  = HTTP_POST,
        .handler = api_delete_handler,
    };
    httpd_register_uri_handler(srv, &api_delete);

    static const httpd_uri_t api_move = {
        .uri     = "/api/move",
        .method  = HTTP_POST,
        .handler = api_move_handler,
    };
    httpd_register_uri_handler(srv, &api_move);

    static const httpd_uri_t api_scan = {
        .uri     = "/api/scan",
        .method  = HTTP_POST,
        .handler = api_scan_handler,
    };
    httpd_register_uri_handler(srv, &api_scan);

    static const httpd_uri_t api_reboot = {
        .uri     = "/api/reboot",
        .method  = HTTP_POST,
        .handler = api_reboot_handler,
    };
    httpd_register_uri_handler(srv, &api_reboot);

    static const httpd_uri_t api_adc = {
        .uri     = "/api/adc",
        .method  = HTTP_GET,
        .handler = api_adc_handler,
    };
    httpd_register_uri_handler(srv, &api_adc);

    static const httpd_uri_t api_settings_get = {
        .uri     = "/api/settings",
        .method  = HTTP_GET,
        .handler = api_settings_get_handler,
    };
    httpd_register_uri_handler(srv, &api_settings_get);

    static const httpd_uri_t api_settings_post = {
        .uri     = "/api/settings",
        .method  = HTTP_POST,
        .handler = api_settings_post_handler,
    };
    httpd_register_uri_handler(srv, &api_settings_post);

    static const httpd_uri_t api_restart_charge = {
        .uri     = "/api/restart-charge",
        .method  = HTTP_POST,
        .handler = api_restart_charge_handler,
    };
    httpd_register_uri_handler(srv, &api_restart_charge);

    /* ── Static file handler (wildcard, must be last) ── */
    static const httpd_uri_t static_files = {
        .uri     = "/*",
        .method  = HTTP_GET,
        .handler = static_file_handler,
    };
    httpd_register_uri_handler(srv, &static_files);

    ESP_LOGI(TAG, "Port-80 HTTP server started");
    return srv;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * comms_task
 * ═══════════════════════════════════════════════════════════════════════════ */

void comms_task(void *arg)
{
    /* ── NVS init ── */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* ── Network init ── */
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wcfg);

    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
        on_sta_got_ip, NULL);
    WifiList wl;
    bool connected = false;

    if (nvs_load_wifi_list(&wl) == ESP_OK && wl.cnt > 0) {
        for (int i = 0; i < wl.cnt && !connected; i++) {
            ESP_LOGI(TAG, "Trying SSID: %s", wl.ssid[i]);
            connected = wifi_connect(wl.ssid[i], wl.pass[i]);
        }
    }

    /* ── Fall back to AP mode ── */
    if (!connected) {
        char ap_ssid[48];
        snprintf(ap_ssid, sizeof(ap_ssid), "%sCAR", settings.ap_prefix);
        wifi_start_ap(ap_ssid);
        xTaskCreate(dns_task, "dns", 4096, NULL, 5, NULL);
    }

    /* ── Set TX power ── */
    esp_wifi_set_max_tx_power(settings.wifi_tx_power);

    /* ── Start servers ── */
    httpd_handle_t server = start_ws_server();
    server81 = start_ws81_server();

    /* ── Start UDP discovery ── */
    xTaskCreate(udp_discovery_task, "udp_disc", 4096, NULL, 5, NULL);

    /* ── Main loop: battery broadcast + failsafe ── */
    TickType_t last_batt_tick = xTaskGetTickCount();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        check_failsafe();

        TickType_t now = xTaskGetTickCount();
        if ((uint32_t)((now - last_batt_tick) * portTICK_PERIOD_MS) >= 1000) {
            last_batt_tick = now;
            if (server)   broadcast_battery(server);
            broadcast_battery81();
        }
    }
}
