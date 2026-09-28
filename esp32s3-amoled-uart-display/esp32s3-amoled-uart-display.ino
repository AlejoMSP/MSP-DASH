/*
  esp32s3-amoled-uart-display
  Round AMOLED gauge display for the Waveshare ESP32-S3-Touch-AMOLED-1.43,
  receiving pre-decoded ECU data over a UART link from the WROOM board
  (esp32-main.ino) instead of reading CAN directly.

  This REPLACES the standalone CAN-reading approach in
  esp32s3-amoled-linkecu-numbers/ per project decision: rather than
  duplicating a CAN transceiver, voltage divider, and separate 5V supply
  on THIS board too, it just receives already-decoded values over a
  simple wired UART link. WROOM owns all CAN decoding (rusEFI +
  MegaSquirt/LinkECU via its Active Secondary ECU selector) - this board
  just displays whatever field/value data it's sent, same protocol the
  physical Nextion display already uses.

  UART link (from WROOM's esp32-main.ino AmoledLink section):
    WROOM GPIO25 (TX) -> this board's GPIO2 (RX)
    WROOM GPIO26 (RX) -> this board's GPIO1 (TX) - not used yet, wired
                          for possible future bidirectional use
    WROOM GND         -> this board's GND (common ground required)
    Baud: 115200

  Protocol: identical to what nextionSend() sends the physical Nextion
  display - "field.txt=\"value\"" or "field.val=value", terminated by
  three 0xFF bytes. This board buffers incoming bytes until it sees that
  terminator, parses the field/value, and updates the matching on-screen
  field if it's one this display shows.

  Display config (confirmed working via prior bring-up testing):
    - Driver: CO5300, QSPI clock: 10MHz
    - Visible area: 466x466, centered via offset=7,7,7,7
    - Tearing fix: opaque-background text (setTextColor(fg,bg)), one
      write per field, only redrawn when the value actually changed
*/

#include <Arduino_GFX_Library.h>

// ---- Display pins (confirmed) ----
#define AMOLED_CS    9
#define AMOLED_SCK   10
#define AMOLED_D0    11
#define AMOLED_D1    12
#define AMOLED_D2    13
#define AMOLED_D3    14
#define AMOLED_RST   21
#define AMOLED_EN    42

// ---- UART link to the WROOM board ----
#define LINK_RX_PIN 2  // receives WROOM's TX (its GPIO25)
#define LINK_TX_PIN 1  // sends to WROOM's RX (its GPIO26) - not used yet
#define LINK_BAUD 115200

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  AMOLED_CS, AMOLED_SCK, AMOLED_D0, AMOLED_D1, AMOLED_D2, AMOLED_D3);

Arduino_GFX *gfx = new Arduino_CO5300(
  bus, AMOLED_RST, 0, 466, 466,
  7, 7, 7, 7);

#define COLOR_BG    0x0000
#define COLOR_LABEL 0x4E9C // approx web dashboard's cyan #4dd0e1
#define COLOR_VALUE 0xFFFF

// ---- Fields this display shows - keyed by the SAME gauge names WROOM
// uses (rpm, map, clt, tps, ego, battv, mat) - works whether WROOM is
// currently feeding these from rusEFI or from MegaSquirt/LinkECU, since
// both write to the same keys. ----
struct Field {
  const char* key;
  const char* label;
  int16_t labelX, labelY, valueX, valueY;
  uint8_t valueSize, fieldWidth;
  char lastDrawn[16];
};

Field fields[] = {
  { "rpm",   "RPM",  170, 60,  120, 95,  7, 6, "" },
  { "map",   "MAP",  70,  240, 55,  270, 4, 6, "" },
  { "clt",   "CLT",  270, 240, 255, 270, 4, 6, "" },
  { "tps",   "TPS",  70,  320, 55,  350, 4, 6, "" },
  { "ego",   "AFR",  270, 320, 255, 350, 4, 6, "" },
  { "battv", "BATT", 70,  400, 55,  430, 4, 6, "" },
  { "mat",   "MAT",  270, 400, 255, 430, 4, 6, "" },
};
const int NUM_FIELDS = sizeof(fields) / sizeof(fields[0]);

int findField(const String &key) {
  for (int i = 0; i < NUM_FIELDS; i++) {
    if (key == fields[i].key) return i;
  }
  return -1;
}

void drawLabels() {
  gfx->setTextColor(COLOR_LABEL, COLOR_BG);
  gfx->setTextSize(2);
  for (int i = 0; i < NUM_FIELDS; i++) {
    gfx->setCursor(fields[i].labelX, fields[i].labelY);
    gfx->print(fields[i].label);
  }
}

// Opaque-background, fixed-width single write - same tearing fix
// confirmed working on the clock test sketch. Skips the write entirely
// if the value hasn't actually changed since last drawn.
void updateField(int idx, const String &value) {
  Field &f = fields[idx];
  char buf[16];
  snprintf(buf, sizeof(buf), "%-*s", f.fieldWidth, value.c_str());

  if (strcmp(buf, f.lastDrawn) == 0) return;
  strcpy(f.lastDrawn, buf);

  gfx->setTextColor(COLOR_VALUE, COLOR_BG);
  gfx->setTextSize(f.valueSize);
  gfx->setCursor(f.valueX, f.valueY);
  gfx->print(buf);
}

// ---- UART receive buffer/parser - mirrors how nextionSend() builds
// its commands, in reverse. ----
String rxBuffer = "";
uint8_t ffCount = 0;

void processLine(const String &line) {
  int dot = line.indexOf('.');
  int eq = line.indexOf('=');
  if (dot <= 0 || eq <= dot) return; // malformed - drop silently

  String field = line.substring(0, dot);
  String value = line.substring(eq + 1);
  value.replace("\"", "");
  value.trim();

  int idx = findField(field);
  if (idx >= 0) updateField(idx, value);
  // fields WROOM sends that this display doesn't show are silently
  // ignored - this board only cares about its own 7 fields
}

void pollUartLink() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    if (b == 0xFF) {
      ffCount++;
      if (ffCount >= 3) {
        if (rxBuffer.length() > 0) processLine(rxBuffer);
        rxBuffer = "";
        ffCount = 0;
      }
    } else {
      ffCount = 0;
      rxBuffer += (char)b;
      if (rxBuffer.length() > 64) rxBuffer = ""; // malformed/oversized - drop and resync
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("UART display starting...");

  pinMode(AMOLED_EN, OUTPUT);
  digitalWrite(AMOLED_EN, HIGH);

  if (!gfx->begin(10000000)) {
    Serial.println("gfx->begin() FAILED");
  } else {
    Serial.println("gfx->begin() succeeded");
  }
  gfx->fillScreen(COLOR_BG);
  drawLabels();

  Serial1.begin(LINK_BAUD, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN);
  Serial.println("UART link ready - waiting for data from WROOM board...");
}

void loop() {
  pollUartLink();
}
