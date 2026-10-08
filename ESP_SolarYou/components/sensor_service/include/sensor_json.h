#ifndef SENSOR_JSON_H
#define SENSOR_JSON_H

#include <stddef.h>

#include "sensor_api.h"
#include "sensor_json_keys.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SENSOR_JSON_RECOMMENDED_CAPACITY 2048U

typedef enum {
    SENSOR_JSON_OK = 0,
    SENSOR_JSON_INVALID_ARGUMENT = 1,
    SENSOR_JSON_OUT_OF_MEMORY = 2,
    SENSOR_JSON_BUFFER_TOO_SMALL = 3
} sensor_json_result_t;

/* This only creates JSON. Sending it and retrying failed sends are not implemented yet.
 */
sensor_json_result_t sensor_snapshot_to_json(const sensor_snapshot_t *snapshot,
                                             char *output,
                                             size_t output_capacity,
                                             size_t *bytes_written);

#ifdef __cplusplus
}
#endif

#endif
