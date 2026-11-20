#pragma once
#include <cstdint>
typedef int esp_err_t;
#define ESP_OK 0
typedef enum { TWAI_STATE_STOPPED, TWAI_STATE_RUNNING, TWAI_STATE_BUS_OFF, TWAI_STATE_RECOVERING } twai_state_t;
typedef struct { twai_state_t state; uint32_t msgs_to_tx, msgs_to_rx, tx_error_counter, rx_error_counter, tx_failed_count, rx_missed_count, rx_overrun_count, arb_lost_count, bus_error_count; } twai_status_info_t;
esp_err_t twai_get_status_info(twai_status_info_t *);

typedef enum { GPIO_NUM_0 = 0, GPIO_NUM_1 = 1, GPIO_NUM_2 = 2, GPIO_NUM_3 = 3, GPIO_NUM_4 = 4, GPIO_NUM_5 = 5, GPIO_NUM_6 = 6, GPIO_NUM_7 = 7, GPIO_NUM_8 = 8, GPIO_NUM_9 = 9, GPIO_NUM_10 = 10, GPIO_NUM_11 = 11, GPIO_NUM_12 = 12, GPIO_NUM_13 = 13, GPIO_NUM_14 = 14, GPIO_NUM_15 = 15, GPIO_NUM_16 = 16, GPIO_NUM_17 = 17, GPIO_NUM_18 = 18, GPIO_NUM_19 = 19, GPIO_NUM_20 = 20, GPIO_NUM_21 = 21, GPIO_NUM_22 = 22, GPIO_NUM_23 = 23, GPIO_NUM_24 = 24, GPIO_NUM_25 = 25, GPIO_NUM_26 = 26, GPIO_NUM_27 = 27, GPIO_NUM_28 = 28, GPIO_NUM_29 = 29, GPIO_NUM_30 = 30, GPIO_NUM_31 = 31, GPIO_NUM_32 = 32, GPIO_NUM_33 = 33, GPIO_NUM_34 = 34, GPIO_NUM_35 = 35, GPIO_NUM_36 = 36, GPIO_NUM_37 = 37, GPIO_NUM_38 = 38, GPIO_NUM_39 = 39, GPIO_NUM_40 = 40, GPIO_NUM_41 = 41, GPIO_NUM_42 = 42, GPIO_NUM_43 = 43, GPIO_NUM_44 = 44, GPIO_NUM_45 = 45, GPIO_NUM_46 = 46, GPIO_NUM_47 = 47, GPIO_NUM_48 = 48 } gpio_num_t;
typedef enum { TWAI_MODE_NORMAL } twai_mode_t;
typedef struct { twai_mode_t mode; gpio_num_t tx_io, rx_io; int clkout_io, bus_off_io; uint32_t tx_queue_len, rx_queue_len; uint32_t alerts_enabled; uint32_t clkout_divider; int intr_flags; } twai_general_config_t;
#define TWAI_GENERAL_CONFIG_DEFAULT(tx, rx, m) { m, tx, rx, -1, -1, 5, 5, 0, 0, 0 }
typedef struct { uint32_t brp; uint8_t tseg_1, tseg_2, sjw; bool triple_sampling; } twai_timing_config_t;
#define TWAI_TIMING_CONFIG_500KBITS() { 8, 15, 4, 3, false }
typedef struct { uint32_t acceptance_code, acceptance_mask; bool single_filter; } twai_filter_config_t;
#define TWAI_FILTER_CONFIG_ACCEPT_ALL() { 0, 0xFFFFFFFF, true }
typedef struct { union { struct { uint32_t extd: 1, rtr: 1, ss: 1, self: 1, dlc_non_comp: 1, reserved: 27; }; uint32_t flags; }; uint32_t identifier; uint8_t data_length_code; uint8_t data[8]; } twai_message_t;
esp_err_t twai_driver_install(const twai_general_config_t *, const twai_timing_config_t *, const twai_filter_config_t *);
esp_err_t twai_start(void); esp_err_t twai_receive(twai_message_t *, int); esp_err_t twai_initiate_recovery(void);

esp_err_t twai_transmit(const twai_message_t *, uint32_t);
