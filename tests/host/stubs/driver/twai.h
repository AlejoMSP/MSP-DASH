#pragma once
#include <cstdint>
typedef int esp_err_t;
#define ESP_OK 0
typedef enum { TWAI_STATE_STOPPED, TWAI_STATE_RUNNING, TWAI_STATE_BUS_OFF, TWAI_STATE_RECOVERING } twai_state_t;
typedef struct { twai_state_t state; uint32_t msgs_to_tx, msgs_to_rx, tx_error_counter, rx_error_counter, tx_failed_count, rx_missed_count, rx_overrun_count, arb_lost_count, bus_error_count; } twai_status_info_t;
esp_err_t twai_get_status_info(twai_status_info_t *);

typedef enum { GPIO_NUM_43 = 43, GPIO_NUM_44 = 44 } gpio_num_t;
typedef enum { TWAI_MODE_NORMAL } twai_mode_t;
typedef struct { twai_mode_t mode; gpio_num_t tx_io, rx_io; int clkout_io, bus_off_io; uint32_t tx_queue_len, rx_queue_len; uint32_t alerts_enabled; uint32_t clkout_divider; int intr_flags; } twai_general_config_t;
#define TWAI_GENERAL_CONFIG_DEFAULT(tx, rx, m) { m, tx, rx, -1, -1, 5, 5, 0, 0, 0 }
typedef struct { uint32_t brp; uint8_t tseg_1, tseg_2, sjw; bool triple_sampling; } twai_timing_config_t;
#define TWAI_TIMING_CONFIG_500KBITS() { 8, 15, 4, 3, false }
typedef struct { uint32_t acceptance_code, acceptance_mask; bool single_filter; } twai_filter_config_t;
#define TWAI_FILTER_CONFIG_ACCEPT_ALL() { 0, 0xFFFFFFFF, true }
typedef struct { uint32_t flags; uint32_t identifier; uint8_t data_length_code; uint8_t data[8]; } twai_message_t;
esp_err_t twai_driver_install(const twai_general_config_t *, const twai_timing_config_t *, const twai_filter_config_t *);
esp_err_t twai_start(void); esp_err_t twai_receive(twai_message_t *, int); esp_err_t twai_initiate_recovery(void);
