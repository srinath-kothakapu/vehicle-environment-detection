/*
  Integrated Sensor Network for Vehicle Environment Detection
  Arduino Uno + DHT11 + MQ gas sensor + IR sensor + LDR + 16x2 LCD + ESP8266 (ThingSpeak)

  NOTE: This is a cleaned-up version of the sketch listing printed in the project report.
  The original listing was pasted into the report with broken formatting and some missing
  lines, so the structure (pins, thresholds, messages, upload fields) follows the report,
  and the helper functions (WiFi connect, ThingSpeak upload) were rewritten in the usual
  ESP8266 AT-command style. Check the three TODO pins against your own wiring before use.

  Libraries: "DHT sensor library" (Adafruit), LiquidCrystal (built in), SoftwareSerial (built in)
  Credentials: copy secrets.h.example to secrets.h and fill in your own values.
*/

#include <DHT.h>
#include <LiquidCrystal.h>
#include <SoftwareSerial.h>
#include "secrets.h"  // WIFI_SSID, WIFI_PASS, THINGSPEAK_API_KEY (not committed)

// ---------- Pins (from the report listing) ----------
const int LCD_RS = 13, LCD_EN = 12, LCD_D4 = 11, LCD_D5 = 10, LCD_D6 = 9, LCD_D7 = 8;
const int ESP_RX = 2, ESP_TX = 3;   // Arduino pins connected to the ESP8266 (SoftwareSerial)
#define DHT_PIN A0
#define DHT_TYPE DHT11
const int LED_PIN = 4;              // alert LED
const int MOT1 = 5;                 // output switched on when no light is detected (car light)
const int MOT2 = 6;                 // output switched on when the IR sensor detects something
const int MOT3 = 7;                 // not used in the listing

// TODO: these three input pins were not in the report listing. Set them to your wiring.
const int GAS_PIN = A1;             // gas sensor digital output (LOW = gas detected)
const int IR_PIN  = A2;             // IR sensor digital output (LOW = object detected)
const int LDR_PIN = A3;             // LDR module digital output (HIGH = no light)

// ---------- Thresholds (from the report listing) ----------
const float HUMIDITY_LIMIT_PCT = 80.0;
const float TEMPERATURE_LIMIT_C = 40.0;
const unsigned long UPLOAD_INTERVAL_MS = 15000;  // ThingSpeak free accounts allow one update per 15 s

LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);
SoftwareSerial esp(ESP_RX, ESP_TX);
DHT dht(DHT_PIN, DHT_TYPE);

// ---------- ESP8266 helpers ----------
bool sendAT(const String &cmd, const char *expect, unsigned long timeoutMs) {
  esp.setTimeout(timeoutMs);
  esp.println(cmd);
  return esp.find(const_cast<char *>(expect));
}

bool connectWifi() {
  esp.setTimeout(8000);
  if (!sendAT("AT", "OK", 2000)) return false;
  sendAT("AT+CWMODE=1", "OK", 2000);
  String join = String("AT+CWJAP=\"") + WIFI_SSID + "\",\"" + WIFI_PASS + "\"";
  return sendAT(join, "OK", 8000);
}

bool uploadToThingSpeak(float humi, float temp, int gas, int ir, int ldr) {
  // Fields: 1 humidity, 2 temperature, 3 gas, 4 IR, 5 LDR
  String req = String("GET /update?api_key=") + THINGSPEAK_API_KEY +
               "&field1=" + String(humi) + "&field2=" + String(temp) +
               "&field3=" + String(gas) + "&field4=" + String(ir) +
               "&field5=" + String(ldr) + "\r\n\r\n";
  if (!sendAT("AT+CIPSTART=\"TCP\",\"api.thingspeak.com\",80", "OK", 4000)) return false;
  esp.println(String("AT+CIPSEND=") + req.length());
  if (!esp.find(const_cast<char *>(">"))) {
    esp.println("AT+CIPCLOSE");
    return false;
  }
  esp.print(req);
  return true;
}

// ---------- LCD helpers ----------
void showReadings(float humi, float temp, bool gasDetected, bool irDetected) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("H:");
  lcd.setCursor(3, 0); lcd.print(humi);
  lcd.setCursor(9, 0); lcd.print("T:");
  lcd.setCursor(11, 0); lcd.print(temp);
  lcd.setCursor(0, 1); lcd.print("G:");
  lcd.setCursor(3, 1); lcd.print(gasDetected ? "YES" : "NO");
  lcd.setCursor(9, 1); lcd.print("F:");               // second flag on the display: IR state
  lcd.setCursor(11, 1); lcd.print(irDetected ? "YES" : "NO");
}

void showAlert(const char *message) {
  digitalWrite(LED_PIN, HIGH);
  delay(1500);
  lcd.clear();
  lcd.print(message);
  digitalWrite(LED_PIN, LOW);
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(MOT1, OUTPUT);
  pinMode(MOT2, OUTPUT);
  pinMode(MOT3, OUTPUT);
  pinMode(GAS_PIN, INPUT);
  pinMode(IR_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(MOT1, LOW);
  digitalWrite(MOT2, LOW);
  digitalWrite(MOT3, LOW);

  Serial.begin(9600);
  esp.begin(9600);
  dht.begin();
  lcd.begin(16, 2);
  lcd.print("WELCOME");
  delay(1000);

  Serial.println("Connecting to WiFi");
  lcd.clear();
  lcd.print("CONNECTING WIFI");
  uint8_t attempts = 0;
  while (!connectWifi()) {
    Serial.println("Trying to reconnect..");
    lcd.clear();
    lcd.print("TRYING TO RECONNECT");
    delay(1000);
    if (++attempts >= 5) break;   // keep running locally even without WiFi
  }
  lcd.clear();
  lcd.print(attempts >= 5 ? "NO WIFI" : "WIFI CONNECTED");
  delay(2000);
}

void loop() {
  float humi = dht.readHumidity();
  float temp = dht.readTemperature();
  int gas = digitalRead(GAS_PIN);
  int ir = digitalRead(IR_PIN);
  int ldr = digitalRead(LDR_PIN);

  if (isnan(humi) || isnan(temp)) {
    lcd.clear();
    lcd.print("DHT READ ERROR");
    delay(2000);
    return;
  }

  bool gasDetected = (gas == LOW);
  bool irDetected = (ir == LOW);
  bool noLight = (ldr == HIGH);

  // Outputs
  digitalWrite(MOT1, noLight ? HIGH : LOW);     // car light on when it is dark
  digitalWrite(MOT2, irDetected ? HIGH : LOW);  // IR event output

  showReadings(humi, temp, gasDetected, irDetected);

  // Alert messages, in the same order as the report listing
  if (humi > HUMIDITY_LIMIT_PCT) showAlert("HUMIDITY HIGH");
  if (temp > TEMPERATURE_LIMIT_C) showAlert("TEMP HIGH");
  if (gasDetected) showAlert("GAS DETECT");
  if (irDetected) showAlert("IR DETECT");
  if (noLight) showAlert("NO LIGHT");

  uploadToThingSpeak(humi, temp, gas, ir, ldr);

  delay(UPLOAD_INTERVAL_MS);   // wait before the next update
}
