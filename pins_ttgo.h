// TTGO boards

// Manual config:
//    Once TFT_eSPI is installed - it needs to be configured to select the right board.
//    Go to .../Arduino/library/TFT_eSPI and open the file "User_Setup_Select.h".
//    Uncomment the line with that says:
//       // #include <User_Setups/Setup25_TTGO_T_Display.h>    // Setup file for ESP32 and TTGO T-Display ST7789V SPI bus TFT
//    and chagne it to:
//       #include <User_Setups/Setup25_TTGO_T_Display.h>    // Setup file for ESP32 and TTGO T-Display ST7789V SPI bus TFT
//    i.e. remove the first two // characters. Then save it again in the same place.
//

#define BUTTON_1            35
#define BUTTON_2            0

#define RFID_CS       12 // SDA on board, SS in library
#define RFID_SCLK     13
#define RFID_MOSI     15
#define RFID_MISO     02
#define RFID_RESET    21

#define TFT_ROTATION 1
