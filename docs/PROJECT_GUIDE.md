# Tunis Watch: complete project guide

## 1. What this project does

Tunis Watch is a prototype smartwatch built around an AI-Thinker style ESP32-CAM. It shows Tunis time on a 128 x 64 I2C OLED, reads a MAX3010x pulse sensor, an MPU6050 motion sensor, and a MAX4466 microphone module, and uses three buttons for a menu and a small Pong game. A companion Android app connects over Bluetooth Low Energy (BLE) to display live readings and help locate the phone using BLE signal strength.

The firmware also runs a small decision-tree classifier on MPU6050 acceleration. It labels the current motion as **Walking**, **Running**, or **Resting** on the home screen and Activity page. “Resting” includes standing still and sitting; this model cannot tell those apart. The activity model is experimental because its training data is simulated.

The watch uses Wi-Fi briefly to set its clock by NTP. BLE stays available for the Android app. The microphone is connected to ADC2, so the firmware pauses microphone measurements while Wi-Fi is on, then switches Wi-Fi off. Wi-Fi also provides a short TCP debug window on port 2323 during time synchronization. Serial debugging at 115200 baud is always available.

## 2. Package contents

| Path | Purpose |
| --- | --- |
| `wearable_clock/wearable_clock.ino` | Main firmware currently flashed to the ESP32-CAM |
| `wearable_clock/motion_model.h` | Exported 21-node motion decision tree used by the firmware |
| `wearable_clock/secrets.h` | Wi-Fi configuration; set your own SSID and password |
| `android_app/` | Android Studio / Gradle app source and wrapper |
| `TunisWatch-debug.apk` | Prebuilt Android debug app |
| `i2c_scanner/` | Separate diagnostic firmware for I2C detection |
| `motion_training/` | Python training source, synthetic data, report, and original demonstration |
| `docs/PROJECT_GUIDE.md` | This guide |

Only one ESP32 sketch can run at a time. Flashing the I2C scanner replaces the watch firmware; flash `wearable_clock` again to restore the watch.

## 3. Hardware and wiring

All I2C devices share the same SDA and SCL lines and must share ground with the ESP32-CAM. The addresses below were detected in this project's circuit.

| Part / connection | ESP32-CAM connection | I2C address |
| --- | --- | --- |
| OLED SDA, MAX3010x SDA, MPU6050 SDA | GPIO 13 | OLED `0x3C`; MAX3010x `0x57`; MPU6050 `0x68` |
| OLED SCL, MAX3010x SCL, MPU6050 SCL | GPIO 14 | Shared bus at 100 kHz |
| MAX4466 analog output | GPIO 15 | Not I2C |
| Up button | GPIO 2 to GND when pressed | Not I2C |
| Down button | GPIO 4 to GND when pressed | Not I2C |
| OK / Back button | GPIO 12 to GND when pressed | Not I2C |

Use a common ground. Power each module according to its own voltage rating and ensure the voltage on every ESP32 GPIO stays within the ESP32's 3.3 V logic range. GPIO 4 is also connected to the ESP32-CAM flash LED. GPIO 12 is a boot strapping pin; do not externally pull it HIGH at boot. The microSD interface is unused because some of its pins serve the watch circuit. The camera is not used by this firmware.

The main watch sketch uses Adafruit SSD1306 at I2C address `0x3C`. The separate `motion_training/esp32_motion_demo.ino` is a reference sketch from the motion project and has different I2C pins and display driver; the integrated `wearable_clock` sketch is the one to flash for this wiring.

## 4. Watch controls and screens

The home screen shows Tunis time, date, BLE connection state (`BT*` when connected), and the current activity label. At startup it can say `SYNC...` while setting the clock and `SAMPLING` while collecting motion readings. It says `NO MPU` if the MPU6050 was not found at startup.

Press any button on Home to open the menu. Up and Down move the selection; OK opens it. The menu contains Heart Rate, Yaw, Sound, Activity, Find Phone, Pong, and Back. OK on a sensor page or Find Phone returns to the menu. In Pong, Up and Down move the left paddle and OK returns to the menu. Select Back to return Home.

