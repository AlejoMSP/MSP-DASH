/*
  esp32s3-amoled-linkecu-numbers

  SUPERSEDED per project decision: the round display now receives
  pre-decoded ECU data over a UART link from the WROOM board instead of
  reading CAN independently - see esp32s3-amoled-uart-display/ for the
  current approach. This sketch is kept as a reference/alternative in
  case standalone CAN reading on this board is wanted again later (it
  was fully wired and confirmed working at the code level, just not the
  chosen path going forward).

  First real gauge display for the Waveshare ESP32-S3-Touch-AMOLED-1.43,
  reading LinkECU CAN data. Numbers-only for now (no needles/bars/lights
  yet) - big, clean digits on the round display.

  Display config (confirmed working via prior bring-up testing):
    - Driver: CO5300
    - QSPI clock: 10MHz
    - Visible area: 466x466, centered in the 480x480 native buffer via
      offset=7,7,7,7
    - Tearing fix: opaque-background text (setTextColor(fg,bg)) instead
      of separate clear+draw - single write per field, no visible tear

  CAN config: LinkECU, confirmed from PCLink CAN Setup at the very start
  of this project (base 1520 decimal / 0x5F0, big-endian, most channels
  scaled by /10). Same block layout previously used for MegaSquirt before
  that was removed from the ESP32-classic build - LinkECU and MegaSquirt
  appear to share this exact broadcast format (see docs/CAN-map.md
  history). NOT yet independently bench-verified on THIS board/wiring -
  confirm CAN frames are actually arriving before trusting displayed
  values.

  CAN pins: GPIO1/GPIO2, confirmed both free (per Waveshare's pinout
  table) AND physically broken out on the left-side pin header (per the
  actual board diagram - GPIO5/6 were an earlier wrong guess: they're
  "free" at the chip level but not wired to any accessible connector on
  this specific board).
    CAN TX -> GPIO1 -> SN65HVD230 CTX
    CAN RX -> GPIO2 -> SN65HVD230 CRX
  Bitrate: 500kbps (matches every other ECU in this project - confirm
  against your actual PCLink CAN Setup bitrate if different).

  Blocks parsed (all big-endian words, most channels x0.1):
    0x5F0 (1520+0): word3 = RPM (raw x1, no scaling)
    0x5F2 (1520+2): word0=Baro, word1=MAP, word2=MAT, word3=CLT (all /10)
    0x5F3 (1520+3): word0=TPS, word1=Batt, word2=EGO1, word3=EGO2 (all /10)
*/

#include <Arduino_GFX_Library.h>
#include "driver/twai.h"

// ---- Display pins (confirmed) ----
#define AMOLED_CS    9
#define AMOLED_SCK   10
#define AMOLED_D0    11
#define AMOLED_D1    12
#define AMOLED_D2    13
#define AMOLED_D3    14
#define AMOLED_RST   21
#define AMOLED_EN    42

// ---- CAN pins (revised: GPIO5/6 are NOT physically broken out on this
// board's headers, despite being listed "free" in the chip's official
// pinout table - that table shows silicon availability, not what's
// actually wired to an accessible connector. Confirmed from the real
// board diagram: IO1/IO2 ARE on the left-side header, confirmed free.) ----
#define CAN_TX_PIN GPIO_NUM_1
#define CAN_RX_PIN GPIO_NUM_2
#define CAN_BITRATE TWAI_TIMING_CONFIG_500KBITS()

// ---- LinkECU CAN blocks (confirmed base address, from PCLink) ----
#define LINK_BASE   1520 // 0x5F0
#define LINK_BLOCK0 (LINK_BASE + 0) // RPM
#define LINK_BLOCK2 (LINK_BASE + 2) // Baro, MAP, MAT, CLT
#define LINK_BLOCK3 (LINK_BASE + 3) // TPS, Batt, EGO1, EGO2

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  AMOLED_CS, AMOLED_SCK, AMOLED_D0, AMOLED_D1, AMOLED_D2, AMOLED_D3);

Arduino_GFX *gfx = new Arduino_CO5300(
  bus, AMOLED_RST, 0, 466, 466,
  7, 7, 7, 7);

// Theme colors (RGB565) - cyan label accent matching the web dashboard's look
#define COLOR_BG    0x0000
#define COLOR_LABEL 0x4E9C // approx web theme cyan #4dd0e1
#define COLOR_VALUE 0xFFFF

