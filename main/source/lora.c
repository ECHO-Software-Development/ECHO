#include "common.h"
#include "lora.h"

static uint8_t lora_data;

uint8_t get_lora_data(void) { return lora_data; }
void update_lora_data(void) { lora_data = (uint8_t)(esp_random() % 100); }