Heart Rate needs a finger on the MAX3010x. The reading is an estimate in beats per minute, not a medical measurement. Yaw integrates the MPU6050 gyro and is relative to startup; it drifts and is not a compass heading. Keep the watch still during the first second after power-up while it calibrates the gyro. Sound is a raw peak-to-peak ADC value, not decibels. Activity displays Walking, Running, or Resting after two matching two-second motion windows, normally about four seconds after sampling begins.

Find Phone requires the Android app to be open and connected. The phone measures its BLE link signal strength to the watch about once a second and writes the RSSI value back. The watch displays the value in dBm and a bubble that grows as the received signal gets stronger. RSSI changes with obstacles, body position, and radio conditions; it is only an approximate proximity cue.

## 5. Android app: install and use

The prebuilt `TunisWatch-debug.apk` targets Android and can be copied to the phone by USB or another file transfer. Open the APK on the phone and allow installation from that source if Android asks. Turn on Bluetooth and open **Tunis Watch**. Grant **Nearby devices** and **Location** when prompted; the app needs these permissions for BLE scanning on supported Android versions.

The app scans for the BLE device named `Tunis Watch`, connects automatically, remembers its BLE address, and reconnects while the app is open. If needed, tap **SCAN / RECONNECT**. The connection label should say `Connected to Tunis Watch`, and the watch header should show `BT*`. There is no need to pair from Android's Bluetooth settings or connect the phone to the watch's Wi-Fi network.

The app shows heart rate, relative yaw, raw sound level, and BLE RSSI with a bubble. It currently **does not show the motion activity label**; activity is on the OLED and serial/TCP status only. Find Phone requires the app to stay open and connected.

The app's BLE service UUID is `b84c0001-9f5b-47af-8b6e-84768f2a5c01`. It receives a text telemetry notification from characteristic `b84c0002-9f5b-47af-8b6e-84768f2a5c01` and writes phone RSSI to characteristic `b84c0003-9f5b-47af-8b6e-84768f2a5c01`. Telemetry is a semicolon-separated line with keys `B` (BPM), `IR` (infrared pulse value), `Y` (yaw), `S` (sound peak-to-peak), `P` (pulse sensor present), `M` (MPU present), and `A` (sound valid). This prototype has no BLE authentication.

## 6. Build and flash the ESP32 firmware

Install the Arduino ESP32 board package and these Arduino libraries: **Adafruit GFX Library**, **Adafruit SSD1306**, and **SparkFun MAX3010x Pulse and Proximity Sensor Library**. The firmware also uses ESP32 core libraries for Wire, Wi-Fi, BLE, and time. The last verified build used ESP32 Arduino core **3.3.10** and board target `esp32:esp32:esp32cam`. It used about 1.76 MB of the 3 MB application partition and about 66 KB of static RAM.

