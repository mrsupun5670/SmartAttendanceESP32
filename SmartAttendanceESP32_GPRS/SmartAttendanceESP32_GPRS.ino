/*
 * SmartAttendanceESP32_GPRS.ino
 * Purpose: Bus Attendance System using SIM800L GPRS for Internet Connectivity.
 * Network: Dialog (APN: dialogbb)
 * 
 * LIBRARIES REQUIRED (Install via Library Manager):
 * 1. TinyGSM (by Volodymyr Shymanskyy)
 * 2. ArduinoHttpClient (by Arduino)
 * 
 * HARDWARE WIRING:
 * ESP32 | SIM800L
 * 3.3V  | (NOT CONNECTED - Use separate 4V 2A power!)
 * GND   | GND
 * 16    | TX
 * 17    | RX
 * 
 * POWER WARNING:
 * SIM800L requires 2A peaks! Do not power from ESP32 3.3V or USB.
 * Use a Li-Ion battery or extensive decoupling capacitors.
 */
#define TINY_GSM_MODEM_SIM800
#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>
#include <MFRC522.h>
#include <Preferences.h>
#include <SPI.h>

// ----------------------------------------------------------------------
// CONFIGURATION
// ----------------------------------------------------------------------
// APN Settings (Dialog)
const char apn[] = "dialogbb";
const char user[] = ""; // Leave empty for Dialog
const char pass[] = ""; // Leave empty for Dialog

// GOOGLE SCRIPT CONFIG
const char server[] = "script.google.com";
const int port = 443;
// NOTE: Google Script Base Path (/macros/s/...)
const String SCRIPT_PATH = "/macros/s/AKfycby8QjOohMtw2NpOMpLytM2_LI-lwb39g3Cc2Kaj7FTaRMUb6O744m2mm1uv3724IqY/exec";

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
TinyGsmClientSecure client(modem); // Secure Client (SSL)
HttpClient http(client, server, port);

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

  // 3. Init GSM
  Serial.println("Initializing GSM Module...");
  scanSerial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(6000); 

  Serial.println("Restarting Modem...");
  modem.restart();
  
  // CRITICAL: Set client timeout!
  client.setTimeout(5000); // 5 seconds
  http.setHttpResponseTimeout(20000); // 20 seconds for header/body
  
  String modemInfo = modem.getModemInfo();
  Serial.print("Modem Info: ");
  Serial.println(modemInfo);

  // 4. Connect to GPRS
  Serial.print("Connecting to APN: ");
  Serial.print(apn);
  if (!modem.gprsConnect(apn, user, pass)) {
    Serial.println(" -> fail");
    triggerFail();
  } else {
    Serial.println(" -> success");
    triggerSuccess();
  }

  if (modem.isGprsConnected()) {
    Serial.println("GPRS Connected!");
  }

  Serial.println("--- SYSTEM READY ---");
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
  
  // FORCE DELAY to prevent "scans twice" rapid fire if card stays
  delay(3000); 
}

void handleStudentScan(String uid) {
  // Check last status (0=Out, 1=In)
  int currentStatus = preferences.getInt(uid.c_str(), 0);

  String action = (currentStatus == 0) ? "BUS_ENTRY" : "BUS_EXIT";
  
  // Toggle Status locally first (optimistic UI)
  preferences.putInt(uid.c_str(), (currentStatus == 0) ? 1 : 0);
  Serial.println("Action: " + action);

  // 3. Connect GPRS Explicitly
  Serial.print("Connecting GPRS...");
  if (!modem.isGprsConnected()) {
    if (modem.gprsConnect(apn, user, pass)) {
      Serial.println("OK");
    } else {
      Serial.println("FAIL");
      triggerFail();
      return; // Stop if no internet
    }
  } else {
    Serial.println("Already Connected");
  }

  // 4. Send Data (will handle disconnect internally)
  sendDataToScript(uid, action);
}

