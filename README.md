# Tunis Watch

ESP32-CAM smartwatch firmware and Android companion app. The default OLED screen shows Tunis time and motion activity. A button opens a scrollable menu for Heart Rate, Yaw, Sound, Activity, Find Phone, Pong, and Back. See [the full project guide](docs/PROJECT_GUIDE.md) for setup, operation, architecture, and troubleshooting.

## Wiring

| Device | ESP32-CAM |
| --- | --- |
| OLED, MAX30102, MPU6050 SDA | GPIO 13 |
| OLED, MAX30102, MPU6050 SCL | GPIO 14 |
| MAX4466 output | GPIO 15 |
| Up button | GPIO 2 to GND |
| Down button | GPIO 4 to GND |
| OK / Back button | GPIO 12 to GND |

All modules share GND. Power each module according to its rated supply; the MAX4466 output must stay within the ESP32 input voltage range. The buttons use `INPUT_PULLUP`, so press connects the GPIO to GND. GPIO 4 is shared with the onboard flash LED; GPIO 12 is a boot strapping pin and must not be pulled HIGH at power-on. The microSD slot is unused.

## Watch behavior

- Time syncs from NTP over the configured `Maker` Wi-Fi network, using Tunis UTC+1. Wi-Fi then turns off so the MAX4466 on ADC2 GPIO 15 can be sampled. BLE remains available. Wi-Fi comes back briefly for periodic sync and TCP debugging on port 2323; microphone samples pause during these windows. Serial debug is always available at 115200 baud.
- The MAX30102 shows heart rate when a finger is on the sensor. The MPU6050 shows yaw relative to startup, which can drift because it has no compass. Keep the watch still for a second at startup for gyro calibration. Sound is a peak-to-peak ADC value, not calibrated decibels.
- Find Phone uses the Android app's BLE RSSI measurement of the watch. The app writes that RSSI back to the watch, which grows a circle for stronger signal. The phone app must be open and connected. RSSI is an approximate proximity cue, not a distance measurement.
- Pong uses Up and Down for the left paddle; OK returns to the menu. The right paddle follows the ball.

## Android app

The source is in `android_app/`. Open that folder in Android Studio or build `:app:assembleDebug` with JDK 17 and Android SDK 35. The debug APK is `android_app/app/build/outputs/apk/debug/app-debug.apk` after a successful build. Install it on an Android phone, allow Nearby devices and Location, and keep Bluetooth on. The app scans for `Tunis Watch`, remembers its BLE address, reconnects while open, shows live Heart Rate/Yaw/Sound values, and sends measured RSSI to the watch. It does not need the phone to join the watch's Wi-Fi hotspot.

The custom BLE service UUID is `b84c0001-9f5b-47af-8b6e-84768f2a5c01`. The telemetry characteristic is `b84c0002-9f5b-47af-8b6e-84768f2a5c01` (read/notify), and the RSSI write characteristic is `b84c0003-9f5b-47af-8b6e-84768f2a5c01`. The prototype protocol has no BLE authentication; add pairing and access control before using it with private data.

## I2C scanner

`i2c_scanner/i2c_scanner.ino` scans every I2C address on GPIO 13/14 every five seconds and reports over Serial or TCP port 2323. Confirmed circuit addresses: OLED `0x3C`, MAX30102 `0x57`, MPU6050 `0x68`.
