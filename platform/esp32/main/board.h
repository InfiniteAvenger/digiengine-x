// Pin map for the Waveshare ESP32-S3-LCD-1.69 (and the Touch variant).
// Display and I2C pins come from Waveshare's official pin_config.h; power
// pins come from the board wiki and differ between board revisions.
#pragma once
#include "sdkconfig.h"

#define PIN_LCD_DC 4
#define PIN_LCD_CS 5
#define PIN_LCD_SCK 6
#define PIN_LCD_MOSI 7
#define PIN_LCD_RST 8
#define PIN_LCD_BL 15
#define LCD_W 240
#define LCD_H 280
#define LCD_GAP_Y 20  // the 240x280 panel sits inside the ST7789's 240x320 RAM

#define PIN_I2C_SDA 11
#define PIN_I2C_SCL 10
#define RTC_I2C_ADDR 0x51  // PCF85063

#if CONFIG_VPET_BOARD_REV_V1
#define PIN_SYS_EN 35   // drive high to stay powered on battery
#define PIN_SYS_OUT 36  // low while the PWR button is held
#define PIN_BUZZER 33
#else
#define PIN_SYS_EN 41
#define PIN_SYS_OUT 40
#define PIN_BUZZER 42
#endif

#define PIN_BTN_A CONFIG_VPET_PIN_BTN_A
#define PIN_BTN_B (CONFIG_VPET_PIN_BTN_B < 0 ? PIN_SYS_OUT : CONFIG_VPET_PIN_BTN_B)
#define PIN_BTN_C CONFIG_VPET_PIN_BTN_C
