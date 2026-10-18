# ESP32-CAM activity model

This project trains a small decision tree for three motion labels—Walking, Running, and Resting—and exports it as a C header for an ESP32-CAM sketch. The generated model has 21 nodes and uses 7 numeric features from short accelerometer windows. It needs no TensorFlow or neural-network runtime on the board.

## Hardware needed

Your listed parts do not include an accelerometer. The MAX30102 measures pulse and estimated oxygen saturation; it does not measure motion. To classify walking, running, and resting, add a 3-axis accelerometer such as an MPU-6050. This example uses that sensor.

The display code is now configured for the common four-pin GME12864 128×64 I2C OLED variant with an SSD1315 controller, using U8g2's U8x8 text driver at address `0x3C`. Some modules use `0x3D` or a different controller/interface; verify the label/listing if this setup does not match your board. The USB camera, BME/BMP280, MAX4466, and MAX30102 are not used by this activity demo.

## Example wiring for a common AI-Thinker ESP32-CAM

The MPU-6050 and GME12864 share I2C: SDA to GPIO 14, SCL to GPIO 15, and common ground. Connect both modules to 3.3 V only if their breakout boards support 3.3 V logic and power. MPU-6050 AD0 to GND selects address `0x68`. The display is commonly `0x3C` (some use `0x3D`). These pins are shared with the microSD interface, so the sketch cannot use the SD card at the same time. Verify your exact board pinout before wiring.

## Run and install

1. On a computer with Python, install NumPy and scikit-learn, then run `python train_motion_model.py` in this folder. This regenerates `synthetic_accelerometer.csv`, `training_report.md`, and `motion_model.h`.
2. Install the Arduino ESP32 board package and the **U8g2** library.
3. Open `esp32_motion_demo.ino`, select your ESP32-CAM board and port, and upload. The sketch reads 50 samples at 25 Hz (a 2-second window), then updates the display only after the same class is predicted for two consecutive windows. It also prints `NOTIFICATION: Activity changed to ...` over Serial.

The generated CSV contains 3,000 labeled simulated accelerometer windows and a simulated heart-rate column for reference. Heart rate is not used as a model input. The MAX30102 is not connected in this demo; activity is inferred from acceleration, which is the relevant signal for these labels.

## Replace synthetic data before relying on it

The included recordings are generated examples, not real measurements. The reported holdout score only describes how well the tree separates data from the same simulator. It does not establish real-world accuracy. For a useful watch, record labeled windows from the actual wrist-mounted accelerometer across people, sessions, wrist positions, and speeds; retrain and evaluate on people/sessions excluded from training. Keep sampling at 25 Hz, acceleration in g, and the feature ordering in `train_motion_model.py` and the sketch identical.

This activity demo is not a medical monitor. Do not use its output or MAX30102 readings for diagnosis or emergency decisions.
