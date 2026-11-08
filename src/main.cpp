#include <Arduino.h>
#include <DHT.h>
#include <SoftwareSerial.h>
#include <TimerOne.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define THINGSPEAK_WRITE_API_KEY ""
#endif

namespace {

constexpr uint8_t kEspRxPin = 2;
constexpr uint8_t kEspTxPin = 3;
constexpr uint8_t kDhtPin = 4;
constexpr uint8_t kIrPin = 5;
constexpr uint8_t kBuzzerPin = 6;
constexpr uint8_t kLcdRsPin = 7;
constexpr uint8_t kLcdEnablePin = 8;
constexpr uint8_t kLcdD4Pin = 9;
constexpr uint8_t kLcdD5Pin = 10;
constexpr uint8_t kLcdD6Pin = 11;
constexpr uint8_t kLcdD7Pin = 12;
constexpr uint8_t kLdrPin = A0;
constexpr uint8_t kMq2Pin = A1;
constexpr uint16_t kSmokeThreshold = 600;
constexpr uint32_t kSensorTickUs = 100000;
constexpr uint32_t kDhtPeriodMs = 2000;
constexpr uint32_t kCloudPeriodMs = 20000;
constexpr uint32_t kWifiTimeoutMs = 10000;
constexpr uint32_t kRetryDelayMs = 10000;

enum class WifiState : uint8_t {
  Disabled,
  SendAttention,
  WaitAttention,
  WaitMode,
  WaitJoin,
  Idle,
  WaitConnect,
  WaitPrompt,
  WaitHttp,
  RetryDelay,
};

class CharacterLcd {
 public:
  CharacterLcd(uint8_t rs, uint8_t enable, uint8_t d4, uint8_t d5, uint8_t d6,
                uint8_t d7)
      : rs_(rs), enable_(enable), data_pins_{d4, d5, d6, d7} {}

  void begin(uint8_t columns, uint8_t rows) {
    columns_ = columns;
    rows_ = rows > 2 ? 2 : rows;
    pinMode(rs_, OUTPUT);
    pinMode(enable_, OUTPUT);
    for (uint8_t pin : data_pins_) {
      pinMode(pin, OUTPUT);
    }

    delay(50);
    digitalWrite(rs_, LOW);
    writeNibble(0x03);
    delay(5);
    writeNibble(0x03);
    delayMicroseconds(150);
    writeNibble(0x03);
    writeNibble(0x02);
    command(0x28);
    command(0x08);
    command(0x01);
    command(0x06);
    command(0x0C);
  }

  void setCursor(uint8_t column, uint8_t row) {
    static const uint8_t rowOffsets[] = {0x00, 0x40};
    if (row >= rows_) {
      row = rows_ - 1;
    }
    if (column >= columns_) {
      column = columns_ - 1;
    }
    command(static_cast<uint8_t>(0x80 | (rowOffsets[row] + column)));
  }

  void print(const char *text) {
    while (*text != '\0') {
      write(static_cast<uint8_t>(*text++), true);
    }
  }

  void print(char value) { write(static_cast<uint8_t>(value), true); }

 private:
  void pulseEnable() {
    digitalWrite(enable_, LOW);
    digitalWrite(enable_, HIGH);
    delayMicroseconds(1);
    digitalWrite(enable_, LOW);
    delayMicroseconds(50);
  }

  void writeNibble(uint8_t nibble) {
    for (uint8_t bit = 0; bit < 4; ++bit) {
      digitalWrite(data_pins_[bit], (nibble >> bit) & 0x01);
    }
    pulseEnable();
  }

  void write(uint8_t value, bool data) {
    digitalWrite(rs_, data ? HIGH : LOW);
    writeNibble(value >> 4);
    writeNibble(value & 0x0F);
  }

  void command(uint8_t value) {
    write(value, false);
    if (value == 0x01 || value == 0x02) {
      delay(2);
    }
  }

