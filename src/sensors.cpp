#include "sensors.h"
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
// IMU handled directly by wave.cpp (Mahony + direct I2C). Do not init via library here to avoid conflicts.

// DS18B20 setup
#define TEMP_SENSOR_PIN 13  // DS18B20 data pin connected to GPIO 13
OneWire oneWire(TEMP_SENSOR_PIN);
DallasTemperature waterTempSensor(&oneWire);

// IMU handled in wave.cpp; no global IMU object here

bool beginSensors() {
  Serial.println("Initializing sensors...");
  // If you want to use GY-91 IMU, initialize I2C here:
  Wire.begin(21, 22); // SDA = GPIO 21, SCL = GPIO 22
  // IMU initialization moved to wave.cpp to match working wave pipeline
  waterTempSensor.begin();
  Serial.println("Sensors initialized");
  return true;
}

float getWaterTemperature() {
  const int maxRetries = 3;
  for (int attempt = 0; attempt < maxRetries; ++attempt) {
    waterTempSensor.requestTemperatures();
    float temp = waterTempSensor.getTempCByIndex(0);
    // Validate DS18B20 reading: -127°C and 85°C are error codes
    if (temp != -127.0f && temp != 85.0f && !isnan(temp) && temp > -30.0f && temp < 60.0f) {
      return temp;
    }
    delay(800); // Must cover full 12-bit conversion time (750ms)
  }
  // If all retries fail or value is out of range, return NAN
  return NAN;
}

// Heading not provided in the new wave pipeline (no magnetometer). Return NAN.
float getHeadingDegrees() { return NAN; }