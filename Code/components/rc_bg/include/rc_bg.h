#pragma once
#include "rc_common.h"

#define FOTA_FW_URL "https://github.com/marioisnotavailable/ESP-RC-Car/releases/latest/download/esp-rc-car.bin"

void bg_task(void *arg);
void rc_execute_command(const char *cmd, char *out, size_t out_len);
