// hub_pins.h - EVERY GPIO the hub uses, in one place. docs/HUB_PINS.md lists the same pins with their terminals, and
// tests/host/test_hub_pins.js fails if the two disagree, if a pin is used twice, or if a hub file uses a pin number that
// is not defined here. Adding or moving a pin: change this file AND docs/HUB_PINS.md.
//   (Terminals: the Freenove ESP32 terminal board, L = left row, R = right row.)
#pragma once

// ---- buttons ----
#define BTN_NEXT 25        // L14  right (next page) button, to ground, internal pull-up
#define BTN_PREV 26        // L15  left (previous page) button, to ground, internal pull-up

// ---- analog inputs (ADC1) ----
#define FUEL_PIN 36        // L8   fuel level sender
#define POT_PIN 34         // L10  brightness knob
#define LIGHTS_PIN 35      // L11  car's lights-on wire, 47k / 10k divider
#define DIMMER_PIN 32      // L12  dash dimmer, 47k / 10k divider (+ 10 uF if PWM)

// ---- CAN (SN65HVD230) ----
#define CAN_TX_PIN GPIO_NUM_21   // R11  transceiver CTX / D
#define CAN_RX_PIN GPIO_NUM_22   // R8   transceiver CRX / R

// ---- EGT (MAX6675, bit-banged SPI) ----
#define EGT_SCK 18         // R14
#define EGT_SO 19          // R13
#define EGT_CS 23          // R7

// ---- GPS (GT-U7, Serial2 at 9600) ----
#define GPS_RX_PIN 16      // R17  hub RX <- GPS TX
#define GPS_TX_PIN 17      // R16  hub TX -> GPS RX

// ---- DHT22 (cabin temperature / humidity) ----
#define DHT_PIN 4          // R18

// ---- CC1101 433 MHz receiver (SPI) ----
#define RF_GDO0 33         // L13  demodulated data
#define RF_CS 27           // L16
#define RF_SCK 14          // L17
#define RF_MOSI 13         // L20
#define RF_MISO 39         // L9   input-only pin

// ---- I2C bus: OLED (0x3C), MPU6050 (0x68), PCF8574 expanders (0x20, 0x21) ----
#define I2C_SDA 5          // R15
#define I2C_SCL 15         // R21

// ---- lamp inputs (OLED panel), switch to ground, internal pull-up ----
#define LAMP_IN_A 2        // R20  default: seatbelt
#define LAMP_IN_B 12       // L18  must not be held HIGH at power-up (flash voltage strap)
#define LAMP_IN_C 0        // R19  the BOOT button; must not be held LOW at power-up
