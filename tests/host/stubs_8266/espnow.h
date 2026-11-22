#pragma once
#include <stdint.h>
#define ESP_NOW_ROLE_SLAVE 2
typedef void (*esp_now_recv_cb_t)(uint8_t *, uint8_t *, uint8_t);
inline int esp_now_init() { return 0; } inline int esp_now_set_self_role(uint8_t) { return 0; } inline int esp_now_register_recv_cb(esp_now_recv_cb_t) { return 0; }