void sendDataToScript(String uid, String type) {
  // Restore device parameter
  String path = SCRIPT_PATH + "?action=handleScan" + "&uid=" + uid + "&device=BUS-UNIT-01" + "&type=" + type;

  // Set Timeouts for slow GPRS
  http.setHttpResponseTimeout(20000); 
  
  Serial.println("GET Request: " + path);
  
  // RETRY LOOP (Simple)
  int attempts = 0;
  bool success = false;
  
  while (attempts < 2 && !success) {
    if (attempts > 0) {
      Serial.println("Retrying...");
      delay(1000);
    }
    
    http.beginRequest();
    http.get(path);
    http.endRequest();
  
    int statusCode = http.responseStatusCode();
    Serial.print("Status Code: ");
    Serial.println(statusCode);
  
    String responseBody = "";
    
    if (statusCode > 0) {
      // Handle Redirect (302)
      // Handle Redirect (302)
      if (statusCode == 302) {
        Serial.println("Entered 302 - Redirect");
        String newUrl = "";
        
        // STANDARD SAFE METHOD: Use loop with hard limits
        // 'responseHeader' DOES NOT EXIST in ArduinoHttpClient, so we loop manually.
        unsigned long headerStart = millis();
        int headerCount = 0;
        
        while (http.headerAvailable()) {
          // SAFETY LIMITS:
          // 1. Time > 5s
          if (millis() - headerStart > 5000) {
            Serial.println("Error: Header Read Timeout!");
            break; 
          }
          // 2. Count > 30 (Runaway headers)
          if (headerCount++ > 30) {
            Serial.println("Error: Too many headers!");
            break;
          }
          
          String headerName = http.readHeaderName();
          String headerValue = http.readHeaderValue();
          // Serial.print("."); // Debug progress
          
          if (headerName.equalsIgnoreCase("Location")) {
            newUrl = headerValue;
            // Quick exit if found
            break;
          }
        }
        
        // Stop old request immediately
        http.stop(); 
        
        Serial.println("Exited Header Read"); 
        
        if (newUrl.length() > 0) {
          Serial.println("Redirecting to: " + newUrl);
          
          // Extract Host and Path
          int doubleSlash = newUrl.indexOf("//");
          int firstSlash = newUrl.indexOf("/", doubleSlash + 2);
          
          if (firstSlash > 0) {
            String newHost = newUrl.substring(doubleSlash + 2, firstSlash);
            String newPath = newUrl.substring(firstSlash);
            
            // New Request
            // CRITICAL: Close everything first
            client.stop(); 
            
            // Create NEW client/connection
            HttpClient httpRedirect(client, newHost, 443);
            httpRedirect.beginRequest();
            httpRedirect.get(newPath);
            httpRedirect.endRequest();
            
            statusCode = httpRedirect.responseStatusCode();
            responseBody = httpRedirect.responseBody();
            Serial.print("Redirected Status: ");
            Serial.println(statusCode);
          } else {
             Serial.println("Error: Could not parse Redirect URL Host/Path");
          }
        } else {
          Serial.println("Error: 302 received but 'Location' header missing or timed out.");
        }
      } else {
        responseBody = http.responseBody();
      }
      
      // Check Success
      if (statusCode == 200) {
        success = true; // Exit retry loop
        Serial.print("Response: "); Serial.println(responseBody);
        
        if (responseBody.indexOf("\"allowed\"") > 0) {
          Serial.println(">>> ACCESS GRANTED <<<");
          triggerSuccess();
          
          String phone = extractJsonValue(responseBody, "phone");
          String name = extractJsonValue(responseBody, "name");
          
          if (phone.length() > 0) {
            phone.replace(" ", ""); phone.replace("-", "");
            String msg = (type == "BUS_ENTRY") ? "Alert: " + name + " BOARDED the Bus." : "Alert: " + name + " LEFT the Bus.";
            sendSMS(phone, msg);
          } else {
             Serial.println("Phone number missing in JSON.");
          }
        } else if (responseBody.indexOf("\"denied\"") > 0) {
          Serial.println(">>> ACCESS DENIED <<<");
           triggerFail();
        } else {
           Serial.println("Unknown response.");
        }
      } else {
        Serial.println("Request Failed (Non-200).");
      }
    } else {
      Serial.println("Connection Error (-" + String(statusCode) + ")");
    }
    
    http.stop();
    attempts++;
  }
  
  if (!success) {
    Serial.println("Failed after retries.");
    triggerFail();
  }
  
  client.stop(); // Full Close
}

void sendSMS(String number, String message) {
  // CRITICAL FIX: Disconnect GPRS before sending SMS
  // SIM800L cannot handle both active data and SMS well simultaneously
  if (modem.isGprsConnected()) {
    Serial.println("Disconnecting GPRS for SMS...");
    modem.gprsDisconnect();
    delay(500);
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
  
  // Reconnect GPRS for next scan? 
  // Ideally, we start fresh next loop anyway, but let's leave it disconnected
  // The loop() or handleStudentScan() checks connection anyway.
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
