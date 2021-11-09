// 1.77 160(RGB)x128 Board  labeling v.s pining
// 1  GND
// 2  VCC   3V3
// 3  SCK   CLK
// 4  SDA   MOSI
// 5  RES   RESET
// 6  RS    A0 / bank select / DC
// 7  CS    SS
// 8 LEDA - wired to 3V3
// 9 LEDA - already wired to pin 8 on the board.
//

// #define TFT_MOSI            12 /
// #define TFT_MISO            13 // not used
// #define TFT_SCLK            14 /
// #define TFT_CS              26
// #define TFT_DC              27
// #define TFT_RST              2

#define TFT_BL              4
#define TFT_BACKLIGHT_ON    HIGH

#define LED_1               23 // CANCELand left red light
#define LED_2               22 // OK and right red light
#define BUTTON_1            32 // CANCEL and LEFT button
#define BUTTON_2            33 // OK and right button
#define BOARD_V3_SENSE      35 // hard wired to GND on board V3

#define RFID_SCLK           16 // shared with screen
#define RFID_MOSI            5 // shared with screen
#define RFID_MISO           13 // shared with screen
#define RFID_CS             25  // ok
#define RFID_RESET          17  // shared with screen
#define RFID_IRQ             3

#define TFT_ROTATION 3
