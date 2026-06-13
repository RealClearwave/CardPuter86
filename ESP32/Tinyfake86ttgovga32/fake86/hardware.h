#ifndef _HARDWARE_H
 #define _HARDWARE_H

 #include "gbConfig.h"

 // ============================================
 // Cardputer Peripheral Pin Definitions
 // ============================================

 // ST7789 TFT Display (SPI)
 #define TFT_BL   38
 #define TFT_RST  33
 #define TFT_DC   34   // RS/DC
 #define TFT_MOSI 35   // DATA
 #define TFT_SCK  36
 #define TFT_CS   37

 // microSD Card (SPI)
 #define SD_CS    12
 #define SD_MOSI  14
 #define SD_CLK   40
 #define SD_MISO  39

 // I2S Speaker (NS4168 amplifier)
 #define I2S_BCLK   41
 #define I2S_SDATA  42
 #define I2S_LRCLK  43

 // Keyboard Matrix (74HC138 decoder)
 // Row input - analog read via resistor ladder
 #define KBD_ROW_PIN      10
 // Column address select (A, B, C)
 #define KBD_COL_A        11
 #define KBD_COL_B        9
 #define KBD_COL_C        8
 // Column enable outputs from 74HC138 (Y0-Y6)
 #define KBD_COL_EN_0     7
 #define KBD_COL_EN_1     6
 #define KBD_COL_EN_2     5
 #define KBD_COL_EN_3     4
 #define KBD_COL_EN_4     3
 #define KBD_COL_EN_5     15
 #define KBD_COL_EN_6     13
 #define KBD_NUM_COLS     7

 // IR Transmitter
 #define IR_LED_PIN  44

 // Microphone (SPM1423 MEMS)
 #define MIC_DATA_PIN  46
 #define MIC_CLK_PIN   43  // shared with I2S LRCLK

 // Grove port (HY2.0-4P)
 #define GROVE_YELLOW  2
 #define GROVE_WHITE   1

 // RGB LED (on G37, shared with TFT_CS — M5Cardputer lib handles)
 #define RGB_LED_PIN  37

 // User button (G0 = boot/download)
 #define USER_BTN_PIN 0

 // Color indices (for OSD and text rendering)
 #define BLACK       0
 #define BLUE        1
 #define GREEN       2
 #define CYAN        3
 #define RED         4
 #define MAGENTA     5
 #define YELLOW      6
 #define WHITE       7

 // Hardware color IDs for compatibility
 #define ID_COLOR_BLACK   0
 #define ID_COLOR_WHITE   1
 #define ID_COLOR_VIOLETA 2

#endif