  uint8_t rs_;
  uint8_t enable_;
  uint8_t data_pins_[4];
  uint8_t columns_ = 16;
  uint8_t rows_ = 2;
};

DHT dht(kDhtPin, DHT11);
CharacterLcd lcd(kLcdRsPin, kLcdEnablePin, kLcdD4Pin, kLcdD5Pin, kLcdD6Pin,
                 kLcdD7Pin);
SoftwareSerial esp(kEspRxPin, kEspTxPin);

volatile uint8_t pendingTicks = 0;
uint32_t lastDhtMs = 0;
uint32_t lastLcdMs = 0;
uint32_t lastSerialMs = 0;
uint32_t lastCloudMs = 0;
uint32_t wifiDeadlineMs = 0;
uint16_t smokeReading = 0;
uint16_t lightReading = 0;
float temperatureC = NAN;
float humidityPercent = NAN;
bool dhtValid = false;
bool irDetected = false;
bool alarmActive = true;
WifiState wifiState = WifiState::Disabled;
char wifiResponse[96] = {};
size_t wifiResponseLength = 0;
char httpRequest[160] = {};

void onSensorTick() {
  if (pendingTicks != UINT8_MAX) {
    ++pendingTicks;
  }
}

bool cloudCredentialsConfigured() {
  return WIFI_SSID[0] != '\0' && WIFI_PASSWORD[0] != '\0' &&
         THINGSPEAK_WRITE_API_KEY[0] != '\0';
}

void resetWifiResponse() {
  wifiResponseLength = 0;
  wifiResponse[0] = '\0';
}

void rememberWifiResponse(char value) {
  if (wifiResponseLength >= sizeof(wifiResponse) - 1) {
    memmove(wifiResponse, wifiResponse + 1, sizeof(wifiResponse) - 2);
    wifiResponseLength = sizeof(wifiResponse) - 2;
  }
  wifiResponse[wifiResponseLength++] = value;
  wifiResponse[wifiResponseLength] = '\0';
}

bool wifiResponseContains(const char *text) {
  return strstr(wifiResponse, text) != nullptr;
}

void sendWifiCommand(const char *command, WifiState waitState, uint32_t now,
                     uint32_t timeoutMs = kWifiTimeoutMs) {
  resetWifiResponse();
  esp.println(command);
  wifiState = waitState;
  wifiDeadlineMs = now + timeoutMs;
}

void scheduleWifiRetry(uint32_t now) {
  resetWifiResponse();
  wifiState = WifiState::RetryDelay;
  wifiDeadlineMs = now + kRetryDelayMs;
}

void startWifiJoin(uint32_t now) {
  char command[112];
  const int count =
      snprintf(command, sizeof(command), "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID,
               WIFI_PASSWORD);
  if (count < 0 || static_cast<size_t>(count) >= sizeof(command)) {
    Serial.println(F("Wi-Fi credentials exceed the AT command buffer."));
    scheduleWifiRetry(now);
    return;
  }
  sendWifiCommand(command, WifiState::WaitJoin, now, 30000);
}

void prepareCloudUpdate() {
  const int tempTenths =
      isnan(temperatureC) ? 0 : static_cast<int>(temperatureC * 10.0f);
  const int count = snprintf(
      httpRequest, sizeof(httpRequest),
      "GET /update?api_key=%s&field1=%d&field2=%d&field3=%u&field4=%u&field5=%u "
      "HTTP/1.1\r\nHost: api.thingspeak.com\r\nConnection: close\r\n\r\n",
      THINGSPEAK_WRITE_API_KEY, tempTenths,
      isnan(humidityPercent) ? 0 : static_cast<int>(humidityPercent),
      static_cast<unsigned int>(smokeReading),
      static_cast<unsigned int>(lightReading),
      static_cast<unsigned int>(irDetected ? 1 : 0));
  if (count < 0 || static_cast<size_t>(count) >= sizeof(httpRequest)) {
    Serial.println(F("ThingSpeak request exceeds the AT command buffer."));
    return;
  }

  char command[32];
  snprintf(command, sizeof(command), "AT+CIPSEND=%u",
           static_cast<unsigned int>(count));
  sendWifiCommand(command, WifiState::WaitPrompt, millis());
}

void pollWifi(uint32_t now) {
  while (esp.available() > 0) {
    rememberWifiResponse(static_cast<char>(esp.read()));
  }

  switch (wifiState) {
    case WifiState::Disabled:
      return;

    case WifiState::SendAttention:
      sendWifiCommand("AT", WifiState::WaitAttention, now);
      return;

    case WifiState::WaitAttention:
      if (wifiResponseContains("OK")) {
        sendWifiCommand("AT+CWMODE=1", WifiState::WaitMode, now);
      } else if (wifiResponseContains("ERROR") || now >= wifiDeadlineMs) {
        scheduleWifiRetry(now);
      }
      return;

    case WifiState::WaitMode:
      if (wifiResponseContains("OK")) {
        startWifiJoin(now);
      } else if (wifiResponseContains("ERROR") || now >= wifiDeadlineMs) {
        scheduleWifiRetry(now);
      }
      return;

    case WifiState::WaitJoin:
      if (wifiResponseContains("WIFI GOT IP")) {
        resetWifiResponse();
        wifiState = WifiState::Idle;
        lastCloudMs = now;
      } else if (wifiResponseContains("FAIL") || now >= wifiDeadlineMs) {
        scheduleWifiRetry(now);
      }
      return;

    case WifiState::Idle:
      if (static_cast<uint32_t>(now - lastCloudMs) >= kCloudPeriodMs &&
          dhtValid) {
        lastCloudMs = now;
        sendWifiCommand("AT+CIPSTART=\"TCP\",\"api.thingspeak.com\",80",
                        WifiState::WaitConnect, now);
      }
      return;

    case WifiState::WaitConnect:
      if (wifiResponseContains("CONNECT") || wifiResponseContains("ALREADY")) {
        prepareCloudUpdate();
      } else if (wifiResponseContains("ERROR") || now >= wifiDeadlineMs) {
        scheduleWifiRetry(now);
      }
      return;

    case WifiState::WaitPrompt:
      if (wifiResponseContains(">")) {
        resetWifiResponse();
        esp.print(httpRequest);
        wifiState = WifiState::WaitHttp;
        wifiDeadlineMs = now + 8000;
      } else if (wifiResponseContains("ERROR") || now >= wifiDeadlineMs) {
        scheduleWifiRetry(now);
      }
      return;

    case WifiState::WaitHttp:
      if (wifiResponseContains("200 OK")) {
        resetWifiResponse();
        wifiState = WifiState::Idle;
      } else if (wifiResponseContains("ERROR") || now >= wifiDeadlineMs) {
        scheduleWifiRetry(now);
      }
      return;

    case WifiState::RetryDelay:
      if (now >= wifiDeadlineMs) {
        wifiState = WifiState::SendAttention;
      }
      return;
  }
}

void readSensors(uint32_t now) {
  smokeReading = static_cast<uint16_t>(analogRead(kMq2Pin));
  lightReading = static_cast<uint16_t>(analogRead(kLdrPin));
  irDetected = digitalRead(kIrPin) == LOW;

  if (static_cast<uint32_t>(now - lastDhtMs) >= kDhtPeriodMs) {
    lastDhtMs = now;
    const float newTemperatureC = dht.readTemperature();
    const float newHumidityPercent = dht.readHumidity();
    dhtValid = !isnan(newTemperatureC) && !isnan(newHumidityPercent);
    if (dhtValid) {
      temperatureC = newTemperatureC;
      humidityPercent = newHumidityPercent;
    }
  }

  alarmActive = smokeReading >= kSmokeThreshold || irDetected || !dhtValid;
  digitalWrite(kBuzzerPin, alarmActive ? HIGH : LOW);
}

void updateDisplay(uint32_t now) {
  if (static_cast<uint32_t>(now - lastLcdMs) < 500) {
    return;
  }
  lastLcdMs = now;

  char line[17];
  lcd.setCursor(0, 0);
  if (dhtValid) {
    snprintf(line, sizeof(line), "T:%2d.%dC H:%2d%%",
             static_cast<int>(temperatureC),
             abs(static_cast<int>(temperatureC * 10.0f)) % 10,
             static_cast<int>(humidityPercent));
  } else {
    snprintf(line, sizeof(line), "DHT11 reading...");
  }
  lcd.print(line);
  for (uint8_t i = strlen(line); i < 16; ++i) {
    lcd.print(' ');
  }

  lcd.setCursor(0, 1);
  snprintf(line, sizeof(line), "MQ:%4u IR:%s",
           static_cast<unsigned int>(smokeReading),
           irDetected ? "HIT" : "OK");
  lcd.print(line);
  for (uint8_t i = strlen(line); i < 16; ++i) {
    lcd.print(' ');
  }
}

void printTelemetry(uint32_t now) {
  if (static_cast<uint32_t>(now - lastSerialMs) < 1000) {
    return;
  }
  lastSerialMs = now;
  Serial.print(F("VALID="));
  Serial.print(dhtValid ? 1 : 0);
  Serial.print(F(",TEMP_C="));
  if (dhtValid) {
    Serial.print(temperatureC, 1);
  } else {
    Serial.print(F("NA"));
  }
  Serial.print(F(",HUMIDITY_PERCENT="));
  if (dhtValid) {
    Serial.print(humidityPercent, 0);
  } else {
    Serial.print(F("NA"));
  }
  Serial.print(F(",MQ2_ADC="));
  Serial.print(smokeReading);
  Serial.print(F(",LDR_ADC="));
  Serial.print(lightReading);
  Serial.print(F(",IR_DETECTED="));
  Serial.print(irDetected ? 1 : 0);
  Serial.print(F(",ALARM="));
  Serial.println(alarmActive ? 1 : 0);
}

}  // namespace

