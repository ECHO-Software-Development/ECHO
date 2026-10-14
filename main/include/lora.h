#pragma once

#include "esp_random.h"

#define LORA_STATUS_TASK_PERIOD (1000 / portTICK_PERIOD_MS)

uint8_t get_lora_data(void);
void update_lora_data(void);