Edit `wearable_clock/secrets.h` with the SSID and password of the Wi-Fi network the watch should join for time sync. The package uses placeholders so it can be shared without the current hotspot password. Then connect the ESP32-CAM to its USB programming shield, select the ESP32-CAM board and the detected serial port, and upload `wearable_clock/wearable_clock.ino`. With Arduino CLI, from the package root on Windows:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32cam wearable_clock
arduino-cli upload -p COM5 --fqbn esp32:esp32:esp32cam wearable_clock
```

Replace `COM5` with the port shown on your computer. The USB shield handles programming on this setup. After upload, open the serial monitor at **115200 baud**. Once the board is moved to the sensor circuit, reboot it so it detects the sensors during setup.

On the last verified USB-shield run, firmware booted, BLE advertising started, Wi-Fi connected, and Tunis time synchronized. The serial log reported the MPU6050 and pulse sensor missing while they were not on the shield; activity classification was therefore not verified on a worn watch.

## 7. How the firmware works

At boot, the watch initializes the shared I2C bus on GPIO 13/14, probes the OLED, MPU6050, and MAX3010x, sets up the buttons and BLE service, and starts a Wi-Fi window for NTP. The main loop updates the clock connection, BLE notifications, game, gyro yaw, accelerometer activity, pulse estimate, microphone, buttons, debug status, and screen. The display redraws every 250 ms.

The clock uses a fixed Tunis UTC+1 setting (`CET-1`) and NTP servers. Wi-Fi turns off after a successful time sync window of at least 15 seconds, or after 45 seconds if time cannot be obtained. It tries again in two minutes while time is unset and every six hours after a successful sync. The microphone on ADC2 GPIO 15 is only sampled while Wi-Fi is off. The watch's TCP server on port 2323 is available only during the brief Wi-Fi windows; the IP address comes from the current hotspot and can change. The serial monitor is the reliable continuous debug channel.

The motion code samples three MPU6050 acceleration axes at a target 25 Hz, collects 50 samples (two seconds), computes seven features, and passes them through a 21-node decision tree. It changes the visible activity only after two consecutive windows agree. The features are standard deviations of X/Y/Z, acceleration magnitude standard deviation and mean, positive mean crossings, and the strongest autocorrelation over lags 5 through 12. Acceleration is in units of g. Occasional sample timing jitter can occur because the current microphone routine samples in a short blocking burst.

The supplied `motion_training/` project contains `train_motion_model.py`, its generated C header, a report, 3,000 simulated labeled windows, and a standalone reference sketch. Its reported 97.9% holdout accuracy measures separation of data from the same simulator; it is **not** measured accuracy on real people. For a credible wearable result, collect labeled wrist data at 25 Hz from the real hardware across people, sessions, placements, and walking/running speeds; keep feature calculation and units identical, and evaluate on people or sessions excluded from training. Retraining generates a new `motion_model.h`; copy it into `wearable_clock/` before rebuilding.

## 8. Build the Android source

Open `android_app/` in Android Studio with a suitable Android SDK, or use JDK 17 and Android SDK 35 with the included Gradle wrapper. From `android_app/` on Windows:

```powershell
.\gradlew.bat :app:assembleDebug
```

The generated file is `android_app/app/build/outputs/apk/debug/app-debug.apk`. The included `TunisWatch-debug.apk` is the prebuilt debug package. The Android project uses Android Gradle Plugin 8.6.1, Gradle 8.13, minimum SDK 26, and target SDK 34. It is a local prototype; it does not need a cloud account or backend.

## 9. I2C diagnostic scanner

If the OLED or a sensor is absent, flash `i2c_scanner/i2c_scanner.ino` temporarily. Set its `secrets.h` Wi-Fi credentials if you want TCP output. The scanner probes all normal 7-bit addresses from `0x08` through `0x77` on GPIO 13 SDA / GPIO 14 SCL every five seconds and prints the complete found-address list, line states, and bus error count to serial at 115200 baud. While connected to Wi-Fi, it streams the same scan to TCP port 2323. Expected addresses in this circuit are `0x3C`, `0x57`, and `0x68`. The MAX4466 is analog and cannot appear in an I2C scan. Flash `wearable_clock` again when finished.

## 10. Troubleshooting

| Symptom | Check |
| --- | --- |
| OLED remains blank | Confirm VCC, GND, GPIO 13 SDA, GPIO 14 SCL, and address `0x3C`; use the I2C scanner. Some OLED modules use a different controller or address. |
| `NO MPU` or no activity | Confirm MPU6050 power, common GND, bus wiring, and address `0x68`. Reboot the watch after reconnecting it. |
| Heart rate says `NO SENSOR` / `NO FINGER` | Check address `0x57`, wiring, then place a finger over the MAX3010x. |
| Time stays `SYNC...` | Confirm `secrets.h`, that the ESP32 can join the hotspot, and that NTP is reachable. Watch the 115200-baud serial log. |
| Sound says `SYNC...` | Wi-Fi is currently on for time sync; sound resumes when Wi-Fi turns off. |
| Phone app cannot find watch | Keep the watch powered, turn on Bluetooth, grant Nearby devices and Location, and tap SCAN / RECONNECT. Look for `BLE Tunis Watch advertising` in serial output. |
| Find Phone says `WAITING RSSI` | Keep the Android app open and connected. Wait a few seconds for the first RSSI read. |
| Upload fails | Confirm the USB shield and COM port, close any serial monitor using that port, and retry. |

## 11. Present limitations

This is an experimental watch. Activity classification is trained on simulated acceleration, and Resting cannot distinguish standing from sitting. Pulse and sound readings are uncalibrated for medical or acoustic use. Yaw drifts over time. BLE has no authentication. The phone app stays connected while open and does not display the activity label. Sensor operation after the last firmware upload still needs an end-to-end check in the complete wearable circuit.
