/*
  esp32-can-sniffer
  Throwaway diagnostic sketch: dumps raw CAN frames (ID + bytes) to Serial.

  Use this against the LIVE rusEFI to:
    1. Confirm wiring/transceiver/bit-rate work at all.
    2. Reverse-engineer frame layout + scale factors: compare raw byte values
       here to what TunerStudio's own live gauges show at the same instant.

  rusEFI CAN config (confirmed via TunerStudio):
    - Base address: 512 decimal (0x200), 11-bit standard frames
    - Bitrate: 500kbps
    - Broadcast period: 50ms (20Hz)
    - NOTE: rusEFI also emits a separate Mazda RX8 dash-emulation stream on
      other IDs - this sketch filters to print only IDs >= 0x200 by default
      to reduce noise, adjust ID_FILTER_MIN if you want to see everything.

  Wiring (adjust pins to your board):
    ESP32 GPIO21 -> CAN transceiver TX
    ESP32 GPIO22 -> CAN transceiver RX
    Transceiver CANH/CANL -> bus (Primary CAN on rusEFI: PD0/PD1)
*/

#include "driver/twai.h"

#define CAN_TX_PIN GPIO_NUM_21
#define CAN_RX_PIN GPIO_NUM_22
#define CAN_BITRATE TWAI_TIMING_CONFIG_500KBITS()

#define RUSEFI_BASE_ID 0x200
#define ID_FILTER_MIN  0x200 // set to 0 to see everything including RX8 dash emulation frames

void setup() {
  Serial.begin(115200);
  delay(500);

  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_LISTEN_ONLY);
  twai_timing_config_t t_config = CAN_BITRATE;
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
    Serial.println("Failed to install TWAI driver");
    while (1) delay(1000);
  }
  if (twai_start() != ESP_OK) {
    Serial.println("Failed to start TWAI driver");
    while (1) delay(1000);
  }

  Serial.println("CAN sniffer running (rusEFI, base 0x200)...");
}

void loop() {
  twai_message_t message;
  if (twai_receive(&message, pdMS_TO_TICKS(1000)) == ESP_OK) {
    if (message.identifier < ID_FILTER_MIN) return; // skip RX8 dash emulation frames

    Serial.printf("ID: 0x%03X  (base+%d)  DLC: %d  Data:", message.identifier, message.identifier - RUSEFI_BASE_ID, message.data_length_code);
    for (int i = 0; i < message.data_length_code; i++) {
      Serial.printf(" %02X", message.data[i]);
    }
    Serial.println();
  }
}
