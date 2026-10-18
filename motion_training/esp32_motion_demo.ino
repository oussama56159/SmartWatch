// Tiny 3-class motion classifier using an MPU-6050 and GME12864 (SSD1315) over I2C.
#include "motion_model.h"
#include <Wire.h>
#include <U8g2lib.h>

constexpr int SAMPLE_HZ = 25;
constexpr int WINDOW_SAMPLES = 50;
constexpr int AXES = 3;
constexpr int I2C_SDA = 14; // Default AI-Thinker ESP32-CAM wiring; change if needed.
constexpr int I2C_SCL = 15;
constexpr uint8_t MPU_ADDR = 0x68; // AD0 low; use 0x69 if AD0 is high.
constexpr uint8_t MPU_REG_DATA = 0x3B;
constexpr float ACCEL_LSB_PER_G = 16384.0f; // ±2 g range
float samples[AXES][WINDOW_SAMPLES];
U8X8_SSD1315_128X64_NONAME_HW_I2C display(U8X8_PIN_NONE, I2C_SCL, I2C_SDA);
int candidateClass = -1;
int candidateCount = 0;
int shownClass = -1;

void showActivity(uint8_t id, bool changed) {
  display.clearDisplay();
  display.setFont(u8x8_font_chroma48medium8_r);
  display.drawString(0, 0, changed ? "Activity changed" : "Current activity");
  display.drawString(0, 3, motion_labels[id]);
  display.drawString(0, 7, "Motion estimate only");
}

bool writeMpuRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg); Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool beginMpu6050() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x75); // WHO_AM_I
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(MPU_ADDR, (uint8_t)1) != 1) return false;
  if (Wire.read() != 0x68) return false;
  // Wake, enable a 44 Hz low-pass filter, sample at 25 Hz, select ±2 g.
  return writeMpuRegister(0x6B, 0x00) && writeMpuRegister(0x1A, 0x03) &&
         writeMpuRegister(0x19, 39) && writeMpuRegister(0x1C, 0x00);
}

// Return acceleration in g for one sample.
bool readAccelerometerG(float &x, float &y, float &z) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_REG_DATA);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(MPU_ADDR, (uint8_t)6) != 6) return false;
  int16_t rawX = (int16_t)((Wire.read() << 8) | Wire.read());
  int16_t rawY = (int16_t)((Wire.read() << 8) | Wire.read());
  int16_t rawZ = (int16_t)((Wire.read() << 8) | Wire.read());
  x = rawX / ACCEL_LSB_PER_G;
  y = rawY / ACCEL_LSB_PER_G;
  z = rawZ / ACCEL_LSB_PER_G;
  return true;
}

bool computeFeatures(const float data[AXES][WINDOW_SAMPLES], float *f) {
  float mag[WINDOW_SAMPLES], mean[3] = {0,0,0}, var[3] = {0,0,0};
  float magMean = 0;
  for (int i=0; i<WINDOW_SAMPLES; ++i) {
    for (int a=0; a<3; ++a) mean[a] += data[a][i] / WINDOW_SAMPLES;
    mag[i] = sqrtf(data[0][i]*data[0][i] + data[1][i]*data[1][i] + data[2][i]*data[2][i]);
    magMean += mag[i] / WINDOW_SAMPLES;
  }
  for (int a=0; a<3; ++a) {
    for (int i=0; i<WINDOW_SAMPLES; ++i) { float d=data[a][i]-mean[a]; var[a]+=d*d/WINDOW_SAMPLES; }
    f[a] = sqrtf(var[a]);
  }
  float centered[WINDOW_SAMPLES], magVar=0;
  for(int i=0;i<WINDOW_SAMPLES;++i) { centered[i]=mag[i]-magMean; magVar+=centered[i]*centered[i]; }
  f[3]=sqrtf(magVar/WINDOW_SAMPLES); f[4]=magMean;
  int crossings=0;
  for(int i=0;i<WINDOW_SAMPLES-1;++i) if(centered[i]<=0 && centered[i+1]>0) ++crossings;
  f[5]=(float)crossings;
  float denom=magVar+1e-9f, best=-1e9f;
  for(int lag=5;lag<=12;++lag) {
    float sum=0; for(int i=0;i<WINDOW_SAMPLES-lag;++i) sum+=centered[i]*centered[i+lag];
    float acf=sum/denom; if(acf>best) best=acf;
  }
  f[6]=best;
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  if (!beginMpu6050()) {
    Serial.println("MPU-6050 not found. Check power, SDA/SCL, and address.");
    while (true) delay(1000);
  }
  display.setI2CAddress(0x3C * 2); // Common address; change to 0x3D if your module uses it.
  display.begin();
  display.clearDisplay();
  display.setFont(u8x8_font_chroma48medium8_r);
  display.drawString(0, 1, "Motion watch ready");
  display.drawString(0, 3, "Collecting samples");
  Serial.println("Ready; waiting for stable activity estimate.");
}

void loop() {
  static unsigned long nextSample=millis();
  const unsigned long period=1000/SAMPLE_HZ;
  for(int i=0;i<WINDOW_SAMPLES;++i) {
    while((long)(millis()-nextSample)<0) delay(1);
    nextSample += period;
    float x,y,z;
    if(!readAccelerometerG(x,y,z)) { delay(1000); return; }
    samples[0][i]=x; samples[1][i]=y; samples[2][i]=z;
  }
  float features[7]; computeFeatures(samples,features);
  uint8_t id=predictMotion(features);
  if ((int)id == candidateClass) ++candidateCount;
  else { candidateClass = id; candidateCount = 1; }
  if (candidateCount >= 2 && shownClass != candidateClass) {
    bool changed = shownClass >= 0;
    shownClass = candidateClass;
    showActivity(shownClass, changed);
    Serial.print(changed ? "NOTIFICATION: Activity changed to " : "Activity detected: ");
    Serial.println(motion_labels[shownClass]);
  }
}
