#pragma once
#include <stdint.h>
#include <stddef.h>
#ifndef ESP_OK
#define ESP_OK 0
#endif
typedef enum { WIFI_SECOND_CHAN_NONE = 0 } wifi_second_chan_t;
#define WIFI_IF_STA 0
inline int esp_wifi_set_promiscuous(bool) { return 0; }
inline int esp_wifi_set_channel(uint8_t, wifi_second_chan_t) { return 0; }
inline int esp_wifi_get_channel(uint8_t *c, wifi_second_chan_t *s) { *c = 1; *s = WIFI_SECOND_CHAN_NONE; return 0; }
