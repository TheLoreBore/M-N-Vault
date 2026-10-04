#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>
#include <WiFiS3.h>
#include "arduino_secrets.h"

// =====================================
// PIN DEFINITIONS
// =====================================

// RFID
#define RFID_SS   10
#define RFID_RST  9

// RGB LED
#define RED_PIN    7
#define GREEN_PIN  6
#define BLUE_PIN   5

// Other components
#define BUTTON_PIN 8
#define MOTION_PIN 4
#define SERVO_PIN  3
#define BUZZER_PIN 2

// =====================================
// COMPONENTS
// =====================================

MFRC522 rfid(RFID_SS, RFID_RST);
Servo vaultServo;
WiFiSSLClient telegram;

// =====================================
// SETTINGS
// =====================================

const unsigned long SELECTION_TIME = 2000;
const unsigned long STEP_TIMEOUT = 5000;
const unsigned long OPEN_TIME = 30000;   // how long the vault stays open

const int LOCKED_POSITION = 90;
const int OPEN_POSITION = 0;

const bool TEXT_ON_ALARM = true;         // also text when the alarm goes off

// =====================================
// VAULT STATES
// =====================================

enum VaultState {

  SELECTING,

  RED_WAVE,
  RED_RFID,

  GREEN_WAVE,

  BLUE_WAVE,
  BLUE_RFID
};

VaultState state = SELECTING;

// =====================================
// VARIABLES
// =====================================

int buttonPresses = 0;

unsigned long lastButtonPress = 0;
unsigned long stateStartTime = 0;

// =====================================
// SETUP
// =====================================

void setup() {

  Serial.begin(9600);

  delay(1000);

  // Button
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Motion sensor
  pinMode(MOTION_PIN, INPUT);

  // RGB LED
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

  // Buzzer
  pinMode(BUZZER_PIN, OUTPUT);

  // Servo
  vaultServo.attach(SERVO_PIN);
  vaultServo.write(LOCKED_POSITION);

  // RFID
  SPI.begin();
  rfid.PCD_Init();

  // Lights off
  allLightsOff();

  // WiFi (for text messages). The vault still works if this fails.
  Serial.print("Connecting to WiFi... ");
  Serial.println(connectWiFi() ? "OK" : "FAILED (will retry when sending)");

  Serial.println();
  Serial.println("================================");
  Serial.println("        HEIST VAULT READY");
  Serial.println("================================");
  Serial.println();

  Serial.println("BUTTON SELECTION:");
  Serial.println("1 press = RED");
  Serial.println("2 presses = GREEN (INVALID)");
  Serial.println("3 presses = BLUE");
  Serial.println();

  Serial.println("Valid combos:");
  Serial.println("RED -> WAVE -> RFID");
  Serial.println("BLUE -> WAVE -> RFID");
  Serial.println();
}

// =====================================
// MAIN LOOP
// =====================================

