/*
 * SmartAttendanceESP32.ino
 * Purpose: Main firmware for Smart Attendance System using ESP32, RC522 RFID, and GSM Module.
 * Logic: Scans RFID card -> Reads UID -> Sends SMS Notification "Attendance Marked".
 * 
 * Hardware Connections:
 * [RFID MFRC522]
 * 3.3V -> 3.3V
 * GND  -> GND
 * Rst  -> GPIO 22
 * SDA  -> GPIO 5  (SS)
 * MOSI -> GPIO 23
 * MISO -> GPIO 19
 * SCK  -> GPIO 18
 * 
 * [GSM SIM800L/900A]
 * TX   -> GPIO 16 (RX2)
 * RX   -> GPIO 17 (TX2)
 * GND  -> GND
 * VCC  -> External 5V/2A PSU
 */

#include <SPI.h>
#include <MFRC522.h>

// --- Configuration ---
#define SS_PIN  5
#define RST_PIN 22
#define RXD2    16
#define TXD2    17

// Target Phone Number for Attendance Notification
// CHANGE THIS TO YOUR ACTUAL NUMBER
const String PHONE_NUMBER = "+1234567890"; 

MFRC522 rfid(SS_PIN, RST_PIN);

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2); // Init GSM Serial

  SPI.begin(); 
  rfid.PCD_Init(); 

  Serial.println("Smart Attendance System Initialized");
  Serial.println("1. Testing GSM Connection...");
  delay(1000);
  
  // Basic GSM Check
  Serial2.println("AT");
  delay(500);
  if(Serial2.available()){
     String response = Serial2.readString();
     Serial.println("GSM Response: " + response);
  } else {
     Serial.println("GSM Not Responding (Check Power/Wiring!)");
  }
  
  Serial.println("2. Waiting for RFID Card...");
}

void loop() {
  // 1. Look for new cards
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  // 2. Select one of the cards
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  // 3. Read UID
  Serial.print("Card Detected! UID: ");
  String uidString = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
     Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
     Serial.print(rfid.uid.uidByte[i], HEX);
     uidString += String(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
     uidString += String(rfid.uid.uidByte[i], HEX);
  }
  Serial.println();
  uidString.toUpperCase();
  uidString.trim();

  // 4. Send SMS
  sendAttendanceSMS(uidString);
  
  // 5. Cleanup
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  
  // Prevent duplicate reads
  delay(3000); 
}

void sendAttendanceSMS(String cardID) {
  Serial.println("Sending SMS...");
  
  Serial2.println("AT+CMGF=1"); // Set SMS to Text Mode
  delay(500);
  
  Serial2.print("AT+CMGS=\"");
  Serial2.print(PHONE_NUMBER); 
  Serial2.println("\"");
  delay(500);
  
  Serial2.print("Attendance Marked for ID: ");
  Serial2.println(cardID);
  
  Serial2.write(26); // ASCII code of Ctrl+Z to send
  delay(3000); // Wait for transmission
  
  Serial.println("SMS Sent Command Issued.");
}
