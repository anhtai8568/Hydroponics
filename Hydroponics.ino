#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <math.h>
#define BLYNK_TEMPLATE_ID "TMPL6GAJ-jvg8"
#define BLYNK_TEMPLATE_NAME "Hydroponics"
#define BLYNK_AUTH_TOKEN "LeF0NTZtY4-BDmVc2PCHTYq4a_UAQTX4"

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

char ssid[] = "Cun Cun";
char pass[] = "12345689";

// ============= PINS DEFINITION =============
#define DHT_PIN 5
#define TDS_PIN 32
#define PH_PIN 33
#define LIGHT_PIN 34
#define RELAY_LIGHT_PIN 12
#define RELAY_FAN1_PIN 14
#define RELAY_FAN2_PIN 27
#define RELAY_PUMP_PIN 26
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
bool dhtValid = false;
float dhtTempRaw = NAN;
float dhtHumiRaw = NAN;

int displayMode = 0;  // 0:Temp, 1:Humidity, 2:pH, 3:TDS, 4:Light

bool relay_light_state = false;
bool relay_fan1_state = false;
bool relay_fan2_state = false;
bool relay_pump_state = false;
bool autoMode = true;
bool local_light_button = false;
int blynk_light_button = 0;
int blynk_light_timer_min = 0;
bool light_timer_active = false;
unsigned long light_timer_start = 0;

unsigned long lastReadTime = 0;
const unsigned long READ_INTERVAL = 2000;  // Read every 2 seconds

// ============= PH CALIBRATION =============
const float pH_Cal = 0.009;  // Calibration value for pH sensor

// ============= TDS CALIBRATION =============
const float VREF = 3.3;
const float ADC_RESOLUTION = 4095.0;
const int ADC_SAT_THRESHOLD = 4000;

const float TEMP_FAN_ON = 25.0;
const float HUMI_FAN_ON = 80.0;

// ============= BLYNK HANDLERS =============
BLYNK_WRITE(V7) {  // Light button
  blynk_light_button = param.asInt();
  if (!blynk_light_button && !local_light_button) {
    light_timer_active = false;
    light_timer_start = 0;
  }
}

BLYNK_WRITE(V8) {  // Light timer (minutes)
  blynk_light_timer_min = param.asInt();
  if (blynk_light_timer_min > 0) {
    light_timer_active = true;
    light_timer_start = millis();
  } else if (!blynk_light_button && !local_light_button) {
    light_timer_active = false;
    light_timer_start = 0;
  }
}
BLYNK_WRITE(V20) {
  autoMode = param.asInt();
}

BLYNK_WRITE(V10) {
  if (!autoMode) {
    relay_light_state = param.asInt();
    digitalWrite(RELAY_LIGHT_PIN,
                 relay_light_state ? LOW : HIGH);
  }
}

BLYNK_WRITE(V11) {
  if (!autoMode) {
    relay_fan1_state = param.asInt();
    digitalWrite(RELAY_FAN1_PIN,
                 relay_fan1_state ? LOW : HIGH);
  }
}

BLYNK_WRITE(V12) {
  if (!autoMode) {
    relay_fan2_state = param.asInt();
    digitalWrite(RELAY_FAN2_PIN,
                 relay_fan2_state ? LOW : HIGH);
  }
}

BLYNK_WRITE(V13) {
  if (!autoMode) {
    relay_pump_state = param.asInt();
    digitalWrite(RELAY_PUMP_PIN,
                 relay_pump_state ? LOW : HIGH);
  }
}

// ============= SETUP =============
void setup() {
  Serial.begin(115200);
  delay(100);

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.print("Initializing...");

  // Initialize Pins
  pinMode(RELAY_LIGHT_PIN, OUTPUT);
  pinMode(RELAY_FAN1_PIN, OUTPUT);
  pinMode(RELAY_FAN2_PIN, OUTPUT);
  pinMode(RELAY_PUMP_PIN, OUTPUT);
  pinMode(SWITCH1_PIN, INPUT_PULLUP);
  pinMode(SWITCH2_PIN, INPUT_PULLUP);

  // Turn off all relays initially (Active Low)
  digitalWrite(RELAY_LIGHT_PIN, HIGH);
  digitalWrite(RELAY_FAN1_PIN, HIGH);
  digitalWrite(RELAY_FAN2_PIN, HIGH);
  digitalWrite(RELAY_PUMP_PIN, HIGH);

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
  Blynk.run();

  // Handle switches
  handleSwitches();

  // Read sensors at intervals
  if (millis() - lastReadTime >= READ_INTERVAL) {
    readAllSensors();
    controlFans();
    controlLight();
    sendToBlynk();
    digitalWrite(RELAY_PUMP_PIN,
             relay_pump_state ? LOW : HIGH);
    displayLCD();
    printSelected();
    lastReadTime = millis();
  }

  delay(10);
}

