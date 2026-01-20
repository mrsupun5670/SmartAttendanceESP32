/*
 * Test_GSM.ino
 * Purpose: Test AT commands and SMS sending with GSM Module (SIM800L/900A)
 * 
 * Hardware Connections:
 * ESP32 GPIO 16 (RX2) -> GSM TX
 * ESP32 GPIO 17 (TX2) -> GSM RX
 * GSM GND -> ESP32 GND
 * GSM VCC -> External 5V/2A Source (Grounds must be shared with ESP32)
 */

#define RXD2 16
#define TXD2 17

void updateSerial() {
  delay(500);
  while (Serial.available()) {
    Serial2.write(Serial.read()); // Forward from Serial Monitor to GSM
  }
  while (Serial2.available()) {
    Serial.write(Serial2.read()); // Forward from GSM to Serial Monitor
  }
}

void setup() {
  Serial.begin(115200);
  // Initialize Serial2 for GSM
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  Serial.println("Initializing GSM Module Test...");
  delay(1000);

  Serial.println("Testing AT Command...");
  Serial2.println("AT"); 
  updateSerial();
  delay(1000);
  
  Serial.println("Signal Quality:");
  Serial2.println("AT+CSQ"); 
  updateSerial();
  delay(1000);

  Serial.println("SIM Card Status:");
  Serial2.println("AT+CPIN?");
  updateSerial();

  Serial.println("\n--- Type AT commands in Serial Monitor to interact manually ---");
  Serial.println("Example: ATD+123456789; to call, or send SMS commands.");
}

void loop() {
  updateSerial();
}
