#define BLYNK_TEMPLATE_ID "TMPL695GmNUCG"
#define BLYNK_TEMPLATE_NAME "Smart Door System"
char BLYNK_AUTH_TOKEN[33] = "";  // Default Auth Token

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <WebServer.h>

char ssid[32] = "";
char pass[64] = "";
char correctPassword[5] = "1234";
unsigned long UNLOCK_DURATION = 5000;
int MAX_WRONG_ATTEMPTS = 5;
unsigned long LOCKOUT_DURATION = 30000;

#define POWER_LED_PIN 32
#define RELAY_PIN 33
#define BUTTON_PIN 25
#define BUZZER_PIN 26
#define ROW_NUM 4
#define COLUMN_NUM 4
#define EEPROM_SIZE 145  // 5 password + 1 notifications + 1 blynk change + 32 SSID + 64 WiFi pass + 4 unlock + 1 attempts + 4 lockout + 33 auth token
#define EEPROM_ADDR 0    // Starting address for password
const char keys[ROW_NUM][COLUMN_NUM] = {
  { 'A', '3', '2', '1' },
  { 'B', '6', '5', '4' },
  { 'C', '9', '8', '7' },
  { 'D', '#', '0', '*' }
};
byte rowPins[ROW_NUM] = { 19, 18, 5, 17 };
byte colPins[COLUMN_NUM] = { 2, 0, 4, 16 };
const uint8_t LCD_ADDR = 0x27;
const uint8_t LCD_COLS = 16;
const uint8_t LCD_ROWS = 2;

const int LEDC_CHANNEL = 0;      // PWM channel
const int PWM_RESOLUTION = 8;    // 8-bit resolution
const int PWM_FREQUENCY = 2000;  // 1kHz tone for buzzer

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROW_NUM, COLUMN_NUM);
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);
/* Put IP Address details */
IPAddress local_ip(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);
WebServer server(80);

char enteredPassword[5] = "";
int passwordIndex = 0;
unsigned long unlockTime = 0;
unsigned long lockoutStartTime = 0;
bool isUnlocked = false;
bool buzzerState = false;
bool changePasswordMode = false;
bool verifyOldPassword = false;
int wrongPasswordCount = 0;
bool isLockedOut = false;
bool notificationsEnabled = true;   // Default: notifications enabled
bool passwordChangeEnabled = true;  // Default: password change enabled

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><title>Smart Door Config</title>";
  html += "<style>body{font-family:Arial;margin:20px}.form-group{margin:10px 0}input,button{padding:8px;margin:5px}</style></head>";
  html += "<body><h1>Smart Door Configuration</h1><form action='/config' method='POST'>";
  html += "<div class='form-group'><label>Blynk Auth Token:</label><input type='text' name='auth_token' value='" + String(BLYNK_AUTH_TOKEN) + "' maxlength='32'></div>";
  html += "<div class='form-group'><label>WiFi SSID:</label><input type='text' name='ssid' value='" + String(ssid) + "' maxlength='31'></div>";
  html += "<div class='form-group'><label>WiFi Password:</label><input type='text' name='wifi_pass' value='" + String(pass) + "' maxlength='63'></div>";
  html += "<div class='form-group'><label>Door Password (4 digits):</label><input type='text' name='door_pass' value='" + String(correctPassword) + "' maxlength='4'></div>";
  html += "<div class='form-group'><label>Unlock Duration (seconds):</label><input type='number' name='unlock_duration' value='" + String(UNLOCK_DURATION / 1000) + "' min='1' max='60'></div>";
  html += "<div class='form-group'><label>Max Wrong Attempts:</label><input type='number' name='max_attempts' value='" + String(MAX_WRONG_ATTEMPTS) + "' min='1' max='10'></div>";
  html += "<div class='form-group'><label>Lockout Duration (seconds):</label><input type='number' name='lockout_duration' value='" + String(LOCKOUT_DURATION / 1000) + "' min='10' max='300'></div>";
  html += "<button type='submit'>Save & Restart</button></form></body></html>";
  server.send(200, "text/html", html);
}

