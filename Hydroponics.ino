#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <math.h>

// ============= PINS DEFINITION =============
#define DHT_PIN 5
#define TDS_PIN 32
#define PH_PIN 33
#define LIGHT_PIN 34
#define RELAY1_PIN 12
#define RELAY2_PIN 14
#define RELAY3_PIN 27
#define RELAY4_PIN 26
#define SWITCH1_PIN 16  // RX2
#define SWITCH2_PIN 17  // TX2

// ============= SENSOR SETUP =============
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 20, 4);  // Address 0x27, 20x4 LCD

// ============= VARIABLES =============
float temperature = 0, humidity = 0;
float tdsValue = 0, phValue = 0;
float lightValue = 0;
int tdsRaw = 0, phRaw = 0, lightRaw = 0;

int displayMode = 0;  // 0:Temp, 1:Humidity, 2:pH, 3:TDS, 4:Light

bool relay1_state = false;
bool relay2_state = false;
bool relay3_state = false;
bool relay4_state = false;

unsigned long lastReadTime = 0;
const unsigned long READ_INTERVAL = 2000;  // Read every 2 seconds

// ============= PH CALIBRATION =============
const float pH_Cal = 0.009;  // Calibration value for pH sensor

// ============= TDS CALIBRATION =============
const float VREF = 3.3;
const float ADC_RESOLUTION = 4095.0;

// ============= SETUP =============
void setup() {
  Serial.begin(115200);
  delay(100);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.print("Initializing...");

  // Initialize Pins
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(RELAY3_PIN, OUTPUT);
  pinMode(RELAY4_PIN, OUTPUT);
  pinMode(SWITCH1_PIN, INPUT_PULLUP);
  pinMode(SWITCH2_PIN, INPUT_PULLUP);

  // Turn off all relays initially (Active Low)
  digitalWrite(RELAY1_PIN, HIGH);
  digitalWrite(RELAY2_PIN, HIGH);
  digitalWrite(RELAY3_PIN, HIGH);
  digitalWrite(RELAY4_PIN, HIGH);

  // Configure ADC range for better voltage coverage on ESP32
  analogSetPinAttenuation(TDS_PIN, ADC_11db);
  analogSetPinAttenuation(PH_PIN, ADC_11db);
  analogSetPinAttenuation(LIGHT_PIN, ADC_11db);

  // Initialize DHT
  dht.begin();
  delay(100);

  delay(2000);
  lcd.clear();
}

// ============= LOOP =============
void loop() {
  // Handle switches
  handleSwitches();

  // Read sensors at intervals
  if (millis() - lastReadTime >= READ_INTERVAL) {
    readAllSensors();
    displayLCD();
    printSelected();
    lastReadTime = millis();
  }

  delay(100);
}

// ============= READ ALL SENSORS =============
void readAllSensors() {
  // Read DHT22
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();

  // Read pH Sensor
  phRaw = analogRead(PH_PIN);
  phValue = readPHSensor();

  // Read TDS Sensor
  tdsRaw = analogRead(TDS_PIN);
  tdsValue = readTDSSensor();

  // Read Light Sensor
  lightRaw = analogRead(LIGHT_PIN);
  lightValue = map(lightRaw, 0, 4095, 0, 100);  // Convert to 0-100%
}

// ============= READ pH SENSOR =============
float readPHSensor() {
  int raw = analogRead(PH_PIN);
  float voltage = raw * 3.3 / 4095.0;
  float ph = 7.0 + ((2.5 - voltage) / 0.18);  // Calibration formula
  return constrain(ph, 0, 14);
}

// ============= READ TDS SENSOR =============
float readTDSSensor() {
  int raw = analogRead(TDS_PIN);
  float voltage = raw * VREF / ADC_RESOLUTION;
  float tds = (133.42 * voltage * voltage * voltage - 255.86 * voltage * voltage + 857.39 * voltage) * 0.5;
  return constrain(tds, 0, 2000);
}

