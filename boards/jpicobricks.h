// PicoBricks board definition for Smallest-Doom
// OLED: SSD1306 128x64 via I2C0 (SDA=GP4, SCL=GP5, addr=0x3C)
// Button: GP10 (active low, pull-up)
// Potentiometer: GP26 (ADC0) - used for turning
// Buzzer: GP20

#ifndef _BOARDS_JPICOBRICKS_H
#define _BOARDS_JPICOBRICKS_H

#define JPICOBRICKS 1

// OLED I2C config
#define J_OLED_I2C       i2c0
#define J_OLED_SDA_PIN   4
#define J_OLED_SCL_PIN   5
#define J_OLED_ADDR      0x3C
#define J_OLED_WIDTH     128
#define J_OLED_HEIGHT    64
#define J_OLED_FRAME_PERIOD 33333   // ~30fps max over I2C

// Controls
#define J_BUTTON_PIN     10   // button
#define J_POT_PIN        26   // potentiometer ADC0 -> turn left/right
#define J_LDR_PIN        27   // light sensor ADC1 -> touch to move forward
#define J_BUZZER_PIN     20   // buzzer

// Turn thresholds (0-4095 ADC range)
#define J_POT_LEFT_THRESH   1000
#define J_POT_RIGHT_THRESH  3000

// UART
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// LED
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif

// Flash
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

#endif // _BOARDS_JPICOBRICKS_H