void handleConfig() {
  if (server.hasArg("auth_token") && server.hasArg("ssid") && server.hasArg("wifi_pass") && server.hasArg("door_pass") && server.hasArg("unlock_duration") && server.hasArg("max_attempts") && server.hasArg("lockout_duration")) {
    String newAuthToken = server.arg("auth_token");
    String newSSID = server.arg("ssid");
    String newWiFiPass = server.arg("wifi_pass");
    String newDoorPass = server.arg("door_pass");
    unsigned long newUnlockDuration = server.arg("unlock_duration").toInt() * 1000;
    int newMaxAttempts = server.arg("max_attempts").toInt();
    unsigned long newLockoutDuration = server.arg("lockout_duration").toInt() * 1000;

    if (newAuthToken.length() == 32 && newSSID.length() > 0 && newSSID.length() <= 31 && newWiFiPass.length() <= 63 && newDoorPass.length() == 4 && newDoorPass.toInt() >= 0 && newUnlockDuration >= 1000 && newUnlockDuration <= 60000 && newMaxAttempts >= 1 && newMaxAttempts <= 10 && newLockoutDuration >= 10000 && newLockoutDuration <= 300000) {
      newAuthToken.toCharArray(BLYNK_AUTH_TOKEN, 33);
      newSSID.toCharArray(ssid, 32);
      newWiFiPass.toCharArray(pass, 64);
      newDoorPass.toCharArray(correctPassword, 5);
      UNLOCK_DURATION = newUnlockDuration;
      MAX_WRONG_ATTEMPTS = newMaxAttempts;
      LOCKOUT_DURATION = newLockoutDuration;

      writeConfigToEEPROM();
      server.send(200, "text/html", "<html><body><h1>Configuration Saved</h1><p>ESP32 will restart in 3 seconds...</p></body></html>");
      delay(3000);
      ESP.restart();
    } else {
      server.send(400, "text/html", "<html><body><h1>Invalid Input</h1><p>Please check your inputs and try again. Auth Token must be 32 characters, Door Password must be 4 digits.</p><a href='/'>Back</a></body></html>");
    }
  } else {
    server.send(400, "text/html", "<html><body><h1>Missing Parameters</h1><p>All fields are required.</p><a href='/'>Back</a></body></html>");
  }
}

BLYNK_WRITE(V0) {
  int value = param.asInt();
  if (!isUnlocked && value == 1) {
    unlockDoor();
    wrongPasswordCount = 0;  // Reset wrong attempts on successful unlock
    if (notificationsEnabled) {
      // Blynk.logEvent("door_event", "Door unlocked via Blynk");
    }
  }
}

BLYNK_WRITE(V2) {
  notificationsEnabled = param.asInt();
  EEPROM.write(EEPROM_ADDR + 5, notificationsEnabled);
  EEPROM.commit();
  Serial.println("Notifications " + String(notificationsEnabled ? "enabled" : "disabled"));
}

BLYNK_WRITE(V3) {
  passwordChangeEnabled = param.asInt();
  EEPROM.write(EEPROM_ADDR + 6, passwordChangeEnabled);
  EEPROM.commit();
  Serial.println("Blynk password change " + String(passwordChangeEnabled ? "enabled" : "disabled"));
}

void setup() {
  Serial.begin(115200);


  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);

// For testing
  // clearEEPROM();

  readConfigFromEEPROM();

  pinMode(POWER_LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(POWER_LED_PIN, HIGH);
  digitalWrite(RELAY_PIN, HIGH);  // LOCKED

  // Buzzer PWM Setup for ESP32
  ledcAttach(BUZZER_PIN, PWM_FREQUENCY, PWM_RESOLUTION);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Smart Door Lock");
  lcd.setCursor(0, 1);
  lcd.print("Connecting...");

  // Try to connect to WiFi
  WiFi.begin(ssid, pass);
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed, starting AP mode");
    WiFi.mode(WIFI_AP);
    if (WiFi.softAP("SmartDoorConfig", "admin1234")) {
      Serial.println("AP started: SSID=SmartDoorConfig, Password=admin1234, IP=" + WiFi.softAPIP().toString());
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("AP Mode");
      lcd.setCursor(0, 1);
      lcd.print("192.168.4.1");
    } else {
      Serial.println("Failed to start AP mode");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("AP Mode Failed");
      lcd.setCursor(0, 1);
      lcd.print("Check Hardware");
    }
  } else {
    Serial.println("\nWiFi connected, IP: " + WiFi.localIP().toString());
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());
    delay(2000);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Door LOCKED");
    lcd.setCursor(0, 1);
    lcd.print("Enter Password");
  }

  server.on("/", handleRoot);
  server.on("/config", HTTP_POST, handleConfig);
  server.begin();
  Serial.println("Web server started");

  if (WiFi.status() == WL_CONNECTED) {
    Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  } else {
    Serial.println("Blynk not started due to WiFi failure");
  }
}

