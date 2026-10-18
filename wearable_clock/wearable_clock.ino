#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MAX30105.h>
#include <heartRate.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "motion_model.h"
#include "secrets.h"

// Confirmed bus: OLED 0x3C, MAX3010x 0x57, MPU6050 0x68.
constexpr int SDA_PIN = 13;
constexpr int SCL_PIN = 14;
constexpr int SOUND_PIN = 15; // ADC2 works only while Wi-Fi is off.
constexpr int UP_BUTTON_PIN = 2;
constexpr int DOWN_BUTTON_PIN = 4; // Shared with the ESP32-CAM flash LED.
constexpr int OK_BUTTON_PIN = 12;
constexpr uint8_t MPU_ADDRESS = 0x68;
constexpr unsigned long PAGE_MS = 5000;
constexpr unsigned long DEBOUNCE_MS = 35;
constexpr unsigned long MOTION_SAMPLE_MS = 40; // 25 Hz, matching the trained model.
constexpr uint8_t MOTION_WINDOW = 50; // Two seconds of acceleration.
constexpr unsigned long SYNC_RETRY_MS = 2UL * 60UL * 1000UL;
constexpr unsigned long RESYNC_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr char BLE_SERVICE_UUID[] = "b84c0001-9f5b-47af-8b6e-84768f2a5c01";
constexpr char BLE_TELEMETRY_UUID[] = "b84c0002-9f5b-47af-8b6e-84768f2a5c01";
constexpr char BLE_PHONE_RSSI_UUID[] = "b84c0003-9f5b-47af-8b6e-84768f2a5c01";

Adafruit_SSD1306 display(128, 64, &Wire, -1);
MAX30105 pulse;
WiFiServer debugServer(2323);
WiFiClient debugClient;
BLEServer *bleServer = nullptr;
BLECharacteristic *telemetryCharacteristic = nullptr;
volatile bool bleConnected = false;
bool bleWasConnected = false;
unsigned long lastBleTelemetry = 0;
bool oledReady = false, pulseReady = false, mpuReady = false;
bool wifiWindow = false, ntpStarted = false, soundValid = false;
float gyroBiasZ = 0, yawDegrees = 0, bpm = 0;
float motionSamples[3][MOTION_WINDOW];
uint8_t motionSampleCount = 0;
int8_t motionClass = -1, motionCandidate = -1;
uint8_t motionCandidateCount = 0;
unsigned long lastMotionSample = 0;
uint32_t lastIr = 0;
uint16_t soundPeakToPeak = 0;
unsigned long lastBeat = 0, lastMpuRead = 0, lastSoundRead = 0;
unsigned long lastDraw = 0, lastDebug = 0, wifiWindowStarted = 0, lastTimeSync = 0;

enum class Screen : uint8_t { Home, Menu, Sensor, FindTracking, Pong };
Screen screen = Screen::Home;
uint8_t menuChoice = 0; // Pulse, Yaw, Sound, Activity, Find Phone, Pong, Back.
uint8_t selectedSensor = 0;
volatile float phoneRssi = -95;
volatile bool phoneSeen = false;
volatile unsigned long lastPhoneRssi = 0;
float ballX = 64, ballY = 32, ballVx = 2.0f, ballVy = 1.3f;
int paddleY = 26, rivalY = 26, pongScore = 0;
unsigned long lastPongFrame = 0, lastPaddleMove = 0;

void setupBle();
void updateBle();
void updatePong();

class WatchBleServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *) override { bleConnected = true; }
  void onDisconnect(BLEServer *) override {
    bleConnected = false;
    phoneSeen = false;
  }
};

class PhoneRssiCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    const String value = characteristic->getValue();
    const int rssi = value.toInt();
    if (rssi >= -100 && rssi <= -25 && value.startsWith("-")) {
      phoneRssi = phoneSeen ? phoneRssi * 0.7f + rssi * 0.3f : rssi;
      phoneSeen = true;
      lastPhoneRssi = millis();
    }
  }
};

struct Button {
  uint8_t pin;
  bool rawHigh = true;
  bool stableHigh = true;
  unsigned long changedAt = 0;
};
Button upButton{UP_BUTTON_PIN};
Button downButton{DOWN_BUTTON_PIN};
Button okButton{OK_BUTTON_PIN};

