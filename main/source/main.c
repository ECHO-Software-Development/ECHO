#include "common.h"
#include "gap.h"
#include "gatt_svc.h"
#include "lora.h"

/* Library function declarations */
void ble_store_config_init(void);

/* Private function declarations */
static void nimble_host_config_init(void);
static void nimble_host_task(void *param);

/* Private functions */
/*
 *  Stack event callback functions
 *      - on_stack_reset is called when host resets BLE stack due to errors
 *      - on_stack_sync is called when host has synced with controller
 */
static void on_stack_reset(int reason) {
  ESP_LOGI(TAG, "nimble stack reset, reset reason: %d", reason);
}

static void on_stack_sync(void) {
  adv_init();
}

static void nimble_host_config_init(void) {
  /* Set host callbacks */
  ble_hs_cfg.reset_cb = on_stack_reset;
  ble_hs_cfg.sync_cb = on_stack_sync;
  ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  /* Security manager configuration */
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 1;
  ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

  /* Store host configuration */
  ble_store_config_init();
}

static void nimble_host_task(void *param) {
  ESP_LOGI(TAG, "nimble host task has been started!");

  /* This function won't return until nimble_port_stop() is executed */
  nimble_port_run();

  vTaskDelete(NULL);
}

static void lora_status_task(void *param) {
  ESP_LOGI(TAG, "lora status task has been started!");

  while (1) {
    update_lora_data();
    ESP_LOGI(TAG, "lora data updated to %d", get_lora_data());
    lora_status_notify();
    vTaskDelay(LORA_STATUS_TASK_PERIOD);
  }

  vTaskDelete(NULL);
}

void app_main(void) {
  /* Local variables */
  int rc = 0;
  uint32_t seed = esp_random();
  esp_err_t ret;

  /* Random generator initialization */
  srand(seed);

  /*
   * NVS flash initialization
   * Dependency of BLE stack to store configurations
   */
  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to initialize nvs flash, error code: %d ", ret);
    return;
  }

  /* NimBLE stack initialization */
  ret = nimble_port_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to initialize nimble stack, error code: %d ", ret);
    return;
  }

#if CONFIG_BT_NIMBLE_GAP_SERVICE
  /* GAP service initialization */
  rc = gap_init();
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to initialize GAP service, error code: %d", rc);
    return;
  }
#endif

  /* GATT server initialization */
  rc = gatt_svc_init();
  if (rc != 0) {
    ESP_LOGE(TAG, "failed to initialize GATT server, error code: %d", rc);
    return;
  }

  /* NimBLE host configuration initialization */
  nimble_host_config_init();

  /* Start NimBLE host task thread and return */
  xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, NULL);
  xTaskCreate(lora_status_task, "LoRa forwarder", 4 * 1024, NULL, 5, NULL);

  return;
}
