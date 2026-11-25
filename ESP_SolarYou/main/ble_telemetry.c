#include "ble_telemetry.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static QueueHandle_t latest_queue;

#define ADVERTISED_DEVICE_NAME SY_BLE_DEVICE_NAME

#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_att.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE";
static portMUX_TYPE session_lock = portMUX_INITIALIZER_UNLOCKED;
static sy_ble_session_t session;
static QueueHandle_t reply_queue;
static uint16_t connection = BLE_HS_CONN_HANDLE_NONE;
static uint16_t telemetry_handle;
static uint8_t own_address_type;

static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(
    0x3d,0x29,0xeb,0xf4,0xb6,0xa7,0x68,0xaf,0xe2,0x4a,0xc9,0x04,0x1f,0x84,0xe8,0x47);
static const ble_uuid128_t telemetry_uuid = BLE_UUID128_INIT(
    0x0b,0x32,0x99,0x3a,0x01,0xd8,0xac,0x8e,0x24,0x4e,0xbc,0x66,0x0c,0x68,0xd2,0xf2);
/* Rate-setting characteristic used by the dashboard. */
static const ble_uuid128_t rate_uuid = BLE_UUID128_INIT(
    0x0c,0x32,0x99,0x3a,0x01,0xd8,0xac,0x8e,0x24,0x4e,0xbc,0x66,0x0c,0x68,0xd2,0xf2);

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static int access_telemetry(uint16_t conn, uint16_t attr,
                           struct ble_gatt_access_ctxt *context, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    /* NimBLE requires a callback even for a notify-only characteristic. */
    return context->op == BLE_GATT_ACCESS_OP_READ_CHR
        ? BLE_ATT_ERR_READ_NOT_PERMITTED : BLE_ATT_ERR_WRITE_NOT_PERMITTED;
}

static int access_rate(uint16_t conn, uint16_t attr,
                       struct ble_gatt_access_ctxt *context, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (context->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char reply[24];
        portENTER_CRITICAL(&session_lock);
        unsigned hz = session.rate_hz;
        portEXIT_CRITICAL(&session_lock);
        size_t length = sy_ble_rate_reply(reply, sizeof(reply), hz);
        return os_mbuf_append(context->om, reply, (uint16_t)length) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    uint8_t command[32]; uint16_t length = 0U; unsigned hz = 0U;
    if (OS_MBUF_PKTLEN(context->om) > sizeof(command) ||
        ble_hs_mbuf_to_flat(context->om, command, sizeof(command), &length) != 0 ||
        !sy_ble_parse_rate_command(command, length, &hz)) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    uint32_t now = now_ms();
    portENTER_CRITICAL(&session_lock);
    (void)sy_ble_session_set_rate(&session, hz, now);
    portEXIT_CRITICAL(&session_lock);
    /* The readable characteristic is authoritative even if a notification is lost. */
    (void)xQueueOverwrite(reply_queue, &hz);
    ESP_LOGI(TAG, "Outgoing telemetry rate applied: %u Hz", hz);
    return 0;
}

static const struct ble_gatt_svc_def services[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &service_uuid.u,
      .characteristics = (struct ble_gatt_chr_def[]) {
          { .uuid = &telemetry_uuid.u, .access_cb = access_telemetry,
            .flags = BLE_GATT_CHR_F_NOTIFY, .val_handle = &telemetry_handle },
          { .uuid = &rate_uuid.u, .access_cb = access_rate,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE },
          {0}
      } },
    {0}
};

static int gap_event(struct ble_gap_event *event, void *arg);
static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) { ESP_LOGE(TAG, "Advertising fields failed: %d", rc); return; }
    struct ble_hs_adv_fields response = {0};
    response.name = (const uint8_t *)ADVERTISED_DEVICE_NAME;
    response.name_len = (uint8_t)strlen(ADVERTISED_DEVICE_NAME);
    response.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) { ESP_LOGE(TAG, "Scan response failed: %d", rc); return; }
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_address_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0) ESP_LOGE(TAG, "Advertising start failed: %d", rc);
    else ESP_LOGI(TAG, "Advertising as %s", ADVERTISED_DEVICE_NAME);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    uint32_t now = now_ms();
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) { advertise(); break; }
        portENTER_CRITICAL(&session_lock);
        connection = event->connect.conn_handle;
        sy_ble_session_connect(&session);
        portEXIT_CRITICAL(&session_lock);
        (void)xQueueReset(reply_queue);
        ESP_LOGI(TAG, "Connected; waiting for notification subscription");
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        portENTER_CRITICAL(&session_lock);
        connection = BLE_HS_CONN_HANDLE_NONE;
        sy_ble_session_disconnect(&session);
        portEXIT_CRITICAL(&session_lock);
        (void)xQueueReset(reply_queue);
        ESP_LOGI(TAG, "Disconnected; restarting advertising");
        advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == telemetry_handle) {
            portENTER_CRITICAL(&session_lock);
            sy_ble_session_subscribe(&session, event->subscribe.cur_notify != 0, now);
            unsigned hz = session.rate_hz;
            portEXIT_CRITICAL(&session_lock);
            (void)xQueueOverwrite(reply_queue, &hz);
        }
        break;
    case BLE_GAP_EVENT_MTU:
        portENTER_CRITICAL(&session_lock);
        session.mtu = event->mtu.value;
        portEXIT_CRITICAL(&session_lock);
        ESP_LOGI(TAG, "Negotiated MTU %u", (unsigned)event->mtu.value);
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    default: break;
    }
    return 0;
}

