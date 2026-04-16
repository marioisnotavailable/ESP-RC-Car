#include "rc_common.h"

QueueHandle_t       cmd_queue;
QueueHandle_t       batt_queue;
EventGroupHandle_t  rc_events;

void rc_common_init(void) {
    cmd_queue  = xQueueCreate(1, sizeof(Cmd));
    batt_queue = xQueueCreate(1, sizeof(int));
    rc_events  = xEventGroupCreate();

    configASSERT(cmd_queue);
    configASSERT(batt_queue);
    configASSERT(rc_events);
}
