# Integrated Sensor Network for Vehicle Environment Detection

This repository contains an Arduino Uno environmental-monitoring prototype for a vehicle cabin. A DHT11 provides temperature and humidity, a digital-output MQ-series gas sensor module provides a gas-warning signal, an IR module provides a nearby-object signal, and an LDR module provides a light/dark signal. The Arduino presents readings and sensor flags on a 16x2 character LCD, drives a local alert LED and low-voltage logic outputs, and can send readings to a ThingSpeak channel through an ESP8266 Wi-Fi module.

This was a final-year B.Tech project in Electronics and Communication Engineering at Sreenidhi Institute of Science and Technology, Hyderabad. It is a team project. The sketch in this repository is a cleaned-up reconstruction of the code listing from the project report: missing or malformed Wi-Fi and ThingSpeak helper code was rewritten in an ESP8266 AT-command style. The hardware input pins that were not specified in the report are marked for verification. Check the code and wiring against your actual prototype before using it.

![Assembled prototype](docs/images/prototype_overview.jpg)

## Project goals

The project demonstrates how a small microcontroller prototype can combine several sensor signals, apply simple threshold and digital-state checks, show local status, and upload data for remote visualization. It is a learning/prototype project, not an automotive safety system or an air-quality instrument.

The readings available in the sketch are:

| Module | Signal used by the firmware | Display / action |
|---|---|---|
| DHT11 | Temperature in C and relative humidity in percent | Both values appear on the LCD and are compared with fixed demo limits |
| MQ-2 gas sensor module | Digital output level | A LOW output is treated as `GAS DETECT`; the module is not used to calculate gas concentration |
| IR sensor module | Digital output level | A LOW output is treated as `IR DETECT` and switches the corresponding logic output |
| LDR module | Digital output level | A HIGH output is treated as no light and switches the corresponding logic output |

The precise behavior and active polarity of digital modules can vary with the particular breakout board and its comparator settings. Verify each module's output against its documentation rather than assuming every board uses the same logic level.

## System overview

```text
 DHT11 ───────────────┐
 MQ-2 digital output ─┤
 IR digital output ───┼──> Arduino Uno ──> 16x2 LCD
 LDR digital output ─┤        │          alert LED
                      │        ├───────── logic outputs
                      │        │
                      └────────┘
                               │ SoftwareSerial AT commands
                               ▼
                            ESP8266 ──Wi-Fi/HTTP──> ThingSpeak
```

The firmware processes one set of readings in a loop:

1. Read temperature and humidity from the DHT11, then read the digital gas, IR, and LDR signals.
2. If the DHT11 returns an invalid value, display a sensor-read error and skip that cycle.
3. Set the configurable logic outputs for the dark and IR states, and display the latest values and gas/IR flags.
4. Check the fixed temperature/humidity limits and the digital input states. Each active condition turns on the alert LED and displays its message.
5. Send the measurement and raw digital levels to ThingSpeak, then wait for the next update interval.

The LCD shows humidity, temperature, gas status, and IR status. The LDR and output state are shown indirectly through the alert message and the `MOT1` output.

## Alerts and cloud fields

The current thresholds and upload period are defined in [`firmware/vehicle_env_detection/vehicle_env_detection.ino`](firmware/vehicle_env_detection/vehicle_env_detection.ino):

| Condition in the sketch | LCD message | Local behavior |
|---|---|---|
| Humidity above 80% | `HUMIDITY HIGH` | Alert LED for the message interval |
| Temperature above 40 C | `TEMP HIGH` | Alert LED for the message interval |
| Gas module digital output LOW | `GAS DETECT` | Alert LED; LCD shows gas flag |
| IR module digital output LOW | `IR DETECT` | Alert LED; `MOT2` is switched on |
| LDR module output HIGH | `NO LIGHT` | Alert LED; `MOT1` is switched on |

ThingSpeak channel field mapping in the sketch:

