#include "gatt_svc.h"

#include "common.h"
#include "gap.h"
#include "host/ble_gatt.h"
#include "lora.h"


static int lora_rx_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static int lora_tx_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static int lora_status_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg);


static const ble_uuid128_t lora_svc_uuid = BLE_UUID128_INIT(
  0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe
);

static const ble_uuid128_t lora_rx_chr_uuid = BLE_UUID128_INIT(
  0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xff
);
static uint8_t lora_rx_val[2] = {0};
static uint16_t lora_rx_val_handle;

static const ble_uuid128_t lora_tx_chr_uuid = BLE_UUID128_INIT(
  0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0x00
);
static uint16_t lora_tx_val_handle;


static const ble_uuid128_t lora_status_chr_uuid = BLE_UUID128_INIT(
  0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0x01
);
static uint16_t lora_status_val_handle;
static bool lora_status_conn_handle = false;
static bool lora_status_notify_enabled = false;

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
  {
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &lora_svc_uuid.u,
    .characteristics =
      (struct ble_gatt_chr_def[]){
        {
          // LoRa RX: receive packets via notifications
          .uuid = &lora_rx_chr_uuid.u,
          .access_cb = lora_rx_access,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
          .val_handle = &lora_rx_val_handle,
        },
        {
          // LoRa TX: send packets to the radio
          .uuid = &lora_tx_chr_uuid.u,
          .access_cb = lora_tx_access,
          .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
          .val_handle = &lora_tx_val_handle,
        },
        {
          // LoRa Status: radio state, RSSI, SNR, errors
          .uuid = &lora_status_chr_uuid.u,
          .access_cb = lora_status_access,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
          .val_handle = &lora_status_val_handle,
        },
        {0},
      },
  },
  {0},
};

static int lora_rx_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
      if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "characteristic read; conn_handle=%d attr_handle=%d", conn_handle,
                 attr_handle);
      } else {
        ESP_LOGI(TAG, "characteristic read by nimble stack; attr_handle=%d", attr_handle);
      }

      if (attr_handle == lora_rx_val_handle) {
        lora_rx_val[0] = get_lora_data();
        lora_rx_val[1] = 'r'; // just for test
        int rc = os_mbuf_append(ctxt->om, &lora_rx_val, sizeof(lora_rx_val));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
      }
      goto error;

    // unknown event
    default:
      goto error;
  }

error:
  ESP_LOGE(TAG, "unexpected access operation to heart rate characteristic, opcode: %d", ctxt->op);
  return BLE_ATT_ERR_UNLIKELY;
}

static int lora_tx_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_WRITE_CHR:
      if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "characteristic write; conn_handle=%d attr_handle=%d", conn_handle,
                 attr_handle);
      } else {
        ESP_LOGI(TAG, "characteristic write by nimble stack; attr_handle=%d", attr_handle);
      }

      if (attr_handle == lora_tx_val_handle) {
        ESP_LOGI(TAG, "Got to send to LoRa (%d): %.*s", ctxt->om->om_len, ctxt->om->om_len,
                 (char *)ctxt->om->om_data);
        return 0;
      }
      goto error;

    // unknown event
    default:
      goto error;
  }

error:
  ESP_LOGE(TAG, "unexpected access operation to heart rate characteristic, opcode: %d", ctxt->op);
  return BLE_ATT_ERR_UNLIKELY;
}

static int lora_status_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg) {
  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
      if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "characteristic read; conn_handle=%d attr_handle=%d", conn_handle,
                 attr_handle);
      } else {
        ESP_LOGI(TAG, "characteristic read by nimble stack; attr_handle=%d", attr_handle);
      }

      if (attr_handle == lora_status_val_handle) {
        lora_rx_val[0] = get_lora_data();
        lora_rx_val[1] = 's'; // just for test
        int rc = os_mbuf_append(ctxt->om, &lora_rx_val, sizeof(lora_rx_val));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
      }
      goto error;

    // unknown event
    default:
      goto error;
  }

error:
  ESP_LOGE(TAG, "unexpected access operation to heart rate characteristic, opcode: %d", ctxt->op);
  return BLE_ATT_ERR_UNLIKELY;
}

#if 0
void send_lora_indication(void) {
  if (!test_chr_conn_handle_inited) return;

  if (test_ind_status && is_connection_encrypted(test_chr_conn_handle)) {
    ble_gatts_indicate(test_chr_conn_handle, test_chr_val_handle);
  }
}
#endif

int lora_status_notify(void)
{
    char buf[32] = "hello";
    snprintf(buf, sizeof(buf), "received: %d", get_lora_data());

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, strlen(buf));

    if (!om) return BLE_HS_ENOMEM;

    // TODO: check for encryption
    return ble_gatts_notify_custom(lora_status_conn_handle, lora_status_val_handle, om);
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
  /* Local variables */
  char buf[BLE_UUID_STR_LEN];

  /* Handle GATT attributes register events */
  switch (ctxt->op) {
    /* Service register event */
    case BLE_GATT_REGISTER_OP_SVC:
      ESP_LOGD(TAG, "registered service %s with handle=%d",
               ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
      break;

    /* Characteristic register event */
    case BLE_GATT_REGISTER_OP_CHR:
      ESP_LOGD(TAG,
               "registering characteristic %s with "
               "def_handle=%d val_handle=%d",
               ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf), ctxt->chr.def_handle,
               ctxt->chr.val_handle);
      break;

    /* Descriptor register event */
    case BLE_GATT_REGISTER_OP_DSC:
      ESP_LOGD(TAG, "registering descriptor %s with handle=%d",
               ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
      break;

    // Unknown event
    default:
      assert(0);
      break;
  }
}

int gatt_svr_subscribe_cb(struct ble_gap_event *event) {
  if (event->subscribe.attr_handle == lora_status_val_handle) {
    lora_status_conn_handle = event->subscribe.conn_handle;
    lora_status_notify_enabled = event->subscribe.cur_notify;

    ESP_LOGI("LoRa", "Status notifications %s", lora_status_notify_enabled? "enabled" : "disabled");

    if (!is_connection_encrypted(event->subscribe.conn_handle)) {
      ESP_LOGE(TAG, "failed to subscribe to LoRa status notifications, connection not encrypted!");
      return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
  }
  return 0;
}

int gatt_svc_init(void) {

  ble_svc_gatt_init();

  int rc = ble_gatts_count_cfg(gatt_svr_svcs);
  if (rc != 0) return rc;

  rc = ble_gatts_add_svcs(gatt_svr_svcs);
  if (rc != 0) return rc;

  return 0;
}