// ============= DISPLAY ON LCD =============
void displayLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mode: ");
  lcd.print(displayMode);

  lcd.setCursor(0, 1);
  if (displayMode == 0) {
    lcd.print("Temp: ");
    lcd.print(temperature, 1);
    lcd.print("C");
  } else if (displayMode == 1) {
    lcd.print("Humidity: ");
    lcd.print(humidity, 0);
    lcd.print("%");
  } else if (displayMode == 2) {
    lcd.print("pH: ");
    lcd.print(phValue, 2);
  } else if (displayMode == 3) {
    lcd.print("TDS: ");
    lcd.print(tdsValue, 0);
    lcd.print("ppm");
  } else if (displayMode == 4) {
    lcd.print("Light: ");
    lcd.print(lightValue, 0);
    lcd.print("%");
  }

  lcd.setCursor(0, 2);
  lcd.print("R1:");
  lcd.print(relay1_state ? "ON" : "OFF");
  lcd.print(" R2:");
  lcd.print(relay2_state ? "ON" : "OFF");

  lcd.setCursor(0, 3);
  lcd.print("R3:");
  lcd.print(relay3_state ? "ON" : "OFF");
  lcd.print(" R4:");
  lcd.print(relay4_state ? "ON" : "OFF");
}

// ============= HANDLE SWITCHES =============
void handleSwitches() {
  if (digitalRead(SWITCH1_PIN) == LOW) {
    delay(50);  // Debounce
    if (digitalRead(SWITCH1_PIN) == LOW) {
      toggleRelay1();
      while (digitalRead(SWITCH1_PIN) == LOW) {
        delay(20);
      }
      delay(100);  // Debounce release
    }
  }

  if (digitalRead(SWITCH2_PIN) == LOW) {
    delay(50);  // Debounce
    if (digitalRead(SWITCH2_PIN) == LOW) {
      displayMode = (displayMode + 1) % 5;
      displayLCD();
      printSelected();
      while (digitalRead(SWITCH2_PIN) == LOW) {
        delay(20);
      }
      delay(100);  // Debounce release
    }
  }
}

void printSelected() {
  if (displayMode == 0) {
    Serial.print("Temp: ");
    Serial.print(temperature, 1);
    Serial.println("C");
  } else if (displayMode == 1) {
    Serial.print("Humidity: ");
    Serial.print(humidity, 0);
    Serial.println("%");
  } else if (displayMode == 2) {
    float phVoltage = phRaw * VREF / ADC_RESOLUTION;
    Serial.print("pH: ");
    Serial.print(phValue, 2);
    Serial.print(" | RAW: ");
    Serial.print(phRaw);
    Serial.print(" (V:");
    Serial.print(phVoltage, 2);
    Serial.println(")");
  } else if (displayMode == 3) {
    float tdsVoltage = tdsRaw * VREF / ADC_RESOLUTION;
    Serial.print("TDS: ");
    Serial.print(tdsValue, 0);
    Serial.print("ppm | RAW: ");
    Serial.print(tdsRaw);
    Serial.print(" (V:");
    Serial.print(tdsVoltage, 2);
    Serial.println(")");
  } else if (displayMode == 4) {
    float lightVoltage = lightRaw * VREF / ADC_RESOLUTION;
    Serial.print("Light: ");
    Serial.print(lightValue, 0);
    Serial.print("% | RAW: ");
    Serial.print(lightRaw);
    Serial.print(" (V:");
    Serial.print(lightVoltage, 2);
    Serial.println(")");
  }
}

// ============= RELAY CONTROL =============
void toggleRelay1() {
  relay1_state = !relay1_state;
  digitalWrite(RELAY1_PIN, relay1_state ? LOW : HIGH);  // Active Low
  Serial.println(relay1_state ? "Relay 1 ON" : "Relay 1 OFF");
}

void toggleRelay2() {
  relay2_state = !relay2_state;
  digitalWrite(RELAY2_PIN, relay2_state ? LOW : HIGH);  // Active Low
  Serial.println(relay2_state ? "Relay 2 ON" : "Relay 2 OFF");
}

void toggleRelay3() {
  relay3_state = !relay3_state;
  digitalWrite(RELAY3_PIN, relay3_state ? LOW : HIGH);  // Active Low
  Serial.println(relay3_state ? "Relay 3 ON" : "Relay 3 OFF");
}

void toggleRelay4() {
  relay4_state = !relay4_state;
  digitalWrite(RELAY4_PIN, relay4_state ? LOW : HIGH);  // Active Low
  Serial.println(relay4_state ? "Relay 4 ON" : "Relay 4 OFF");
}