// ============= READ ALL SENSORS =============
void readAllSensors() {
  // Read DHT22
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  dhtTempRaw = t;
  dhtHumiRaw = h;
  if (!isnan(t) && !isnan(h)) {
    temperature = t;
    humidity = h;
    dhtValid = true;
  } else {
    dhtValid = false;
  }

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

void controlLight() {

  if (!autoMode) {
    digitalWrite(RELAY_LIGHT_PIN,
                 relay_light_state ? LOW : HIGH);
    return;
  }

  bool timer_on = false;

  if (light_timer_active &&
      blynk_light_timer_min > 0) {

    unsigned long duration_ms =
      (unsigned long)blynk_light_timer_min *
      60000UL;

    timer_on =
      (millis() - light_timer_start) <
      duration_ms;

    if (!timer_on) {
      light_timer_active = false;
    }
  }

  bool light_on =
      (blynk_light_button == 1) ||
      local_light_button ||
      timer_on;

  relay_light_state = light_on;

  digitalWrite(RELAY_LIGHT_PIN,
               relay_light_state ? LOW : HIGH);
}

void controlFans() {

  if (!autoMode) {

    digitalWrite(RELAY_FAN1_PIN,
                 relay_fan1_state ? LOW : HIGH);

    digitalWrite(RELAY_FAN2_PIN,
                 relay_fan2_state ? LOW : HIGH);

    return;
  }

  if (!dhtValid) {

    relay_fan1_state = false;
    relay_fan2_state = false;
  }
  else if (temperature > TEMP_FAN_ON &&
           humidity > HUMI_FAN_ON) {

    relay_fan1_state = true;
    relay_fan2_state = true;
  }
  else if (temperature > TEMP_FAN_ON &&
           humidity <= HUMI_FAN_ON) {

    relay_fan1_state = true;
    relay_fan2_state = false;
  }
  else if (temperature <= TEMP_FAN_ON &&
           humidity > HUMI_FAN_ON) {

    relay_fan1_state = false;
    relay_fan2_state = true;
  }
  else {

    relay_fan1_state = false;
    relay_fan2_state = false;
  }

  digitalWrite(RELAY_FAN1_PIN,
               relay_fan1_state ? LOW : HIGH);

  digitalWrite(RELAY_FAN2_PIN,
               relay_fan2_state ? LOW : HIGH);
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

  // Dòng 1
  lcd.setCursor(0, 0);

  switch (displayMode) {
    case 0:
      lcd.print("TEMP: ");
      lcd.print(temperature, 1);
      lcd.print((char)223);
      lcd.print("C");
      break;

    case 1:
      lcd.print("HUMIDITY: ");
      lcd.print(humidity, 0);
      lcd.print("%");
      break;

    case 2:
      lcd.print("PH: ");
      lcd.print(phValue, 2);
      break;

    case 3:
      lcd.print("TDS: ");
      lcd.print(tdsValue, 0);
      lcd.print("ppm");
      break;

    case 4:
      lcd.print("LIGHT: ");
      lcd.print(lightValue, 0);
      lcd.print("%");
      break;
  }

  // Dòng 2
  lcd.setCursor(0, 1);
  lcd.print("L:");
  lcd.print(relay_light_state ? "ON " : "OFF");

  lcd.print(" F1:");
  lcd.print(relay_fan1_state ? "ON " : "OFF");

  // Dòng 3
  lcd.setCursor(0, 2);
  lcd.print("F2:");
  lcd.print(relay_fan2_state ? "ON " : "OFF");

  lcd.print(" P:");
  lcd.print(relay_pump_state ? "ON " : "OFF");

  // Dòng 4
  lcd.setCursor(0, 3);
  lcd.print("WiFi:");
  lcd.print(WiFi.status() == WL_CONNECTED ? "OK" : "NO");

  lcd.print(" B:");
  lcd.print(Blynk.connected() ? "OK" : "NO");
}

// ============= SEND DATA TO BLYNK =============
void sendToBlynk() {

  // Sensors
  Blynk.virtualWrite(V0, temperature);
  Blynk.virtualWrite(V1, humidity);
  Blynk.virtualWrite(V2, phValue);
  Blynk.virtualWrite(V3, lightValue);
  Blynk.virtualWrite(V4, tdsValue);

  // Relay status
  Blynk.virtualWrite(V10, relay_light_state);
  Blynk.virtualWrite(V11, relay_fan1_state);
  Blynk.virtualWrite(V12, relay_fan2_state);
  Blynk.virtualWrite(V13, relay_pump_state);

  // Auto mode
  Blynk.virtualWrite(V20, autoMode);
}

// ============= HANDLE SWITCHES =============
void handleSwitches() {
  if (digitalRead(SWITCH1_PIN) == LOW) {
    delay(50);  // Debounce
    if (digitalRead(SWITCH1_PIN) == LOW) {
      local_light_button = !local_light_button;
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
    Serial.print("% | RAW: ");
    Serial.print(dhtHumiRaw, 2);
    Serial.print(" | OK: ");
    Serial.println(dhtValid ? "YES" : "NO");
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
    Serial.print(")");
    if (tdsRaw >= ADC_SAT_THRESHOLD) {
      Serial.print(" | SAT");
    }
    Serial.print(" | ADC%:");
    Serial.println((tdsRaw * 100.0) / ADC_RESOLUTION, 1);
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
  relay_light_state = !relay_light_state;
  digitalWrite(RELAY_LIGHT_PIN, relay_light_state ? LOW : HIGH);  // Active Low
  Serial.println(relay_light_state ? "Relay 1 ON" : "Relay 1 OFF");
}

void toggleRelay2() {
  relay_fan1_state = !relay_fan1_state;
  digitalWrite(RELAY_FAN1_PIN, relay_fan1_state ? LOW : HIGH);  // Active Low
  Serial.println(relay_fan1_state ? "Relay 2 ON" : "Relay 2 OFF");
}

void toggleRelay3() {
  relay_fan2_state = !relay_fan2_state;
  digitalWrite(RELAY_FAN2_PIN, relay_fan2_state ? LOW : HIGH);  // Active Low
  Serial.println(relay_fan2_state ? "Relay 3 ON" : "Relay 3 OFF");
}

void toggleRelay4() {
  relay_pump_state = !relay_pump_state;
  digitalWrite(RELAY_PUMP_PIN, relay_pump_state ? LOW : HIGH);  // Active Low
  Serial.println(relay_pump_state ? "Relay 4 ON" : "Relay 4 OFF");
}
