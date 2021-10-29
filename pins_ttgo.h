// TTGO boards; requires in User_Setup_Select.c the line
// #include <User_Setups/Setup25_TTGO_T_Display.h>    // Setup file for ESP32 and TTGO T-Display ST7789V SPI bus TFT
// to be uncommented.

#define BUTTON_1            35
#define BUTTON_2            0
 
#define RFID_CS       12 // SDA on board, SS in library
#define RFID_SCLK     13
#define RFID_MOSI     15
#define RFID_MISO     02
#define RFID_RESET    21
 
// SPIClass spi = SDSPI(HSPI);
