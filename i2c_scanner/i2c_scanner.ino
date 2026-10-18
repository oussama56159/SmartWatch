#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>

#include "secrets.h"

// All I2C sensors share the confirmed bus: GPIO13 SDA and GPIO14 SCL.
struct BusResult {
  int sda;
  int scl;
  uint8_t addresses[112];
  uint8_t addressCount;
  uint16_t busErrors;
  bool initialized;
  bool sdaHigh;
  bool sclHigh;
};

BusResult buses[] = {
  {13, 14, {}, 0, 0, false, false, false},
};
constexpr uint16_t TCP_PORT = 2323;
constexpr unsigned long SCAN_INTERVAL_MS = 5000;

WiFiServer statusServer(TCP_PORT);
WiFiClient statusClient;
unsigned long scanNumber = 0;
unsigned long lastScan = 0;
unsigned long lastWifiAttempt = 0;
bool wifiWasConnected = false;
bool serverStarted = false;
bool wireStarted = false;

void printScan(Print &out, const BusResult &bus) {
  out.print("scan=");
  out.print(scanNumber);
  out.print(" sda_gpio=");
  out.print(bus.sda);
  out.print(" scl_gpio=");
  out.print(bus.scl);
  out.print(" i2c=");
  if (bus.addressCount == 0) {
    out.print("none");
  } else {
    for (uint8_t index = 0; index < bus.addressCount; ++index) {
      if (index != 0) out.print(',');
      char hexAddress[5];
      snprintf(hexAddress, sizeof(hexAddress), "0x%02X", bus.addresses[index]);
      out.print(hexAddress);
    }
  }
  out.print(" sda=");
  out.print(bus.sdaHigh ? "HIGH" : "LOW");
  out.print(" scl=");
  out.print(bus.sclHigh ? "HIGH" : "LOW");
  out.print(" bus_errors=");
  out.print(bus.busErrors);
  out.print(" init=");
  out.println(bus.initialized ? "ok" : "failed");
}

void scanBus(BusResult &bus) {
  if (!wireStarted) {
    bus.initialized = Wire.begin(bus.sda, bus.scl);
    wireStarted = bus.initialized;
    if (wireStarted) {
      Wire.setClock(100000);
      Wire.setTimeOut(20);
    }
  }
  bus.addressCount = 0;
  bus.busErrors = 0;
  if (!bus.initialized) return;
  for (uint8_t address = 0x08; address <= 0x77; ++address) {
    Wire.beginTransmission(address);
    uint8_t result = Wire.endTransmission();
    if (result == 0) {
      bus.addresses[bus.addressCount++] = address;
    } else if (result != 2) { // 2 is the normal "address not acknowledged" result.
      ++bus.busErrors;
    }
  }
  bus.sdaHigh = digitalRead(bus.sda) == HIGH;
  bus.sclHigh = digitalRead(bus.scl) == HIGH;
}

void scanAllBuses() {
  ++scanNumber;
  for (BusResult &bus : buses) {
    scanBus(bus);
    printScan(Serial, bus);
    if (statusClient && statusClient.connected()) printScan(statusClient, bus);
  }
  lastScan = millis();
}

void updateWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      Serial.print("Wi-Fi IP: ");
      Serial.println(WiFi.localIP());
      if (!serverStarted) {
        statusServer.begin();
        serverStarted = true;
      }
      Serial.println("TCP scan stream on port 2323");
    }
  } else {
    if (wifiWasConnected) {
      wifiWasConnected = false;
      statusClient.stop();
      Serial.println("Wi-Fi disconnected");
    }
    if (millis() - lastWifiAttempt >= 45000) {
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      lastWifiAttempt = millis();
      Serial.println("Connecting to Wi-Fi...");
    }
  }
}

void updateTcp() {
  if (!wifiWasConnected) return;
  WiFiClient incoming = statusServer.available();
  if (incoming) {
    statusClient.stop();
    statusClient = incoming;
    statusClient.println("ESP32-CAM I2C scanner: GPIO13 SDA, GPIO14 SCL; all addresses");
    for (const BusResult &bus : buses) printScan(statusClient, bus);
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("ESP32-CAM general I2C scanner");
  scanAllBuses();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttempt = millis();
  Serial.println("Connecting to Wi-Fi...");
}

void loop() {
  updateWifi();
  updateTcp();
  if (millis() - lastScan >= SCAN_INTERVAL_MS) scanAllBuses();
  delay(10);
}
