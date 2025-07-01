#define BLYNK_TEMPLATE_ID ""
#define BLYNK_TEMPLATE_NAME "Smart Door System"
#define BLYNK_AUTH_TOKEN ""

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

char ssid[] = "";
char pass[] = "";

#define POWER_LED_PIN 32
#define RELAY_PIN 33
#define BUTTON_PIN 25
#define BUZZER_PIN 26
#define ROW_NUM 4
#define COLUMN_NUM 4
#define EEPROM_SIZE 5  // 4 digits + null terminator
#define EEPROM_ADDR 0  // Starting address for password
#define MAX_WRONG_ATTEMPTS 5
#define LOCKOUT_DURATION 30000  // 30 seconds

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

char correctPassword[5] = "1234";  // Initial password, will be overwritten by EEPROM
char enteredPassword[5] = "";
int passwordIndex = 0;
unsigned long unlockTime = 0;
unsigned long lockoutStartTime = 0;
const unsigned long UNLOCK_DURATION = 5000;
bool isUnlocked = false;
bool buzzerState = false;
bool changePasswordMode = false;
bool verifyOldPassword = false;
int wrongPasswordCount = 0;
bool isLockedOut = false;

BLYNK_WRITE(V0) {
  int value = param.asInt();
  if (!isUnlocked && value == 1) {
    unlockDoor();
    wrongPasswordCount = 0;  // Reset wrong attempts on successful unlock
    // Blynk.logEvent("door_event", "Door unlocked via Blynk");
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  readPasswordFromEEPROM();

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
  lcd.print("Enter Password");

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
}

void loop() {
  Blynk.run();
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
  if (isLockedOut) return;  // Disable button during lockout
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (!isUnlocked) {  // Only allow button press if door is locked
      unlockDoor();
      wrongPasswordCount = 0;  // Reset wrong attempts on successful unlock
      // Blynk.logEvent("door_event", "Door unlocked via button");
    }
    while (digitalRead(BUTTON_PIN) == LOW)
      ;
  }
}

void checkKeypad() {
  if (isUnlocked || isLockedOut) return;

  char key = keypad.getKey();
  if (key) {
    Serial.print("Key pressed: ");
    Serial.println(key);  // Debug key press

    if (changePasswordMode) {
      handleChangePassword(key);
    } else if (key == 'D') {
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
        // Blynk.logEvent("door_event", "Door unlocked via correct password");
      } else {
        wrongPasswordCount++;
        buzzerTone(0.8, 100);
        delay(100);
        buzzerTone(0.8, 100);
        delay(100);
        buzzerTone(0.8, 100);
        // Blynk.logEvent("door_warning", String("Wrong password entered (") + wrongPasswordCount + "/5)");
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
          // Blynk.logEvent("door_warning", "System locked due to too many wrong attempts");
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
        // Blynk.logEvent("door_warning", String("Wrong old password (") + wrongPasswordCount + "/5)");
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
          // Blynk.logEvent("door_warning", "System locked due to too many wrong attempts");
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
        writePasswordToEEPROM();
        wrongPasswordCount = 0;  // Reset wrong attempts on successful password change
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Password Changed");
        lcd.setCursor(0, 1);
        lcd.print("Success!");
        // Blynk.logEvent("door_event", "Password changed successfully");
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
        // Blynk.logEvent("door_warning", "Invalid new password");
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

void readPasswordFromEEPROM() {
  for (int i = 0; i < 4; i++) {
    correctPassword[i] = EEPROM.read(EEPROM_ADDR + i);
  }
  correctPassword[4] = '\0';
  if (correctPassword[0] < '0' || correctPassword[0] > '9') {
    strcpy(correctPassword, "1234");
  }
  Blynk.virtualWrite(V1, correctPassword);
  Serial.print("Password read from EEPROM: ");
  Serial.println(correctPassword);
}

void writePasswordToEEPROM() {
  for (int i = 0; i < 4; i++) {
    EEPROM.write(EEPROM_ADDR + i, correctPassword[i]);
  }
  EEPROM.write(EEPROM_ADDR + 4, '\0');
  EEPROM.commit();
  Blynk.virtualWrite(V1, correctPassword);
  Serial.println("Password written to EEPROM");
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
      // Blynk.logEvent("door_event", "System unlocked after lockout");
      Serial.println("System unlocked after lockout");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Door LOCKED");
      lcd.setCursor(0, 1);
      lcd.print("Enter Password");
    }
  }
}
