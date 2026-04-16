#pragma once
#include "rc_common.h"

#define WS_PORT                  80
#define DISCOVERY_PORT           49352
#define DISCOVERY_QUERY          "ESP_RC_DISCOVER"
#define DISCOVERY_RESP           "ESP_RC_HERE "
#define WIFI_CONNECT_TIMEOUT_MS  30000
#define FAILSAFE_DEFAULT_MS      500

void comms_task(void *arg);