void loop() {
  server.handleClient();
 if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
  }
  checkButton();
  checkKeypad();
  checkAutoLock();
  checkLockout();
}

void unlockDoor() {
  digitalWrite(RELAY_PIN, LOW);
  unlockTime = millis();
  isUnlocked = true;
  passwordIndex = 0;
  memset(enteredPassword, 0, sizeof(enteredPassword));
  Blynk.virtualWrite(V0, 1);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Door UNLOCKED");
  buzzerTone(0.5, 100);
  Serial.println("Door UNLOCKED");
}

void lockDoor() {
  digitalWrite(RELAY_PIN, HIGH);
  isUnlocked = false;
  changePasswordMode = false;
  verifyOldPassword = false;
  Blynk.virtualWrite(V0, 0);
  passwordIndex = 0;                                    // Reset password input
  memset(enteredPassword, 0, sizeof(enteredPassword));  // Clear password buffer
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Door LOCKED");
  lcd.setCursor(0, 1);
  lcd.print("Enter Password");
  Serial.println("Door LOCKED");
}

void checkButton() {
  if (isLockedOut)
    return;  // Disable button during lockout
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (!isUnlocked) {  // Only allow button press if door is locked
      unlockDoor();
      wrongPasswordCount = 0;  // Reset wrong attempts on successful unlock
      if (notificationsEnabled) {
        Blynk.logEvent("door_event", "Door unlocked via button");
      }
    }
    while (digitalRead(BUTTON_PIN) == LOW)
      ;
  }
}

void checkKeypad() {
  if (isUnlocked || isLockedOut)
    return;

  char key = keypad.getKey();
  if (key) {
    Serial.print("Key pressed: ");
    Serial.println(key);  // Debug key press

    if (changePasswordMode) {
      handleChangePassword(key);
    } else if (key == 'D' && passwordChangeEnabled) {
      changePasswordMode = true;
      verifyOldPassword = true;
      passwordIndex = 0;
      memset(enteredPassword, 0, sizeof(enteredPassword));
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Enter Old Pass");
      lcd.setCursor(0, 1);
      lcd.print("                ");
      Serial.println("Entered change password mode: verify old password");
      buzzerTone(0.7, 100);
    } else if (key == '#') {
      enteredPassword[passwordIndex] = '\0';
      if (strcmp(enteredPassword, correctPassword) == 0) {
        unlockDoor();
        wrongPasswordCount = 0;  // Reset wrong attempts
        if (notificationsEnabled) {
          // Blynk.logEvent("door_event", "Door unlocked via correct password");
        }
      } else {
        wrongPasswordCount++;
        buzzerTone(0.8, 100);
        delay(100);
        buzzerTone(0.8, 100);
        delay(100);
        buzzerTone(0.8, 100);
        if (notificationsEnabled) {
          // Blynk.logEvent("door_warning", String("Wrong password entered (") + wrongPasswordCount + "/5)");
        }
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Wrong Password");
        lcd.setCursor(0, 1);
        lcd.print("Attempts: ");
        lcd.print(wrongPasswordCount);
        lcd.print("/5");
        delay(2000);
        if (wrongPasswordCount >= MAX_WRONG_ATTEMPTS) {
          isLockedOut = true;
          lockoutStartTime = millis();
          if (notificationsEnabled) {
            // Blynk.logEvent("door_warning", "System locked due to too many wrong attempts");
          }
          Serial.println("System locked due to too many wrong attempts");
        } else {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Door LOCKED");
          lcd.setCursor(0, 1);
          lcd.print("Enter Password");
        }
        Serial.println("Wrong password");
      }
      passwordIndex = 0;
      memset(enteredPassword, 0, sizeof(enteredPassword));
    } else if (key == '*') {
      passwordIndex = 0;
      memset(enteredPassword, 0, sizeof(enteredPassword));
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print("Enter Password");
      Serial.println("Password cleared");
      buzzerTone(0.7, 50);
    } else if (passwordIndex < 4) {
      if (passwordIndex == 0) {
        lcd.setCursor(0, 1);
        lcd.print("                ");
      }
      enteredPassword[passwordIndex] = key;
      lcd.setCursor(passwordIndex, 1);
      lcd.print('*');
      passwordIndex++;
      buzzerTone(0.9, 50);
      Serial.print("Key entered: ");
      Serial.println(key);
    }
  }
}