| ThingSpeak field | Value sent |
|---|---|
| Field 1 | Relative humidity |
| Field 2 | Temperature |
| Field 3 | Raw gas-module digital output level |
| Field 4 | Raw IR-module digital output level |
| Field 5 | Raw LDR-module digital output level |

The firmware waits 15 seconds between data uploads, matching the interval used for a free ThingSpeak account in this example. The local `MOT1` and `MOT2` pins are logic outputs only. Do not connect automotive lamps, motors, or other high-current loads directly to Arduino pins; appropriate drivers, isolation, fusing, and a vehicle-rated power design would be required.

## Prototype photos and example observations

The repository includes photos of the physical prototype and example LCD and ThingSpeak views. The readings are examples from the project prototype; they are not calibration evidence or validated safety limits.

| LCD during normal operation | Gas module alert |
|---|---|
| ![Normal LCD readings](docs/images/prototype_lcd_normal.jpg) | ![Gas alert on the LCD](docs/images/lcd_gas_detected.jpg) |

| Low-light alert | ThingSpeak charts |
|---|---|
| ![Low-light state on the LCD](docs/images/lcd_no_light.jpg) | ![ThingSpeak charts for the prototype](docs/images/thingspeak_charts.png) |

Example observations recorded in the existing project notes include humidity of 31% and temperature of 32.7 C while the gas input was active, and humidity of 32% and temperature of 32.2 C with no alerts. These are sample observations, not a systematic evaluation.

## Hardware and pin mapping

The following pin map is in the sketch. In particular, the GAS, IR, and LDR input pins were not present in the original report listing and are explicitly marked in the firmware as pins to verify against your own wiring.

| Part / signal | Arduino Uno pin | Notes |
|---|---|---|
| 16x2 LCD RS, EN, D4-D7 | 13, 12, 11, 10, 9, 8 | Uses the Arduino `LiquidCrystal` parallel interface |
| ESP8266 serial RX, TX | 2, 3 | `SoftwareSerial`; cross these with the module's TX and RX respectively |
| DHT11 data | A0 | Sensor type is configured as DHT11 |
| Alert LED | 4 | Use a current-limiting resistor |
| Dark-state logic output (`MOT1`) | 5 | HIGH when the LDR module indicates no light |
| IR-state logic output (`MOT2`) | 6 | HIGH when the IR module indicates detection |
| Unused output (`MOT3`) | 7 | Configured as an output but not used in the current sketch |
| Gas module digital output | A1 | Verify the pin and active polarity for your module |
| IR module digital output | A2 | Verify the pin and active polarity for your module |
| LDR module digital output | A3 | Verify the pin and active polarity for your module |

### Parts in the prototype

- Arduino Uno (ATmega328P)
- DHT11 temperature/humidity module
- MQ-series gas sensor module with a digital output
- IR sensor module with a digital output
- LDR light-sensor module with a digital output
- 16x2 character LCD connected in parallel
- ESP8266 Wi-Fi module configured for AT commands
- Alert LED, appropriate resistor, wiring, and regulated supplies

Check the exact voltage and current requirements of every breakout board. The Arduino Uno is a 5 V board, while ESP8266 GPIO is not 5 V tolerant. Use a suitable level shifter or a correctly rated interface on every signal connected to the ESP8266, and provide appropriate power for the ESP8266's current draw. The sketch's sample wiring is not a substitute for checking each board's electrical requirements.

## Build and configure

### Requirements

- Arduino IDE
- Arduino Uno and compatible USB cable
- Adafruit **DHT sensor library** (and its Adafruit Unified Sensor dependency as required by the installed version)
- ESP8266 AT-command firmware and a ThingSpeak channel if you want network uploads
- The built-in `LiquidCrystal` and `SoftwareSerial` Arduino libraries

### Steps

