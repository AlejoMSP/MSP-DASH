/*
  canbed-sniffer
  Raw CAN sniffer for the Longan Labs CANBed (with Case) V1.0.

  Confirmed from official docs (docs.longan-labs.cc/1030022):
    - SPI CS pin: 17
    - Library: Longan's own Arduino library (mcp_can.h API), installed via
      Sketch > Include Library > Add .ZIP Library
    - Board: Tools > Board > Arduino Leonardo (uses standard Leonardo bootloader)

  rusEFI CAN config (confirmed via TunerStudio):
    - Bitrate: 500kbps
    - Base address 0x200, 11-bit standard frames (CAN dash type set to None,
      so no RX8 dash-emulation stream cluttering the bus anymore)

  Wiring: CANH/CANL screw terminals -> rusEFI Primary CAN bus.
  120 ohm termination switch: only enable if this board is at a physical
  bus end - check for double-termination before flipping it on.
*/

#include <SPI.h>
#include "mcp_can.h"

const int SPI_CS_PIN = 17; // confirmed CANBed pin per Longan docs

MCP_CAN CAN(SPI_CS_PIN);

void setup() {
  Serial.begin(115200);
  while (!Serial);

  while (CAN_OK != CAN.begin(CAN_500KBPS)) {
    Serial.println("CAN BUS FAIL - retrying...");
    delay(100);
  }
  Serial.println("CAN BUS OK - sniffing rusEFI...");
}

void loop() {
  unsigned char len = 0;
  unsigned char buf[8];

  if (CAN_MSGAVAIL == CAN.checkReceive()) {
    CAN.readMsgBuf(&len, buf);
    unsigned long canId = CAN.getCanId();

    Serial.print("ID: 0x");
    Serial.print(canId, HEX);
    Serial.print("  Len: ");
    Serial.print(len);
    Serial.print("  Data:");
    for (int i = 0; i < len; i++) {
      Serial.print(" ");
      if (buf[i] < 0x10) Serial.print("0");
      Serial.print(buf[i], HEX);
    }
    Serial.println();
  }
}
