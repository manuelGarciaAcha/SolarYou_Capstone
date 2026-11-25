#ifndef SOLARYOU_BLE_APP_H
#define SOLARYOU_BLE_APP_H
#include <stdbool.h>

/* The application supplies readings; this module does not poll sensors. */
void ble_app_start(void);
void ble_app_update_imu(float pitch, float roll, bool valid);
void ble_app_update_targets(float pitch, float roll, bool valid);
void ble_app_update_power(float voltage, float current, float watts,
                          float avg_watts, bool valid);
void ble_app_poll(void);
#endif