void checkAutoLock() {
  if (isUnlocked && millis() - unlockTime >= UNLOCK_DURATION) {
    lockDoor();
  }
}

void handleChangePassword(char key) {
  if (verifyOldPassword) {
    if (key == '#') {
      enteredPassword[passwordIndex] = '\0';
      if (strcmp(enteredPassword, correctPassword) == 0) {
        verifyOldPassword = false;
        passwordIndex = 0;
        memset(enteredPassword, 0, sizeof(enteredPassword));
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Enter New Pass");
        lcd.setCursor(0, 1);
        lcd.print("                ");
        Serial.println("Old password verified, enter new password");
        buzzerTone(0.5, 100);
      } else {
        wrongPasswordCount++;
        buzzerTone(0.8, 100);
        delay(100);
        buzzerTone(0.8, 100);
        delay(100);
        buzzerTone(0.8, 100);
        if (notificationsEnabled) {
          // Blynk.logEvent("door_warning", String("Wrong old password (") + wrongPasswordCount + "/5)");
        }
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Wrong Old Pass");
        lcd.setCursor(0, 1);
        lcd.print("Attempts: ");
        lcd.print(wrongPasswordCount);
        lcd.print("/5");
        delay(2000);
        if (wrongPasswordCount >= MAX_WRONG_ATTEMPTS) {
          isLockedOut = true;
          lockoutStartTime = millis();
          if (notificationsEnabled) {
            // Blynk.logEvent("door_warning", "System locked due to too many wrong attempts");
          }
          Serial.println("System locked due to too many wrong attempts");
        } else {
          changePasswordMode = false;
          verifyOldPassword = false;
          passwordIndex = 0;
          memset(enteredPassword, 0, sizeof(enteredPassword));
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Door LOCKED");
          lcd.setCursor(0, 1);
          lcd.print("Enter Password");
          Serial.println("Wrong old password");
        }
      }
    } else if (key == '*') {
      changePasswordMode = false;
      verifyOldPassword = false;
      passwordIndex = 0;
      memset(enteredPassword, 0, sizeof(enteredPassword));
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Door LOCKED");
      lcd.setCursor(0, 1);
      lcd.print("Enter Password");
      Serial.println("Change password cancelled");
      buzzerTone(0.7, 50);
    } else if (passwordIndex < 4) {
      if (passwordIndex == 0) {
        lcd.setCursor(0, 1);
        lcd.print("                ");
      }
      enteredPassword[passwordIndex] = key;
      lcd.setCursor(passwordIndex, 1);
      lcd.print('*');
      passwordIndex++;
      buzzerTone(0.9, 50);
      Serial.print("Old key entered: ");
      Serial.println(key);
    }
  } else {
    if (key == '#') {
      enteredPassword[passwordIndex] = '\0';
      if (passwordIndex == 4) {
        strcpy(correctPassword, enteredPassword);
        writeConfigToEEPROM();
        wrongPasswordCount = 0;  // Reset wrong attempts on successful password change
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Password Changed");
        lcd.setCursor(0, 1);
        lcd.print("Success!");
        if (notificationsEnabled) {
          // Blynk.logEvent("door_event", "Password changed successfully");
        }
        Serial.print("Password changed to: ");
        Serial.println(correctPassword);
        buzzerTone(0.5, 100);
        delay(2000);
      } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Invalid Password");
        lcd.setCursor(0, 1);
        lcd.print("4 digits needed");
        if (notificationsEnabled) {
          // Blynk.logEvent("door_warning", "Invalid new password");
        }
        Serial.println("Invalid new password");
        buzzerTone(0.8, 100);
        delay(100);
        buzzerTone(0.8, 100);
      }
      changePasswordMode = false;
      verifyOldPassword = false;
      passwordIndex = 0;
      memset(enteredPassword, 0, sizeof(enteredPassword));
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Door LOCKED");
      lcd.setCursor(0, 1);
      lcd.print("Enter Password");
    } else if (key == '*') {
      changePasswordMode = false;
      verifyOldPassword = false;
      passwordIndex = 0;
      memset(enteredPassword, 0, sizeof(enteredPassword));
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Door LOCKED");
      lcd.setCursor(0, 1);
      lcd.print("Enter Password");
      Serial.println("Change password cancelled");
      buzzerTone(0.7, 50);
    } else if (passwordIndex < 4) {
      if (passwordIndex == 0) {
        lcd.setCursor(0, 1);
        lcd.print("                ");
      }
      enteredPassword[passwordIndex] = key;
      lcd.setCursor(passwordIndex, 1);
      lcd.print('*');
      passwordIndex++;
      buzzerTone(0.9, 50);
      Serial.print("New key entered: ");
      Serial.println(key);
    }
  }
}

