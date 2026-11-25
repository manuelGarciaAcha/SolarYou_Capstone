#ifndef SOLARYOU_BLE_TELEMETRY_H
#define SOLARYOU_BLE_TELEMETRY_H
#include "ble_protocol.h"
#include "esp_err.h"

esp_err_t ble_telemetry_init(void);
/* Copies into a one-element latest-value mailbox. Never waits for Bluetooth. */
void ble_telemetry_publish(const sy_telemetry_sample_t *sample);
#endif
