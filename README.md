# Vehicle Environment Detection System

An Arduino Uno environmental-monitoring example reconstructed from the project
description in the author's CV. It samples IR, LDR, MQ-2, and DHT11 sensors;
shows readings on a 16x2 LCD; and drives a buzzer for detected hazards. An
optional ESP8266 running AT firmware reports readings to ThingSpeak.

> **Reconstruction notice:** The original firmware and wiring were not
> available. This is a new, working example based on the CV-listed components,
> not the original 2023 project source. Sensor readings, calibration, wiring,
> and performance must be verified on the actual hardware. The CV's sampling
> and false-positive figures are not validated by this code.

## Hardware assumptions and pinout

The pinout is an example for an Arduino Uno. Check the voltage and wiring of
your own modules before connecting them. Do not feed a 5 V UART output into an
ESP8266 RX pin.

| Component | Arduino Uno connection |
|---|---|
| ESP8266 RX / TX | D2 / D3 (SoftwareSerial, 9600 baud; use 3.3 V level shifting) |
| DHT11 data | D4 |
| IR sensor digital output | D5, active LOW |
| Buzzer driver input | D6 |
| LCD RS / Enable | D7 / D8 |
| LCD D4 / D5 / D6 / D7 | D9 / D10 / D11 / D12 |
| LDR voltage divider | A0 |
| MQ-2 analog output | A1 |

TimerOne uses Timer1, so this example reserves that timer for the 10 Hz sample
tick. Reassign the LCD pins if they conflict with other hardware. Use an
appropriate load driver for buzzers that exceed a GPIO pin's current rating.
MQ-2 heaters require a suitable power supply and warm-up; the example threshold
is a starting point, not a calibrated gas concentration or safety device.

## Build and upload

1. Install [PlatformIO Core](https://platformio.org/install/cli) or use the
   PlatformIO extension in VS Code.
2. Open this repository as a PlatformIO project.
3. Optionally copy `src/secrets.example.h` to the ignored `src/secrets.h` and
   set `WIFI_SSID`, `WIFI_PASSWORD`, and `THINGSPEAK_WRITE_API_KEY`. Never
   commit the file containing your credentials. Cloud uploads are disabled
   when these values are blank.
4. Build and upload to the Uno:

   ```sh
   pio run -e uno
   pio run -e uno -t upload
   pio device monitor -b 115200
   ```

The ESP8266 must already run ESP-AT firmware and be wired using 3.3 V UART
levels. The ThingSpeak write key is sent to the ESP8266 for HTTP transmission;
use Wi-Fi credentials and a write-only key you can rotate.

## Sampling and cloud fields

TimerOne generates a 100 ms tick; the foreground loop reads the analog/digital
sensors on each tick. DHT11 measurements are limited to one every two seconds.
The ThingSpeak client uploads no more often than every 20 seconds, above the
usual free-channel 15-second interval.

The ESP8266 uses `SoftwareSerial` on the Uno, which can briefly mask interrupts
when sending. Validate the actual sample cadence on hardware; a strict 10 Hz
guarantee is not claimed. A hardware UART or separate network coprocessor is
preferable when uninterrupted sampling is required.

ThingSpeak fields are `field1` temperature in tenths of a degree Celsius,
`field2` relative humidity in percent, `field3` MQ-2 ADC counts,
`field4` LDR ADC counts, and `field5` IR detection (`1` = detected). The analog
smoke alarm threshold defaults to 600 on the 10-bit ADC and must be calibrated
for the sensor, load, and environment.

## Project status

The sketch and PlatformIO build configuration are a reconstructed starter
implementation. No physical hardware was available here, so electrical
connections, sensor behavior, cloud connectivity, alarm timing, the CV's
claimed sampling rate, and false-positive rate have not been hardware-tested.