1. Clone this repository and open `firmware/vehicle_env_detection/vehicle_env_detection.ino` in the Arduino IDE.
2. Install the Adafruit DHT sensor library and any dependency requested by the library manager.
3. Copy [`firmware/vehicle_env_detection/secrets.h.example`](firmware/vehicle_env_detection/secrets.h.example) to `secrets.h` in the same sketch folder. Set your own Wi-Fi SSID, Wi-Fi password, and ThingSpeak write API key in that local file.
4. Keep the real `secrets.h` out of Git. The repository provides only the example template; never publish Wi-Fi credentials or a live write key in source code or screenshots.
5. Confirm the three marked sensor input pins and all output wiring. Verify the ESP8266 serial connection and logic levels.
6. Select the Arduino Uno and correct serial port in the IDE, compile, and upload.
7. Open the serial monitor at 9600 baud to inspect the Wi-Fi connection messages. The ESP8266 `SoftwareSerial` link also runs at 9600 baud in this sketch.

The firmware attempts to join Wi-Fi during setup. After several unsuccessful attempts it continues to read sensors and display local conditions, but remote ThingSpeak uploads will not work until connectivity and credentials are corrected.

## ThingSpeak configuration

Create a ThingSpeak channel with at least the five fields described above. Put the channel's **write** API key in your local `secrets.h`; do not commit it. The sketch sends a simple HTTP request to `api.thingspeak.com` over port 80 using ESP8266 AT commands. That simple educational transport should not be treated as a production secure channel: avoid sending sensitive data or using a write key that must remain private if the network is untrusted. If you reuse the approach beyond a classroom prototype, replace it with a transport and credential design appropriate for the deployment.

ThingSpeak field 3-5 record the digital pin levels, not calibrated gas, distance, or illumination measurements. Chart labels and dashboard interpretation should make that distinction clear.

## Repository contents

```text
firmware/vehicle_env_detection/
  vehicle_env_detection.ino  Arduino application and ESP8266 AT helpers
  secrets.h.example          Placeholder template; add local credentials to secrets.h
  secrets.h                  Local-only credentials; do not commit

docs/images/
  prototype_overview.jpg     Assembled prototype
  prototype_lcd_normal.jpg   Example normal LCD state
  lcd_gas_detected.jpg       Example gas-module alert
  lcd_no_light.jpg           Example low-light state
  thingspeak_charts.png      Example cloud charts
```

## Known limitations and safe-use notes

- This is a prototype and a cleaned-up reconstruction of a report listing, not a reviewed or automotive-qualified design.
- The gas input is a digital threshold output from a low-cost sensor module. It cannot identify a gas or report a calibrated concentration; do not describe it as a carbon-monoxide or life-safety detector.
- The IR and LDR behavior depends on the specific module, adjustment, pin, and active level. The sketch leaves those three input pins for you to verify.
- Temperature and humidity thresholds are hard-coded demonstration values. The sensors are not documented as calibrated and are not suitable as authoritative safety limits.
- The LED alert routine blocks for 1.5 seconds per displayed alert. When several conditions are active, a full pass takes longer; it is not a real-time automotive controller.
- `MOT3` is currently unused. The code does not implement a production car-light or motor driver.
- Wi-Fi upload uses an ESP8266 AT-command interface and simple HTTP on port 80. It has no production credential, transport, or network security design.
- The source does not establish suitability for a real vehicle, safety-critical application, or unattended alarm.

Use the project for education and controlled bench demonstrations only. Do not rely on it to protect people, detect hazardous gas, control vehicle equipment, or replace certified sensors or alarms.

## Team and credits

This repository represents a team project originally completed as a final-year B.Tech project in Electronics and Communication Engineering at Sreenidhi Institute of Science and Technology, Hyderabad. The repository contains the reconstructed sketch and supporting prototype images. Add or correct individual team credits here if all contributors agree on the attribution to publish.

## Future work

- Verify and document the pin map against the assembled prototype.
- Record reproducible bench tests, calibration checks, and sensor response limitations.
- Replace blocking alert delays with non-blocking timing if the application needs responsive periodic sampling.
- Use a secure, well-documented network transport and safer secret handling for cloud telemetry.
- Add timestamped local logging and, where useful, a validated database-backed dashboard.
- Review the electrical design, sensor suitability, and applicable safety requirements before considering any real vehicle installation.

