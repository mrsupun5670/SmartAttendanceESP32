/*
 * SmartAttendanceESP32_WiFi_GSM.ino
 * Purpose: Bus Attendance System using Wi-Fi for Internet & SIM800L for SMS.
 * 
 * LIBRARIES REQUIRED:
 * 1. WiFi (Built-in)
 * 2. HTTPClient (Built-in)
 * 3. TinyGSM (by Volodymyr Shymanskyy) - For SMS ONLY
 * 4. MFRC522 (by Miguel Balboa)
 */

#define TINY_GSM_MODEM_SIM800
#include <WiFi.h>
#include <HTTPClient.h>
#include <TinyGsmClient.h>
#include <MFRC522.h>
#include <Preferences.h>
#include <SPI.h>

// ----------------------------------------------------------------------
// CONFIGURATION
// ----------------------------------------------------------------------
// WI-FI CREDENTIALS
const char* wifi_ssid = "Fiber";
const char* wifi_pass = "Suneth@512";

// GOOGLE SCRIPT CONFIG
// NOTE: Google Script Base Path (/macros/s/...)
const String SCRIPT_PATH = "https://script.google.com/macros/s/AKfycbwGcyLA8nDbzgHJUIPGrlFM3zZlqZLOubMkKuVLaDrFjYGHgkW1Y3UubRqJOxrEqQF2ng/exec";

// PINS - GSM (Serial2)
#define RX_PIN 16
#define TX_PIN 17

// PINS - RFID (Standard VSPI)
#define SS_PIN 5
#define RST_PIN 22

// PINS - FEEDBACK
#define PIN_LED_GREEN 25
#define PIN_LED_RED 26
#define PIN_BUZZER 13

// ----------------------------------------------------------------------
// GLOBAL OBJECTS
// ----------------------------------------------------------------------
HardwareSerial scanSerial(2); // UART2 for GSM
TinyGsm modem(scanSerial);

MFRC522 rfid(SS_PIN, RST_PIN);
Preferences preferences;

// Forward Declarations
void handleStudentScan(String uid);
void sendDataToScript(String uid, String type);
void sendSMS(String number, String message);
String extractJsonValue(String json, String key);
void triggerSuccess();
void triggerFail();

void setup() {
  Serial.begin(115200);
  delay(10);
  
  // 0. Init Feedback Pins
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  // 1. Init status storage
  preferences.begin("attendance", false);

  // 2. Init RFID
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("RFID Ready");

  // 3. Init GSM (SMS ONLY)
  Serial.println("Initializing GSM Module for SMS...");
  scanSerial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(3000); 
  
  // Create a clean slate for the modem
  Serial.println("Restarting Modem...");
  modem.restart();
  
  String modemInfo = modem.getModemInfo();
  Serial.print("Modem Info: ");
  Serial.println(modemInfo);
  
  // Ensure GPRS is OFF to avoid conflicts
  if (modem.isGprsConnected()) {
    modem.gprsDisconnect();
  }

  // 4. Connect to Wi-Fi
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(wifi_ssid, wifi_pass);
  
  int wifi_attempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_attempts < 20) {
    delay(500);
    Serial.print(".");
    wifi_attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    triggerSuccess();
  } else {
    Serial.println("\nWi-Fi Failed.");
    triggerFail();
  }

  Serial.println("--- SYSTEM READY (WiFi + GSM) ---");
}

void loop() {
  // Check for RFID
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  // 1. Get UID
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (i > 0) uid += ",";
    uid += String(rfid.uid.uidByte[i]);
  }
  Serial.println("\nCard Detected: " + uid);

  // Auditory feedback
  digitalWrite(PIN_BUZZER, HIGH);
  delay(50);
  digitalWrite(PIN_BUZZER, LOW);

  // 2. Handle Logic
  handleStudentScan(uid);

  // Halt PICC
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  
  delay(3000); 
}