// ---- Live values, updated from CAN frames ----
volatile float v_rpm = 0, v_map = 0, v_clt = 0, v_tps = 0, v_ego = 0, v_batt = 0, v_mat = 0;

// ---- Big-endian word helper (LinkECU/MegaSquirt-style) ----
int16_t wordBE(uint8_t *d, int offset) {
  return (int16_t)((d[offset] << 8) | d[offset + 1]);
}

// ---- Field layout: label position, value position, decimals, last-drawn cache ----
struct Field {
  const char* label;
  int16_t labelX, labelY, valueX, valueY;
  uint8_t valueSize, fieldWidth; // fieldWidth = fixed char width for padding (tearing fix)
  volatile float* value;
  uint8_t decimals;
  char lastDrawn[16];
};

Field fields[] = {
  { "RPM",   170, 60,  120, 95,  7, 6, &v_rpm,  0, "" },
  { "MAP",   70,  240, 55,  270, 4, 6, &v_map,  1, "" },
  { "CLT",   270, 240, 255, 270, 4, 6, &v_clt,  1, "" },
  { "TPS",   70,  320, 55,  350, 4, 6, &v_tps,  1, "" },
  { "AFR",   270, 320, 255, 350, 4, 6, &v_ego,  1, "" },
  { "BATT",  70,  400, 55,  430, 4, 6, &v_batt, 2, "" },
  { "MAT",   270, 400, 255, 430, 4, 6, &v_mat,  1, "" },
};
const int NUM_FIELDS = sizeof(fields) / sizeof(fields[0]);

void drawLabels() {
  gfx->setTextColor(COLOR_LABEL, COLOR_BG);
  gfx->setTextSize(2);
  for (int i = 0; i < NUM_FIELDS; i++) {
    gfx->setCursor(fields[i].labelX, fields[i].labelY);
    gfx->print(fields[i].label);
  }
}

// Only redraws a field if its formatted value actually changed - avoids
// unnecessary QSPI writes (and any residual tearing risk) when a value
// is holding steady.
void updateField(Field &f) {
  char raw[16];
  dtostrf(*f.value, 0, f.decimals, raw);

  char buf[16];
  snprintf(buf, sizeof(buf), "%-*s", f.fieldWidth, raw);

  if (strcmp(buf, f.lastDrawn) == 0) return; // unchanged - skip the write
  strcpy(f.lastDrawn, buf);

  gfx->setTextColor(COLOR_VALUE, COLOR_BG);
  gfx->setTextSize(f.valueSize);
  gfx->setCursor(f.valueX, f.valueY);
  gfx->print(buf);
}

// ---- CAN frame handling ----
void handleLinkFrame(uint32_t id, uint8_t *d) {
  switch (id) {
    case LINK_BLOCK0:
      v_rpm = wordBE(d, 6); // word3, raw x1
      break;
    case LINK_BLOCK2:
      v_map = wordBE(d, 2) / 10.0f; // word1
      v_mat = wordBE(d, 4) / 10.0f; // word2
      v_clt = wordBE(d, 6) / 10.0f; // word3
      break;
    case LINK_BLOCK3:
      v_tps = wordBE(d, 0) / 10.0f; // word0
      v_batt = wordBE(d, 2) / 10.0f; // word1
      v_ego = wordBE(d, 4) / 10.0f; // word2
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("LinkECU numbers display starting...");

  // --- Display init ---
  pinMode(AMOLED_EN, OUTPUT);
  digitalWrite(AMOLED_EN, HIGH);

  if (!gfx->begin(10000000)) {
    Serial.println("gfx->begin() FAILED");
  } else {
    Serial.println("gfx->begin() succeeded");
  }
  gfx->fillScreen(COLOR_BG);
  drawLabels();

  // --- CAN init ---
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = CAN_BITRATE;
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
    Serial.println("TWAI driver install FAILED");
  }
  if (twai_start() != ESP_OK) {
    Serial.println("TWAI start FAILED");
  }
  Serial.println("CAN running - waiting for LinkECU frames...");
}

void loop() {
  twai_message_t message;
  if (twai_receive(&message, pdMS_TO_TICKS(10)) == ESP_OK) {
    handleLinkFrame(message.identifier, message.data);
  }

  for (int i = 0; i < NUM_FIELDS; i++) {
    updateField(fields[i]);
  }
}