bool justPressed(Button &button) {
  const bool high = digitalRead(button.pin) == HIGH;
  if (high != button.rawHigh) {
    button.rawHigh = high;
    button.changedAt = millis();
  }
  if (high != button.stableHigh && millis() - button.changedAt >= DEBOUNCE_MS) {
    button.stableHigh = high;
    return !high; // Button connected between GPIO and GND.
  }
  return false;
}

void updateButtons() {
  const bool up = justPressed(upButton);
  const bool down = justPressed(downButton);
  const bool ok = justPressed(okButton);
  if (screen == Screen::Home) {
    if (up || down || ok) {
      screen = Screen::Menu;
      menuChoice = 0;
    }
  } else if (screen == Screen::Menu) {
    if (up) menuChoice = (menuChoice + 6) % 7;
    if (down) menuChoice = (menuChoice + 1) % 7;
    if (ok) {
      if (menuChoice == 6) screen = Screen::Home;
      else if (menuChoice == 4) {
        screen = Screen::FindTracking;
      } else if (menuChoice == 5) {
        ballX = 64; ballY = 32; ballVx = 2; ballVy = 1.3f;
        paddleY = rivalY = 26; pongScore = 0;
        screen = Screen::Pong;
      } else {
        selectedSensor = menuChoice;
        screen = Screen::Sensor;
      }
    }
  } else if (screen == Screen::Sensor && ok) {
    screen = Screen::Menu;
    menuChoice = selectedSensor;
  } else if (screen == Screen::FindTracking && ok) {
    screen = Screen::Menu;
  } else if (screen == Screen::Pong) {
    if (ok) screen = Screen::Menu;
    if (up) paddleY = max(12, paddleY - 6);
    if (down) paddleY = min(50, paddleY + 6);
    if (!digitalRead(UP_BUTTON_PIN) && millis() - lastPaddleMove >= 70) {
      paddleY = max(12, paddleY - 2);
      lastPaddleMove = millis();
    }
    if (!digitalRead(DOWN_BUTTON_PIN) && millis() - lastPaddleMove >= 70) {
      paddleY = min(50, paddleY + 2);
      lastPaddleMove = millis();
    }
  }
}

bool clockIsSet() { return time(nullptr) > 1700000000; }

bool readMpu(uint8_t reg, uint8_t *data, uint8_t count) {
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDRESS, count) != count) return false;
  for (uint8_t i = 0; i < count; ++i) data[i] = Wire.read();
  return true;
}

