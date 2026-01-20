/*
 * Test_RFID.ino
 * Purpose: specific sketch to test the connection between ESP32 and MFRC522 RFID Reader.
 * 
 * Hardware Connections (ESP32 -> MFRC522):
 * 3.3V -> 3.3V
 * GND  -> GND
 * Rst  -> GPIO 22
 * SDA  -> GPIO 5  (SS)
 * MOSI -> GPIO 23
 * MISO -> GPIO 19
 * SCK  -> GPIO 18
 * 
 * Dependency: Install "MFRC522" library by Github Community in Arduino IDE Library Manager.
 */

#include <SPI.h>
#include <MFRC522.h>
#include "soc/soc.h" 
#include "soc/rtc_cntl_reg.h"

#define SS_PIN  5
#define RST_PIN 22

MFRC522 rfid(SS_PIN, RST_PIN);

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
  Serial.begin(115200);
  while (!Serial); // Wait for serial to settle

  SPI.begin(); // Init SPI bus
  rfid.PCD_Init(); // Init MFRC522

  Serial.println("RFID Reader Test");
  Serial.print("Firmware Version: 0x");
  byte readReg = rfid.PCD_ReadRegister(rfid.VersionReg);
  Serial.println(readReg, HEX);
  
  if (readReg == 0x00 || readReg == 0xFF) {
    Serial.println("WARNING: Communication failure, check wiring!");
  } else {
    Serial.println("RFID Reader found!");
  }
  Serial.println("Place a card on the reader...");
}

void loop() {
  // Look for new cards
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  // Select one of the cards
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Show UID on serial monitor
  Serial.print("UID tag :");
  String content= "";
  byte letter;
  for (byte i = 0; i < rfid.uid.size; i++) {
     Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
     Serial.print(rfid.uid.uidByte[i], HEX);
     content.concat(String(rfid.uid.uidByte[i] < 0x10 ? " 0" : " "));
     content.concat(String(rfid.uid.uidByte[i], HEX));
  }
  Serial.println();
  Serial.print("Message : ");
  content.toUpperCase();
  Serial.println(content.substring(1));
  
  // Halt PICC
  rfid.PICC_HaltA();
  // Stop encryption on PCD
  rfid.PCD_StopCrypto1();
  
  delay(1000); // Simple debounce
}