static void host_reset(int reason)
{
    portENTER_CRITICAL(&session_lock);
    connection = BLE_HS_CONN_HANDLE_NONE;
    sy_ble_session_disconnect(&session);
    portEXIT_CRITICAL(&session_lock);
    ESP_LOGW(TAG, "BLE host reset: %d", reason);
}
static void host_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &own_address_type);
    if (rc != 0) { ESP_LOGE(TAG, "BLE identity failed: %d", rc); return; }
    advertise();
}
static void host_task(void *arg)
{
    (void)arg; nimble_port_run(); nimble_port_freertos_deinit();
}

static bool send_json(const char *json, size_t length, const sy_ble_session_t *s,
                       uint16_t conn, uint16_t sequence)
{
    /* Query the live ATT channel; the cached GAP event can still hold the
     * default MTU after the connection has negotiated a larger value. */
    uint16_t mtu = ble_att_mtu(conn);
    size_t limit = mtu > 3U ? (size_t)mtu - 3U : 0U;
    if (limit > 512U) limit = 512U;
    size_t count = sy_ble_fragment_count(length, limit);
    for (size_t i = 0U; i < count; ++i) {
        portENTER_CRITICAL(&session_lock);
        bool current = session.connected && session.subscribed && session.generation == s->generation && connection == conn;
        portEXIT_CRITICAL(&session_lock);
        if (!current) return false;
        uint8_t packet[512];
        size_t bytes = sy_ble_fragment((const uint8_t *)json, length, sequence, limit, i, packet, sizeof(packet));
        if (bytes == 0U) return false;
        struct os_mbuf *buffer = ble_hs_mbuf_from_flat(packet, (uint16_t)bytes);
        if (buffer == NULL) return false;
        /* NimBLE owns the mbuf after this call, including on error. */
        int rc = ble_gatts_notify_custom(conn, telemetry_handle, buffer);
        if (rc != 0) { ESP_LOGD(TAG, "Notification dropped: %d", rc); return false; }
    }
    return count != 0U;
}

static void sender_task(void *arg)
{
    (void)arg; uint16_t message_sequence = 0U;
    for (;;) {
        uint32_t now = now_ms();
        portENTER_CRITICAL(&session_lock);
        bool due = sy_ble_session_due(&session, now);
        sy_ble_session_t state = session;
        uint16_t conn = connection;
        portEXIT_CRITICAL(&session_lock);
        if (state.connected && state.subscribed) {
            unsigned hz;
            if (xQueueReceive(reply_queue, &hz, 0) == pdTRUE) {
                char reply[24]; size_t n = sy_ble_rate_reply(reply, sizeof(reply), hz);
                (void)send_json(reply, n, &state, conn, message_sequence++);
            }
            if (due) {
                sy_telemetry_sample_t sample;
                if (xQueuePeek(latest_queue, &sample, 0) == pdTRUE) {
                    /* The application can publish on the other core after the
                     * scheduler timestamp above. Read time after copying the
                     * sample so a newer sample cannot underflow its age. */
                    uint32_t sample_now = now_ms();
                    if ((uint32_t)(sample_now - sample.captured_ms) > 1000U) {
                        sample.imu_valid = false; sample.power_valid = false; sample.targets_valid = false;
                    }
                    char json[SY_BLE_JSON_CAPACITY];
                    size_t n = sy_telemetry_format(json, sizeof(json), &sample);
                    if (n > 0U) (void)send_json(json, n, &state, conn, message_sequence++);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t ble_telemetry_init(void)
{
    latest_queue = xQueueCreate(1, sizeof(sy_telemetry_sample_t));
    if (latest_queue == NULL) return ESP_ERR_NO_MEM;
    if (strlen(ADVERTISED_DEVICE_NAME) == 0U || strlen(ADVERTISED_DEVICE_NAME) > 29U) return ESP_ERR_INVALID_ARG;
    reply_queue = xQueueCreate(1, sizeof(unsigned));
    if (reply_queue == NULL) return ESP_ERR_NO_MEM;
    sy_ble_session_init(&session);
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "NVS init failed; existing data was not erased"); return err; }
    err = nimble_port_init();
    if (err != ESP_OK) return err;
    ble_svc_gap_init(); ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set(ADVERTISED_DEVICE_NAME);
    if (rc == 0) rc = ble_gatts_count_cfg(services);
    if (rc == 0) rc = ble_gatts_add_svcs(services);
    if (rc == 0) rc = ble_att_set_preferred_mtu(185);
    if (rc != 0) return ESP_FAIL;
    ble_hs_cfg.reset_cb = host_reset;
    ble_hs_cfg.sync_cb = host_sync;
    if (xTaskCreate(sender_task, "ble_send", 4096, NULL, 2, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

void ble_telemetry_publish(const sy_telemetry_sample_t *sample)
{
    if (sample != NULL && latest_queue != NULL) (void)xQueueOverwrite(latest_queue, sample);
}