bool writeMpu(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readGyroZ(int16_t &raw) {
  uint8_t data[2];
  if (!readMpu(0x47, data, 2)) return false;
  raw = static_cast<int16_t>((data[0] << 8) | data[1]);
  return true;
}

void setupMpu() {
  uint8_t id = 0;
  if (!readMpu(0x75, &id, 1) || id != 0x68 ||
      !writeMpu(0x6B, 0x00) || !writeMpu(0x1B, 0x00)) {
    Serial.printf("MPU6050 missing, ID=0x%02X\n", id);
    return;
  }
  delay(100);
  int32_t sum = 0;
  int count = 0;
  for (int i = 0; i < 100; ++i) {
    int16_t raw;
    if (readGyroZ(raw)) { sum += raw; ++count; }
    delay(5); // Keep the watch still for gyro zero calibration.
  }
  if (count < 80) return;
  gyroBiasZ = static_cast<float>(sum) / count;
  mpuReady = true;
  lastMpuRead = millis();
  Serial.printf("MPU6050 ready, Z bias %.1f\n", gyroBiasZ);
}

void updateMpu() {
  if (!mpuReady || millis() - lastMpuRead < 20) return;
  const unsigned long now = millis();
  const float dt = min((now - lastMpuRead) / 1000.0f, 0.2f);
  lastMpuRead = now;
  int16_t raw;
  if (!readGyroZ(raw)) return;
  float rate = (raw - gyroBiasZ) / 131.0f; // Gyro range +/-250 deg/s.
  if (fabsf(rate) < 0.4f) rate = 0;
  yawDegrees += rate * dt;
  if (yawDegrees > 180) yawDegrees -= 360;
  if (yawDegrees < -180) yawDegrees += 360;
}

bool readAcceleration(float &x, float &y, float &z) {
  uint8_t data[6];
  if (!readMpu(0x3B, data, sizeof(data))) return false;
  x = static_cast<int16_t>((data[0] << 8) | data[1]) / 16384.0f;
  y = static_cast<int16_t>((data[2] << 8) | data[3]) / 16384.0f;
  z = static_cast<int16_t>((data[4] << 8) | data[5]) / 16384.0f;
  return true;
}

// Keep feature order and units identical to the training script.
void computeMotionFeatures(float *f) {
  float magnitude[MOTION_WINDOW], centered[MOTION_WINDOW];
  float mean[3] = {0, 0, 0}, magMean = 0;
  for (uint8_t i = 0; i < MOTION_WINDOW; ++i) {
    for (uint8_t axis = 0; axis < 3; ++axis)
      mean[axis] += motionSamples[axis][i] / MOTION_WINDOW;
    const float x = motionSamples[0][i], y = motionSamples[1][i], z = motionSamples[2][i];
    magnitude[i] = sqrtf(x*x + y*y + z*z);
    magMean += magnitude[i] / MOTION_WINDOW;
  }
  for (uint8_t axis = 0; axis < 3; ++axis) {
    float variance = 0;
    for (uint8_t i = 0; i < MOTION_WINDOW; ++i) {
      const float delta = motionSamples[axis][i] - mean[axis];
      variance += delta * delta / MOTION_WINDOW;
    }
    f[axis] = sqrtf(variance);
  }
  float magVariance = 0;
  for (uint8_t i = 0; i < MOTION_WINDOW; ++i) {
    centered[i] = magnitude[i] - magMean;
    magVariance += centered[i] * centered[i];
  }
  f[3] = sqrtf(magVariance / MOTION_WINDOW);
  f[4] = magMean;
  uint8_t crossings = 0;
  for (uint8_t i = 0; i < MOTION_WINDOW - 1; ++i)
    if (centered[i] <= 0 && centered[i + 1] > 0) ++crossings;
  f[5] = crossings;
  float best = -1e9f;
  const float denominator = magVariance + 1e-9f;
  for (uint8_t lag = 5; lag <= 12; ++lag) {
    float sum = 0;
    for (uint8_t i = 0; i < MOTION_WINDOW - lag; ++i)
      sum += centered[i] * centered[i + lag];
    best = max(best, sum / denominator);
  }
  f[6] = best;
}

void updateMotion() {
  if (!mpuReady || millis() - lastMotionSample < MOTION_SAMPLE_MS) return;
  lastMotionSample += MOTION_SAMPLE_MS;
  if (millis() - lastMotionSample > MOTION_SAMPLE_MS)
    lastMotionSample = millis(); // Do not try to catch up with stale samples.
  float x, y, z;
  if (!readAcceleration(x, y, z)) { motionSampleCount = 0; return; }
  motionSamples[0][motionSampleCount] = x;
  motionSamples[1][motionSampleCount] = y;
  motionSamples[2][motionSampleCount] = z;
  if (++motionSampleCount < MOTION_WINDOW) return;
  motionSampleCount = 0;
  float features[7];
  computeMotionFeatures(features);
  const int8_t prediction = predictMotion(features);
  if (prediction == motionCandidate) ++motionCandidateCount;
  else { motionCandidate = prediction; motionCandidateCount = 1; }
  if (motionCandidateCount >= 2 && motionClass != prediction) {
    motionClass = prediction;
    Serial.printf("Activity: %s\n", motion_labels[motionClass]);
  }
}

void setupPulse() {
  pulseReady = pulse.begin(Wire, I2C_SPEED_STANDARD, 0x57);
  if (!pulseReady) { Serial.println("MAX3010x missing"); return; }
  pulse.setup(0x1F, 4, 2, 100, 411, 4096); // Red + IR at 100 Hz.
  pulse.setPulseAmplitudeRed(0x0A);
  pulse.setPulseAmplitudeIR(0x1F);
  Serial.printf("MAX3010x ready, part ID 0x%02X\n", pulse.readPartID());
}

void updatePulse() {
  if (!pulseReady) return;
  pulse.check();
  while (pulse.available()) {
    lastIr = pulse.getFIFOIR();
    if (lastIr < 50000) { bpm = 0; lastBeat = 0; }
    else if (checkForBeat(lastIr)) {
      const unsigned long now = millis();
      if (lastBeat != 0) {
        const float instant = 60000.0f / (now - lastBeat);
        if (instant >= 35 && instant <= 220)
          bpm = bpm == 0 ? instant : bpm * 0.7f + instant * 0.3f;
      }
      lastBeat = now;
    }
    pulse.nextSample();
  }
  if (lastBeat && millis() - lastBeat > 5000) bpm = 0;
}

void updateSound() {
  if (wifiWindow || millis() - lastSoundRead < 300) return;
  lastSoundRead = millis();
  uint16_t low = 4095, high = 0;
  const unsigned long until = millis() + 50;
  while (millis() < until) {
    const uint16_t sample = analogRead(SOUND_PIN);
    if (sample < low) low = sample;
    if (sample > high) high = sample;
    delayMicroseconds(250);
  }
  soundPeakToPeak = high - low;
  soundValid = true;
}

void beginWifiWindow() {
  wifiWindow = true;
  ntpStarted = false;
  wifiWindowStarted = millis();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connecting Wi-Fi for Tunis time sync...");
}

void endWifiWindow() {
  debugClient.stop();
  debugServer.end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF); // Release ADC2 for GPIO15.
  wifiWindow = false;
  lastTimeSync = millis();
  Serial.println("Wi-Fi off, sound sampling enabled");
}