void readConfigFromEEPROM() {
  // Read password
  for (int i = 0; i < 4; i++) {
    correctPassword[i] = EEPROM.read(EEPROM_ADDR + i);
  }
  correctPassword[4] = '\0';
  if (correctPassword[0] < '0' || correctPassword[0] > '9') {
    strcpy(correctPassword, "1234");
  }

  // Read notifications and password change settings
  notificationsEnabled = EEPROM.read(EEPROM_ADDR + 5);
  passwordChangeEnabled = EEPROM.read(EEPROM_ADDR + 6);

  // Read WiFi SSID
  for (int i = 0; i < 32; i++) {
    ssid[i] = EEPROM.read(EEPROM_ADDR + 7 + i);
    if (ssid[i] == '\0') break;
  }

  // Read WiFi password
  for (int i = 0; i < 64; i++) {
    pass[i] = EEPROM.read(EEPROM_ADDR + 39 + i);
    if (pass[i] == '\0') break;
  }

  // Read Blynk Auth Token
  for (int i = 0; i < 32; i++) {
    BLYNK_AUTH_TOKEN[i] = EEPROM.read(EEPROM_ADDR + 103 + i);
    if (BLYNK_AUTH_TOKEN[i] == '\0') break;
  }

  // Read UNLOCK_DURATION
  UNLOCK_DURATION = 0;
  for (int i = 0; i < 4; i++) {
    UNLOCK_DURATION |= (unsigned long)EEPROM.read(EEPROM_ADDR + 135 + i) << (i * 8);
  }
  if (UNLOCK_DURATION < 1000 || UNLOCK_DURATION > 60000) {
    UNLOCK_DURATION = 5000;
  }

  // Read MAX_WRONG_ATTEMPTS
  MAX_WRONG_ATTEMPTS = EEPROM.read(EEPROM_ADDR + 139);
  if (MAX_WRONG_ATTEMPTS < 1 || MAX_WRONG_ATTEMPTS > 10) {
    MAX_WRONG_ATTEMPTS = 5;
  }

  // Read LOCKOUT_DURATION
  LOCKOUT_DURATION = 0;
  for (int i = 0; i < 4; i++) {
    LOCKOUT_DURATION |= (unsigned long)EEPROM.read(EEPROM_ADDR + 140 + i) << (i * 8);
  }
  if (LOCKOUT_DURATION < 10000 || LOCKOUT_DURATION > 300000) {
    LOCKOUT_DURATION = 30000;
  }

  Serial.print("Password read from EEPROM: ");
  Serial.println(correctPassword);
  Serial.println("Notifications from EEPROM: " + String(notificationsEnabled ? "enabled" : "disabled"));
  Serial.println("Blynk password change from EEPROM: " + String(passwordChangeEnabled ? "enabled" : "disabled"));
  Serial.print("WiFi SSID from EEPROM: ");
  Serial.println(ssid);
  Serial.print("WiFi Password from EEPROM: ");
  Serial.println(pass);
  Serial.print("Blynk Auth Token from EEPROM: ");
  Serial.println(BLYNK_AUTH_TOKEN);
  Serial.print("UNLOCK_DURATION from EEPROM: ");
  Serial.println(UNLOCK_DURATION);
  Serial.print("MAX_WRONG_ATTEMPTS from EEPROM: ");
  Serial.println(MAX_WRONG_ATTEMPTS);
  Serial.print("LOCKOUT_DURATION from EEPROM: ");
  Serial.println(LOCKOUT_DURATION);
}

