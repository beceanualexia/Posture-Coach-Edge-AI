#include <Wire.h>

const int MPU = 0x68;

const int INTERVAL_MS = 20;      // 50 readings per second
const int DURATION_MS = 20000;   // record for 20 seconds

unsigned long t     = 0;
unsigned long last  = 0;
unsigned long start = 0;
bool done = false;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(21, 22);
  Wire.beginTransmission(MPU);
  Wire.write(0x6B);
  Wire.write(0);              // wake the sensor
  Wire.endTransmission(true);

  // CSV header — one recording per posture, pasted into Edge Impulse
  Serial.println("timestamp,accX,accY,accZ,gyroX,gyroY,gyroZ");
  start = millis();
}

void loop() {
  // Stop after the recording window and print a marker once
  if (millis() - start >= DURATION_MS) {
    if (!done) {
      Serial.println("--- DONE (20 seconds) ---");
      done = true;
    }
    return;
  }

  if (millis() - last >= INTERVAL_MS) {
    last = millis();

    Wire.beginTransmission(MPU);
    Wire.write(0x3B);                  // accelerometer data starts here
    Wire.endTransmission(false);
    Wire.requestFrom(MPU, 14, true);   // 14 bytes: accel(6) + temp(2) + gyro(6)

    int16_t ax = Wire.read() << 8 | Wire.read();
    int16_t ay = Wire.read() << 8 | Wire.read();
    int16_t az = Wire.read() << 8 | Wire.read();
    Wire.read(); Wire.read();          // skip temperature
    int16_t gx = Wire.read() << 8 | Wire.read();
    int16_t gy = Wire.read() << 8 | Wire.read();
    int16_t gz = Wire.read() << 8 | Wire.read();

    Serial.print(t);   Serial.print(',');
    Serial.print(ax);  Serial.print(',');
    Serial.print(ay);  Serial.print(',');
    Serial.print(az);  Serial.print(',');
    Serial.print(gx);  Serial.print(',');
    Serial.print(gy);  Serial.print(',');
    Serial.println(gz);

    t += INTERVAL_MS;
  }
}