void setupBle() {
  BLEDevice::init("Tunis Watch");
  BLEDevice::setMTU(185);
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new WatchBleServerCallbacks());
  BLEService *service = bleServer->createService(BLE_SERVICE_UUID);
  telemetryCharacteristic = service->createCharacteristic(
      BLE_TELEMETRY_UUID, BLECharacteristic::PROPERTY_READ |
                              BLECharacteristic::PROPERTY_NOTIFY);
  telemetryCharacteristic->addDescriptor(new BLE2902());
  BLECharacteristic *phoneRssiCharacteristic = service->createCharacteristic(
      BLE_PHONE_RSSI_UUID, BLECharacteristic::PROPERTY_WRITE);
  phoneRssiCharacteristic->setCallbacks(new PhoneRssiCallbacks());
  service->start();
  BLEAdvertising *advertising = bleServer->getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->start();
  Serial.println("BLE Tunis Watch advertising");
}

void updateBle() {
  if (!bleConnected && bleWasConnected) {
    delay(10);
    bleServer->startAdvertising();
    bleWasConnected = false;
  }
  if (bleConnected) bleWasConnected = true;
  if (phoneSeen && millis() - lastPhoneRssi > 5000) phoneSeen = false;
  if (millis() - lastBleTelemetry < 1000) return;
  lastBleTelemetry = millis();
  char payload[96];
  snprintf(payload, sizeof(payload), "B=%.0f;IR=%lu;Y=%.1f;S=%u;P=%d;M=%d;A=%d",
           bpm, static_cast<unsigned long>(lastIr), yawDegrees,
           soundPeakToPeak, pulseReady, mpuReady, soundValid && !wifiWindow);
  telemetryCharacteristic->setValue(payload);
  if (bleConnected) telemetryCharacteristic->notify();
}

void updatePong() {
  if (screen != Screen::Pong || millis() - lastPongFrame < 50) return;
  lastPongFrame = millis();
  ballX += ballVx;
  ballY += ballVy;
  if (ballY < 14 || ballY > 61) ballVy = -ballVy;
  rivalY = constrain(static_cast<int>(ballY) - 6, 12, 50);
  if (ballX <= 9 && ballVx < 0) {
    if (ballY >= paddleY - 2 && ballY <= paddleY + 16) {
      ballVx = -ballVx;
      ++pongScore;
    } else { ballX = 64; ballY = 32; ballVx = 2; pongScore = 0; }
  }
  if (ballX >= 119 && ballVx > 0) ballVx = -ballVx;
}

