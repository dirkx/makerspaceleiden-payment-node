#include <SPI.h>
#include <MFRC522.h>

#include "global.h"
#include "log.h"
#include "rfid.h"
#include "display.h"

SPIClass RFID_SPI(HSPI);
MFRC522_SPI spiDevice = MFRC522_SPI(RFID_CS, RFID_RESET, &RFID_SPI);
MFRC522 mfrc522 = MFRC522(&spiDevice);

// Very ugly global vars - used to communicate between the REST call and the rest.
//
char tag[128] = { 0 };

unsigned int rfid_scans = 0, rfid_miss = 0;
void setupRFID()
{
  RFID_SPI.begin(RFID_SCLK, RFID_MISO, RFID_MOSI, RFID_CS);
  mfrc522.PCD_Init();
  Log.print("RFID Scanner: ");
  mfrc522.PCD_DumpVersionToSerial();  // Show details of PCD - MFRC522 Card Reader details
}

bool loopRFID() {
  if (md != WAIT_FOR_REGISTER_SWIPE && md != ENTER_AMOUNT)
    return false;

  if ( ! mfrc522.PICC_IsNewCardPresent()) {
    return false;
  }

  if ( ! mfrc522.PICC_ReadCardSerial()) {
    Log.println("Bad read (was card removed too quickly ? )");
    rfid_miss++;
    return false;
  }
  if (mfrc522.uid.size == 0) {
    Log.println("Bad card (size = 0)");
    rfid_miss++;
    return false;
  };

  // We're somewhat strict on the parsing/what we accept; as we use it unadultared in the URL.
  if (mfrc522.uid.size > sizeof(mfrc522.uid.uidByte)) {
    Log.println("Too large a card id size. Ignoring.)");
    rfid_miss++;
    return false;
  };

  memset(tag, 0, sizeof(tag));
  for (int i = 0; i < mfrc522.uid.size; i++) {
    char buff[5]; // 3 digits, dash and \0.
    snprintf(buff, sizeof(buff), "%s%d", i ? "-" : "", mfrc522.uid.uidByte[i]);
    strncat(tag, buff, sizeof(tag) - 1);
  };

  Log.println("Good scan");
  rfid_scans++;
  mfrc522.PICC_HaltA();
  return true;
}