void handleStudentScan(String uid) {
  // Check last status (0=Out, 1=In)
  int currentStatus = preferences.getInt(uid.c_str(), 0);

  String action = (currentStatus == 0) ? "BUS_ENTRY" : "BUS_EXIT";
  
  // DO NOT Toggle Status locally yet (wait for server response)
  Serial.println("Identifying Card: " + uid);
  Serial.println("Proposed Action: " + action);

  // 3. Send Data via Wi-Fi
  if (WiFi.status() == WL_CONNECTED) {
    sendDataToScript(uid, action);
  } else {
    Serial.println("Wi-Fi Disconnected! Attempting Reconnect...");
    WiFi.disconnect();
    WiFi.reconnect();
    delay(2000); // Wait for reconnection
    
    if (WiFi.status() == WL_CONNECTED) {
      sendDataToScript(uid, action);
    } else {
      Serial.println("Reconnect Failed. Offline.");
      triggerFail();
    }
  }
}

void sendDataToScript(String uid, String type) {
  // Construct URL
  String url = SCRIPT_PATH + "?action=handleScan" + "&uid=" + uid + "&device=BUS-UNIT-01" + "&type=" + type;

  // Set Timeouts
  HTTPClient http;
  
  Serial.println("GET Request: " + url);
  
  // Begin Request (Secure)
  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // Wi-Fi lib handles redirects automatically!
  
  int httpCode = http.GET();
  Serial.print("Status Code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String responseBody = http.getString();
      Serial.print("Response: "); Serial.println(responseBody);
      
      if (responseBody.indexOf("\"allowed\"") > 0) {
        Serial.println(">>> ACCESS GRANTED <<<");
        
        // UPDATE LOCAL STATUS NOW (Success)
        int newStatus = (type == "BUS_ENTRY") ? 1 : 0;
        preferences.putInt(uid.c_str(), newStatus);
        
        triggerSuccess();
        
        String phone = extractJsonValue(responseBody, "phone");
        String name = extractJsonValue(responseBody, "name");
        
        if (phone.length() > 0) {
          // CLEAN PHONE NUMBER
          phone.replace(" ", ""); 
          phone.replace("-", "");
          if (!phone.startsWith("+")) {
            phone = "+" + phone;
          }
          
          String msg = (type == "BUS_ENTRY") ? "Alert: " + name + " BOARDED the Bus." : "Alert: " + name + " LEFT the Bus.";
          sendSMS(phone, msg);
        } else {
           Serial.println("Phone number missing in JSON.");
        }
      } else if (responseBody.indexOf("\"denied\"") > 0) {
        Serial.println(">>> ACCESS DENIED <<<");
         triggerFail();
      } else {
         Serial.println("Unknown API Response.");
      }
    } else {
      Serial.println("Request Failed (Non-200).");
    }
  } else {
    Serial.println("HTTP Connection Error: " + http.errorToString(httpCode));
  }
  
  http.end(); // Close connection
  Serial.println("--- Request Complete ---");
}

void sendSMS(String number, String message) {
  // Ensure GPRS is OFF just in case
  if (modem.isGprsConnected()) {
    modem.gprsDisconnect();
  }

  Serial.print("Sending SMS to ");
  Serial.print(number);
  Serial.print(": ");
  Serial.println(message);

  if (modem.sendSMS(number, message)) {
    Serial.println("SMS Sent Successfully.");
  } else {
    Serial.println("SMS Failed.");
  }
}

// --- HELPERS ---

String extractJsonValue(String json, String key) {
  String keyStr = "\"" + key + "\":\"";
  int start = json.indexOf(keyStr);
  if (start == -1) return "";
  start += keyStr.length();
  int end = json.indexOf("\"", start);
  if (end == -1) return "";
  return json.substring(start, end);
}

void triggerSuccess() {
  digitalWrite(PIN_LED_GREEN, HIGH);
  digitalWrite(PIN_BUZZER, HIGH);
  delay(100);
  digitalWrite(PIN_BUZZER, LOW);
  delay(100);
  digitalWrite(PIN_BUZZER, HIGH);
  delay(100);
  digitalWrite(PIN_BUZZER, LOW);
  delay(1000);
  digitalWrite(PIN_LED_GREEN, LOW);
}

void triggerFail() {
  digitalWrite(PIN_LED_RED, HIGH);
  digitalWrite(PIN_BUZZER, HIGH);
  delay(1000);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED_RED, LOW);
}
