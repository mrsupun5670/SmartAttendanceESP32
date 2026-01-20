/*
 * Test_Board_Minimal.ino
 * Purpose: Absolute minimum code to test if the ESP32 board is alive.
 * If this crashes with "Brownout", the board is likely physically damaged
 * or the power source (USB port/Cable) is insufficient.
 */

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

void setup() {
  // 1. Disable brownout immediately
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  
  // 2. Start Serial
  Serial.begin(115200);
  delay(1000);
  
  // 3. Print status
  Serial.println("\n\n--- BOARD TEST START ---");
  Serial.println("If you see this, the code is running!");
  
  // 4. Blink user LED (usually GPIO 2)
  pinMode(2, OUTPUT);
}

void loop() {
  Serial.println("Board is alive...");
  digitalWrite(2, HIGH); // Turn LED ON
  delay(500);
  digitalWrite(2, LOW);  // Turn LED OFF
  delay(500);
}
