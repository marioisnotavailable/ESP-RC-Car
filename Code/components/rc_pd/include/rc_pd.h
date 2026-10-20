#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// STUSB4500 USB-PD sink controller (I2C, 7-bit addr 0x28).
// Hinweis: PDO-Schreibzugriffe zur Laufzeit sind fluechtig (RAM). Dauerhaft
// wird die Config nur per NVM-Flash des STUSB4500 -> hier nicht enthalten.
// Ohne NVM-Config bleibt der Chip nach Reset bei 5 V; rc_pd_init() setzt die
// Default-PDOs (5 V/1.5 A, 12 V/3 A) und loest eine Neuverhandlung aus.

typedef struct {
    bool     attached;       // PD-Vertrag aktiv (RDO object position != 0)
    uint8_t  object_pos;     // gewaehlter PDO (1..3), 0 = kein Vertrag
    uint16_t voltage_mv;     // vertraglich vereinbarte Spannung
} rc_pd_status_t;

esp_err_t rc_pd_init(void);
esp_err_t rc_pd_set_pdo(uint8_t pdo_num, uint16_t mv, uint16_t ma);
esp_err_t rc_pd_request_renegotiate(void);
esp_err_t rc_pd_get_status(rc_pd_status_t *out);
void      pd_task(void *arg);
