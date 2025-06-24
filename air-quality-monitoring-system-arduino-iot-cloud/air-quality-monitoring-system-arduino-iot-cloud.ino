#include "secrets.h"
#include "thingProperties.h"
#include <DHT.h>
#include <LiquidCrystal_I2C.h>

// -------------------- Constants --------------------
#define DHT_PIN         32              // GPIO32
#define DHT_TYPE        DHT11
#define MQ135_PIN       36              // GPIO36 (ADC1_CH0)
#define LED_PIN         25              // GPIO25
#define BUZZER_PIN      27              // GPIO27
#define AIR_THRESHOLD   1000             // Air quality threshold (PPM)

const int LEDC_CHANNEL   = 0;           // PWM channel
const int PWM_RESOLUTION = 8;           // 8-bit resolution
const int PWM_FREQUENCY  = 1000;        // 1kHz tone for buzzer

const uint8_t LCD_ADDR = 0x27;
const uint8_t LCD_COLS = 16;
const uint8_t LCD_ROWS = 2;

// -------------------- Objects --------------------
DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// -------------------- Variables --------------------
const unsigned long UPDATE_INTERVAL_MS = 5000;
const unsigned long BLINK_INTERVAL_MS  = 500;

unsigned long lastUpdateTime = 0;
unsigned long lastBlinkTime = 0;

bool isWarning = false;
bool buzzerState = false;

// -------------------- Setup --------------------
void setup() {
  Serial.begin(9600);
  delay(1500);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Buzzer PWM Setup for ESP32
  ledcSetup(LEDC_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcAttachPin(BUZZER_PIN, LEDC_CHANNEL);
  ledcWriteTone(LEDC_CHANNEL, 0); // Ensure buzzer is off

  // Initialize sensors and display
  dht.begin();
  lcd.begin(LCD_COLS, LCD_ROWS);
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Air Monitoring");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");

  // Arduino IoT Cloud
  initProperties();
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  setDebugMessageLevel(2);
  ArduinoCloud.printDebugInfo();
}

// -------------------- Main Loop --------------------
void loop() {
  ArduinoCloud.update();

  if (millis() - lastUpdateTime >= UPDATE_INTERVAL_MS) {
    lastUpdateTime = millis();
    readSensors();
  }

  updateWarningSignal();
}

// -------------------- Read Sensors --------------------
void readSensors() {
  float hum = dht.readHumidity();
  float temp = dht.readTemperature();
  int airPPM = analogRead(MQ135_PIN);

  if (isnan(temp) || isnan(hum)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  // Update cloud variables
  temperature = temp;
  humidity = hum;
  air_quality = airPPM;

  printToSerial(temp, hum, airPPM);

  isWarning = (airPPM > AIR_THRESHOLD);
  updateDisplay(temp, hum, airPPM, isWarning);
}

// -------------------- Update LCD Display --------------------
void updateDisplay(float temp, float hum, int airPPM, bool warn) {
  lcd.clear();
  lcd.setCursor(0, 0);
  if (warn) {
    lcd.print("!!! WARNING !!!");
  } else {
    lcd.print("T:");
    lcd.print(temp, 1);
    lcd.print("C H:");
    lcd.print(hum, 0);
    lcd.print("%");
  }

  lcd.setCursor(0, 1);
  lcd.print("Air PPM: ");
  lcd.print(airPPM);
  lcd.print("   "); // clear trailing chars
}

// -------------------- Warning Signal (LED + Buzzer) --------------------
void updateWarningSignal() {
  if (isWarning) {
    if (millis() - lastBlinkTime >= BLINK_INTERVAL_MS) {
      lastBlinkTime = millis();
      buzzerState = !buzzerState;

      digitalWrite(LED_PIN, buzzerState);
      if (buzzerState) {
        ledcWriteTone(LEDC_CHANNEL, PWM_FREQUENCY);
      } else {
        ledcWriteTone(LEDC_CHANNEL, 0);
      }
    }
  } else {
    digitalWrite(LED_PIN, LOW);
    ledcWriteTone(LEDC_CHANNEL, 0);
    buzzerState = false;
  }
}

// -------------------- Serial Output --------------------
void printToSerial(float temp, float hum, int airPPM) {
  Serial.print("Temperature: ");
  Serial.print(temp);
  Serial.println(" *C");

  Serial.print("Humidity: ");
  Serial.print(hum);
  Serial.println(" %");

  Serial.print("Air Quality (PPM): ");
  Serial.println(airPPM);
}

// -------------------- Cloud Variable Callbacks --------------------
void onTemperatureChange() {}
void onHumidityChange() {}
void onAirQualityChange() {}