void updateWifi() {
  if (!wifiWindow) {
    const unsigned long interval = clockIsSet() ? RESYNC_MS : SYNC_RETRY_MS;
    if (millis() - lastTimeSync >= interval) beginWifiWindow();
    return;
  }
  if (WiFi.status() == WL_CONNECTED && !ntpStarted) {
    ntpStarted = true;
    Serial.print("Wi-Fi IP: "); Serial.println(WiFi.localIP());
    configTzTime("CET-1", "pool.ntp.org", "time.google.com");
    debugServer.begin();
    Serial.println("TCP status on port 2323 during sync");
  }
  if ((clockIsSet() && millis() - wifiWindowStarted >= 15000) ||
      millis() - wifiWindowStarted >= 45000) endWifiWindow();
}

void printStatus(Print &out) {
  out.print("time=");
  if (clockIsSet()) {
    time_t now = time(nullptr);
    struct tm tunis;
    localtime_r(&now, &tunis);
    char stamp[24];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tunis);
    out.print(stamp);
  } else out.print("syncing");
  out.printf(" oled=%s pulse=%s ir=%lu bpm=%.1f mpu=%s yaw=%.1f sound_pp=",
             oledReady ? "ok" : "missing", pulseReady ? "ok" : "missing",
             static_cast<unsigned long>(lastIr), bpm,
             mpuReady ? "ok" : "missing", yawDegrees);
  if (soundValid) out.print(soundPeakToPeak); else out.print("waiting");
  out.print(" activity=");
  out.print(!mpuReady ? "no_mpu" : motionClass < 0 ? "sampling" : motion_labels[motionClass]);
  out.print(" ble="); out.print(bleConnected ? "connected" : "waiting");
  out.print(" phone_rssi=");
  if (phoneSeen) out.print(phoneRssi, 0); else out.print("missing");
  out.println();
}

void updateDebug() {
  if (wifiWindow && ntpStarted) {
    WiFiClient incoming = debugServer.available();
    if (incoming) {
      debugClient.stop();
      debugClient = incoming;
      debugClient.println("ESP32-CAM watch; sound starts after Wi-Fi turns off");
      printStatus(debugClient);
    }
    if (debugClient && debugClient.connected() && millis() - lastDebug >= 1000)
      printStatus(debugClient);
  }
  if (millis() - lastDebug >= 1000) {
    printStatus(Serial);
    lastDebug = millis();
  }
}

