#include <alexia-project-1_inferencing.h>
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

const int MPU = 0x68;

const int LED_RED   = 23;
const int LED_GREEN = 19;
const int BUZZER    = 18;

const float CONFIDENCE_THRESHOLD = 0.55;
const int   CONFIRMATIONS        = 2;

String stablePosture = "...";
String lastSeen      = "";
String lastSent      = "";
int    counter       = 0;

// BLE identifiers 
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *s) { deviceConnected = true; }
  void onDisconnect(BLEServer *s) {
    deviceConnected = false;
    s->getAdvertising()->start();   // allow reconnecting
  }
};

static float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

int getFeatureData(size_t offset, size_t length, float *out_ptr) {
  memcpy(out_ptr, features + offset, length * sizeof(float));
  return 0;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  Wire.begin(21, 22);
  Wire.beginTransmission(MPU);
  Wire.write(0x6B);
  Wire.write(0);              // wake the sensor
  Wire.endTransmission(true);

  // BLE setup
  BLEDevice::init("PostureCoach");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();
  pServer->getAdvertising()->start();

  Serial.println("Ready. Detecting posture...");
}

void loop() {
  // to collect one full window of samples (accel + gyro)
  for (size_t ix = 0; ix < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE; ix += 6) {
    unsigned long t0 = millis();

    Wire.beginTransmission(MPU);
    Wire.write(0x3B);
    Wire.endTransmission(false);
    Wire.requestFrom(MPU, 14, true);

    int16_t ax = Wire.read() << 8 | Wire.read();
    int16_t ay = Wire.read() << 8 | Wire.read();
    int16_t az = Wire.read() << 8 | Wire.read();
    Wire.read(); Wire.read();          // skip temperature
    int16_t gx = Wire.read() << 8 | Wire.read();
    int16_t gy = Wire.read() << 8 | Wire.read();
    int16_t gz = Wire.read() << 8 | Wire.read();

    features[ix + 0] = ax;
    features[ix + 1] = ay;
    features[ix + 2] = az;
    features[ix + 3] = gx;
    features[ix + 4] = gy;
    features[ix + 5] = gz;

    while (millis() < t0 + EI_CLASSIFIER_INTERVAL_MS) { }
  }

  // run the model
  signal_t signal;
  signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
  signal.get_data = &getFeatureData;

  ei_impulse_result_t result = { 0 };
  if (run_classifier(&signal, &result, false) != EI_IMPULSE_OK) {
    Serial.println("Classification failed");
    return;
  }

  // pick the class with the highest confidence
  float maxVal = 0;
  int   maxIx  = 0;
  for (int ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    if (result.classification[ix].value > maxVal) {
      maxVal = result.classification[ix].value;
      maxIx  = ix;
    }
  }
  String candidate = result.classification[maxIx].label;

  // only accept a posture after it holds for a few readings
  if (maxVal >= CONFIDENCE_THRESHOLD) {
    if (candidate == lastSeen) {
      counter++;
    } else {
      lastSeen = candidate;
      counter = 1;
    }
    if (counter >= CONFIRMATIONS) {
      stablePosture = candidate;
    }
  } else {
    counter = 0;
  }

  //feedback (buzzer module is active-low: LOW = on)
  if (stablePosture == "cap_aplecat") {
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(BUZZER, LOW);
  } else if (stablePosture == "cap_drept") {
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED, LOW);
    digitalWrite(BUZZER, HIGH);
  } else {
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(BUZZER, HIGH);
  }

  // send over BLE only when the posture changes
  if (deviceConnected && stablePosture != lastSent) {
    pCharacteristic->setValue(stablePosture.c_str());
    pCharacteristic->notify();
    lastSent = stablePosture;
  }

  Serial.print("raw: ");
  Serial.print(candidate);
  Serial.print(" (");
  Serial.print(maxVal, 2);
  Serial.print(")  stable: ");
  Serial.println(stablePosture);
}