void writeConfigToEEPROM() {
  // Write password
  for (int i = 0; i < 4; i++) {
    EEPROM.write(EEPROM_ADDR + i, correctPassword[i]);
  }
  EEPROM.write(EEPROM_ADDR + 4, '\0');

  // Write notifications and password change settings
  EEPROM.write(EEPROM_ADDR + 5, notificationsEnabled);
  EEPROM.write(EEPROM_ADDR + 6, passwordChangeEnabled);

  // Write WiFi SSID
  for (int i = 0; i < 32; i++) {
    EEPROM.write(EEPROM_ADDR + 7 + i, i < strlen(ssid) ? ssid[i] : '\0');
  }

  // Write WiFi password
  for (int i = 0; i < 64; i++) {
    EEPROM.write(EEPROM_ADDR + 39 + i, i < strlen(pass) ? pass[i] : '\0');
  }

  // Write Blynk Auth Token
  for (int i = 0; i < 32; i++) {
    EEPROM.write(EEPROM_ADDR + 103 + i, i < strlen(BLYNK_AUTH_TOKEN) ? BLYNK_AUTH_TOKEN[i] : '\0');
  }

  // Write UNLOCK_DURATION
  for (int i = 0; i < 4; i++) {
    EEPROM.write(EEPROM_ADDR + 135 + i, (UNLOCK_DURATION >> (i * 8)) & 0xFF);
  }

  // Write MAX_WRONG_ATTEMPTS
  EEPROM.write(EEPROM_ADDR + 139, MAX_WRONG_ATTEMPTS);

  // Write LOCKOUT_DURATION
  for (int i = 0; i < 4; i++) {
    EEPROM.write(EEPROM_ADDR + 140 + i, (LOCKOUT_DURATION >> (i * 8)) & 0xFF);
  }

  EEPROM.commit();
  Serial.println("Configuration written to EEPROM");
}

void buzzerTone(float dutyCycle, int duration) {
  ledcWrite(BUZZER_PIN, (pow(2, PWM_RESOLUTION) - 1) * dutyCycle);
  delay(duration);
  ledcWrite(BUZZER_PIN, 0);
}

void checkLockout() {
  if (isLockedOut) {
    unsigned long timeElapsed = millis() - lockoutStartTime;
    if (timeElapsed < LOCKOUT_DURATION) {
      int secondsLeft = (LOCKOUT_DURATION - timeElapsed) / 1000 + 1;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("System Locked");
      lcd.setCursor(0, 1);
      lcd.print("Wait ");
      lcd.print(secondsLeft);
      lcd.print(" seconds");
      delay(100);  // Update display frequently
    } else {
      isLockedOut = false;
      wrongPasswordCount = 0;
      if (notificationsEnabled) {
        // Blynk.logEvent("door_event", "System unlocked after lockout");
      }
      Serial.println("System unlocked after lockout");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Door LOCKED");
      lcd.setCursor(0, 1);
      lcd.print("Enter Password");
    }
  }
}

void clearEEPROM() {
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  Serial.println("EEPROM cleared");
}