void drawWatch() {
  if (!oledReady) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(4, 2);
  display.print("TUNIS WATCH");
  display.setCursor(102, 2);
  display.print(bleConnected ? "BT*" : "BT-");
  display.drawLine(3, 12, 124, 12, SSD1306_WHITE);
  if (screen == Screen::Home) {
    display.setCursor(12, 19);
    display.setTextSize(2);
    if (clockIsSet()) {
      time_t now = time(nullptr);
      struct tm tunis;
      localtime_r(&now, &tunis);
      char timeText[9];
      strftime(timeText, sizeof(timeText), "%H:%M:%S", &tunis);
      display.print(timeText);
      display.setTextSize(1);
      display.setCursor(21, 43);
      char dateText[20];
      strftime(dateText, sizeof(dateText), "%a  %d %b %Y", &tunis);
      display.print(dateText);
    } else display.print("SYNC...");
    display.fillRoundRect(3, 53, 122, 11, 3, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(9, 55);
    display.print("ACTIVITY: ");
    display.print(!mpuReady ? "NO MPU" : motionClass < 0 ? "SAMPLING" : motion_labels[motionClass]);
    display.display();
    return;
  }
  if (screen == Screen::Menu) {
    const char *items[] = {"HEART RATE", "YAW", "SOUND", "ACTIVITY", "FIND PHONE", "PONG", "BACK"};
    const uint8_t first = (menuChoice / 3) * 3;
    for (uint8_t row = 0; row < 3; ++row) {
      const uint8_t item = first + row;
      if (item >= 7) break;
      const int y = 17 + row * 15;
      if (item == menuChoice) {
        display.fillRoundRect(3, y, 122, 13, 3, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.drawRoundRect(3, y, 122, 13, 3, SSD1306_WHITE);
        display.setTextColor(SSD1306_WHITE);
      }
      display.setCursor(9, y + 3);
      display.print(items[item]);
    }
    display.setTextColor(SSD1306_WHITE);
    display.display();
    return;
  }
  if (screen == Screen::Pong) {
    display.drawFastVLine(64, 14, 49, SSD1306_WHITE);
    display.fillRect(5, paddleY, 3, 13, SSD1306_WHITE);
    display.fillRect(120, rivalY, 3, 13, SSD1306_WHITE);
    display.fillCircle(static_cast<int>(ballX), static_cast<int>(ballY), 2, SSD1306_WHITE);
    display.setCursor(48, 15);
    display.print(pongScore);
    display.display();
    return;
  }
  if (screen == Screen::FindTracking) {
    display.setCursor(6, 17);
    display.print("FIND PHONE  -  BLE");
    display.setCursor(8, 34);
    if (!bleConnected) display.print("OPEN PHONE APP");
    else if (!phoneSeen) display.print("WAITING RSSI");
    else display.printf("%.0f dBm", phoneRssi);
    if (phoneSeen) {
      const int radius = constrain(static_cast<int>((phoneRssi + 95) * 20 / 55) + 3, 3, 22);
      display.fillCircle(99, 40, radius, SSD1306_WHITE);
    } else display.drawCircle(99, 40, 4, SSD1306_WHITE);
    display.setCursor(8, 55);
    display.print("OK  BACK");
    display.display();
    return;
  }
  const char *titles[] = {"HEART RATE", "YAW  /  MPU6050", "SOUND  /  MAX4466", "ACTIVITY  /  MPU6050"};
  display.setCursor(6, 17);
  display.print(titles[selectedSensor]);
  display.setTextSize(2);
  display.setCursor(9, 30);
  if (selectedSensor == 0) {
    if (!pulseReady) display.print("NO SENSOR");
    else if (lastIr < 50000) display.print("NO FINGER");
    else if (bpm > 0) display.printf("%.0f BPM", bpm);
    else display.print("WAIT...");
  } else if (selectedSensor == 1) {
    if (mpuReady) display.printf("%+.1f DEG", yawDegrees);
    else display.print("NO SENSOR");
  } else if (selectedSensor == 2) {
    if (wifiWindow) display.print("SYNC...");
    else if (soundValid) display.printf("%u", soundPeakToPeak);
    else display.print("WAIT...");
  } else {
    if (!mpuReady) display.print("NO MPU");
    else if (motionClass < 0) display.print("SAMPLING");
    else display.print(motion_labels[motionClass]);
  }
  display.setTextSize(1);
  display.drawLine(3, 51, 124, 51, SSD1306_WHITE);
  display.setCursor(6, 55);
  display.print("OK  BACK TO MENU");
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("ESP32-CAM multisensor watch starting");
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);
  Wire.setTimeOut(20);
  oledReady = display.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);
  Serial.printf("OLED 0x3C: %s\n", oledReady ? "ready" : "missing");
  setupMpu();
  lastMotionSample = millis();
  setupPulse();
  analogReadResolution(12);
  analogSetPinAttenuation(SOUND_PIN, ADC_11db);
  pinMode(UP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DOWN_BUTTON_PIN, INPUT_PULLUP);
  pinMode(OK_BUTTON_PIN, INPUT_PULLUP);
  upButton.rawHigh = upButton.stableHigh = digitalRead(UP_BUTTON_PIN) == HIGH;
  downButton.rawHigh = downButton.stableHigh = digitalRead(DOWN_BUTTON_PIN) == HIGH;
  okButton.rawHigh = okButton.stableHigh = digitalRead(OK_BUTTON_PIN) == HIGH;
  drawWatch();
  setupBle();
  beginWifiWindow();
}

void loop() {
  updateWifi();
  updateBle();
  updatePong();
  updateMpu();
  updateMotion();
  updatePulse();
  updateSound();
  updateButtons();
  updateDebug();
  if (millis() - lastDraw >= 250) { drawWatch(); lastDraw = millis(); }
  delay(2);
}