void loop() {

  // ===================================
  // SELECT COLOR
  // ===================================

  if (state == SELECTING) {

    handleButton();

    // Wait until the user has stopped pressing
    if (buttonPresses > 0 &&
        millis() - lastButtonPress > SELECTION_TIME) {

      // =================================
      // 1 PRESS = RED
      // =================================

      if (buttonPresses == 1) {

        Serial.println();
        Serial.println("==============================");
        Serial.println("       RED SELECTED");
        Serial.println("==============================");

        setRed();

        Serial.println("Wave at the motion sensor.");

        state = RED_WAVE;
        stateStartTime = millis();
      }

      // =================================
      // 2 PRESSES = GREEN
      // GREEN IS INVALID
      // =================================

      else if (buttonPresses == 2) {

        Serial.println();
        Serial.println("==============================");
        Serial.println("      GREEN SELECTED");
        Serial.println("==============================");
        Serial.println("GREEN IS AN INVALID COMBINATION!");

        setGreen();

        Serial.println("If you wave, the alarm will sound.");

        state = GREEN_WAVE;
        stateStartTime = millis();
      }

      // =================================
      // 3 PRESSES = BLUE
      // =================================

      else if (buttonPresses == 3) {

        Serial.println();
        Serial.println("==============================");
        Serial.println("       BLUE SELECTED");
        Serial.println("==============================");

        setBlue();

        Serial.println("Wave at the motion sensor.");

        state = BLUE_WAVE;
        stateStartTime = millis();
      }

      // =================================
      // MORE THAN 3 = ALARM
      // =================================

      else {

        Serial.println();
        Serial.println("TOO MANY BUTTON PRESSES!");

        alarm("too many button presses");
        resetVault();
      }

      buttonPresses = 0;
    }
  }

  // ===================================
  // RED: WAIT FOR WAVE
  // ===================================

  else if (state == RED_WAVE) {

    if (motionDetected()) {

      Serial.println("RED WAVE DETECTED!");

      shortBeep();

      Serial.println("Now scan RFID.");

      state = RED_RFID;
      stateStartTime = millis();
    }

    else if (timedOut()) {

      Serial.println("RED WAVE TIMEOUT!");

      alarm("red combo timed out");
      resetVault();
    }
  }

  // ===================================
  // RED: WAIT FOR RFID
  // ===================================

  else if (state == RED_RFID) {

    if (rfidDetected()) {

      Serial.println("RED RFID DETECTED!");

      printUID();

      Serial.println("RED COMBINATION COMPLETE!");

      unlockVault("RED");

      resetVault();
    }

    else if (timedOut()) {

      Serial.println("RED RFID TIMEOUT!");

      alarm("red combo timed out");
      resetVault();
    }
  }

  // ===================================
  // GREEN: INVALID COMBINATION
  // ===================================

  else if (state == GREEN_WAVE) {

    // Any wave after choosing GREEN
    // is immediately wrong.

    if (motionDetected()) {

      Serial.println();
      Serial.println("==============================");
      Serial.println("     WRONG COMBINATION!");
      Serial.println("==============================");

      alarm("wrong combination (green)");
      resetVault();
    }

    // If they do nothing for 5 seconds,
    // the invalid attempt also fails.

    else if (timedOut()) {

      Serial.println("GREEN COMBINATION TIMEOUT!");

      alarm("wrong combination (green)");
      resetVault();
    }
  }

  // ===================================
  // BLUE: WAIT FOR WAVE
  // ===================================

  else if (state == BLUE_WAVE) {

    if (motionDetected()) {

      Serial.println("BLUE WAVE DETECTED!");

      shortBeep();

      Serial.println("Now scan RFID.");

      state = BLUE_RFID;
      stateStartTime = millis();
    }

    else if (timedOut()) {

      Serial.println("BLUE WAVE TIMEOUT!");

      alarm("blue combo timed out");
      resetVault();
    }
  }

  // ===================================
  // BLUE: WAIT FOR RFID
  // ===================================

  else if (state == BLUE_RFID) {

    if (rfidDetected()) {

      Serial.println("BLUE RFID DETECTED!");

      printUID();

      Serial.println("BLUE COMBINATION COMPLETE!");

      unlockVault("BLUE");

      resetVault();
    }

    else if (timedOut()) {

      Serial.println("BLUE RFID TIMEOUT!");

      alarm("blue combo timed out");
      resetVault();
    }
  }
}

// =====================================
// BUTTON HANDLER
// =====================================

void handleButton() {

  static bool lastButtonState = HIGH;

  bool currentButtonState = digitalRead(BUTTON_PIN);

  // Detect new button press
  if (lastButtonState == HIGH &&
      currentButtonState == LOW) {

    buttonPresses++;

    lastButtonPress = millis();

    Serial.print("BUTTON PRESS #");
    Serial.println(buttonPresses);

    // Show current selection

    if (buttonPresses == 1) {

      setRed();

      Serial.println("SELECTION = RED");
    }

    else if (buttonPresses == 2) {

      setGreen();

      Serial.println("SELECTION = GREEN (INVALID)");
    }

    else if (buttonPresses == 3) {

      setBlue();

      Serial.println("SELECTION = BLUE");
    }

    else {

      allLightsOff();

      Serial.println("TOO MANY PRESSES!");
    }

    // Debounce
    delay(50);

    // Wait for button release
    while (digitalRead(BUTTON_PIN) == LOW) {

      delay(10);
    }

    delay(100);
  }

  lastButtonState = currentButtonState;
}

// =====================================
// MOTION SENSOR
// =====================================

bool motionDetected() {

  return digitalRead(MOTION_PIN) == HIGH;
}

// =====================================
// RFID
// =====================================

bool rfidDetected() {

  if (!rfid.PICC_IsNewCardPresent()) {

    return false;
  }

  if (!rfid.PICC_ReadCardSerial()) {

    return false;
  }

  return true;
}

// =====================================
// PRINT RFID UID
// =====================================