void setup() {
  pinMode(kIrPin, INPUT_PULLUP);
  pinMode(kBuzzerPin, OUTPUT);
  digitalWrite(kBuzzerPin, HIGH);
  pinMode(kLdrPin, INPUT);
  pinMode(kMq2Pin, INPUT);

  Serial.begin(115200);
  lcd.begin(16, 2);
  lcd.print("Env monitor boot");
  dht.begin();
  Timer1.initialize(kSensorTickUs);
  Timer1.attachInterrupt(onSensorTick);

  esp.begin(9600);
  esp.listen();
  if (cloudCredentialsConfigured()) {
    wifiState = WifiState::SendAttention;
    Serial.println(F("ThingSpeak upload enabled."));
  } else {
    wifiState = WifiState::Disabled;
    Serial.println(F("Cloud disabled; configure src/secrets.h to enable."));
  }
  Serial.println(F("MQ-2 sensor warm-up/calibration is installation-specific."));
}

void loop() {
  uint8_t ticks = 0;
  noInterrupts();
  ticks = pendingTicks;
  pendingTicks = 0;
  interrupts();

  while (ticks > 0) {
    --ticks;
    readSensors(millis());
  }

  const uint32_t now = millis();
  updateDisplay(now);
  printTelemetry(now);
  pollWifi(now);
}