void printUID() {

  Serial.print("UID: ");

  for (byte i = 0; i < rfid.uid.size; i++) {

    if (rfid.uid.uidByte[i] < 0x10) {

      Serial.print("0");
    }

    Serial.print(rfid.uid.uidByte[i], HEX);

    if (i < rfid.uid.size - 1) {

      Serial.print(":");
    }
  }

  Serial.println();

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// =====================================
// RED LIGHT
// =====================================

void setRed() {

  allLightsOff();

  digitalWrite(RED_PIN, HIGH);
}

// =====================================
// GREEN LIGHT
// =====================================

void setGreen() {

  allLightsOff();

  digitalWrite(GREEN_PIN, HIGH);
}

// =====================================
// BLUE LIGHT
// =====================================

void setBlue() {

  allLightsOff();

  digitalWrite(BLUE_PIN, HIGH);
}

// =====================================
// TURN ALL LIGHTS OFF
// =====================================

void allLightsOff() {

  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(BLUE_PIN, LOW);
}

// =====================================
// TIMEOUT
// =====================================

bool timedOut() {

  return millis() - stateStartTime > STEP_TIMEOUT;
}

// =====================================
// SHORT BEEP
// =====================================

void shortBeep() {

  tone(BUZZER_PIN, 1800);

  delay(120);

  noTone(BUZZER_PIN);
}

// =====================================
// SECURITY ALARM
// =====================================

void alarm(const char* reason) {

  Serial.println();
  Serial.println("==============================");
  Serial.println("       SECURITY ALERT!");
  Serial.println("==============================");

  for (int i = 0; i < 6; i++) {

    tone(BUZZER_PIN, 2500);
    delay(150);

    tone(BUZZER_PIN, 1200);
    delay(150);
  }

  noTone(BUZZER_PIN);

  if (TEXT_ON_ALARM) {

    sendText(String("Vault ALARM: ") + reason);
  }
}

// =====================================
// UNLOCK VAULT
// =====================================

void unlockVault(const char* combo) {

  Serial.println();
  Serial.println("==============================");
  Serial.println("          VAULT OPEN!");
  Serial.println("==============================");

  // Victory sound
  tone(BUZZER_PIN, 1000);
  delay(150);

  tone(BUZZER_PIN, 1500);
  delay(150);

  tone(BUZZER_PIN, 2000);
  delay(300);

  noTone(BUZZER_PIN);

  // Open servo
  unsigned long openedAt = millis();

  vaultServo.write(OPEN_POSITION);

  Serial.println("SERVO = OPEN");

  // Text the owner (takes a few seconds; the vault is already open)
  sendText(String("Vault OPENED (") + combo + " combo)");

  // Keep open for the rest of the 30 seconds
  while (millis() - openedAt < OPEN_TIME) {

    delay(50);
  }

  // Lock again
  vaultServo.write(LOCKED_POSITION);

  Serial.println("SERVO = LOCKED");
}

// =====================================
// RESET VAULT
// =====================================

void resetVault() {

  allLightsOff();

  buttonPresses = 0;

  state = SELECTING;

  stateStartTime = millis();

  Serial.println();
  Serial.println("==============================");
  Serial.println("        VAULT RESET");
  Serial.println("==============================");

  Serial.println("1 press = RED");
  Serial.println("2 presses = GREEN (INVALID)");
  Serial.println("3 presses = BLUE");
  Serial.println();
}

// =====================================
// WIFI
// =====================================

bool connectWiFi() {

  if (WiFi.status() == WL_CONNECTED) return true;

  for (int i = 0; i < 3; i++) {

    WiFi.begin(WIFI_SSID, WIFI_PASS);

    unsigned long t0 = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000UL) {

      delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) return true;
  }

  return false;
}

// =====================================
// TELEGRAM MESSAGE
// =====================================

String urlEncode(const String &s) {

  String o;
  const char *hex = "0123456789ABCDEF";

  for (unsigned i = 0; i < s.length(); i++) {

    char c = s[i];

    if (isalnum(c) || c == '-' || c == '_' || c == '.') o += c;
    else {

      o += '%';
      o += hex[(c >> 4) & 0xF];
      o += hex[c & 0xF];
    }
  }

  return o;
}

// Sends a Telegram message to your chat. Returns true if Telegram answered 200 OK.
bool sendText(const String &msg) {

  if (!connectWiFi()) {

    Serial.println("Telegram: no WiFi");
    return false;
  }

  if (!telegram.connect("api.telegram.org", 443)) {

    Serial.println("Telegram: connect failed");
    return false;
  }

  telegram.print(String("GET /bot") + TELEGRAM_BOT_TOKEN +
                 "/sendMessage?chat_id=" + TELEGRAM_CHAT_ID +
                 "&text=" + urlEncode(msg) + " HTTP/1.1\r\n");
  telegram.print("Host: api.telegram.org\r\n");
  telegram.print("Connection: close\r\n\r\n");

  // First line looks like "HTTP/1.1 200 OK"
  unsigned long t0 = millis();

  while (!telegram.available() && millis() - t0 < 8000UL) delay(10);

  String status = telegram.readStringUntil('\n');

  telegram.stop();

  bool ok = status.indexOf(" 200") > 0;

  Serial.print("Telegram: ");
  Serial.println(ok ? "sent" : status);

  return ok;
}
