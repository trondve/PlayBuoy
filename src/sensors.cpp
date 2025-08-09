#include "sensors.h"
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <MPU9250_asukiaaa.h>

// DS18B20 setup
#define TEMP_SENSOR_PIN 13  // DS18B20 data pin connected to GPIO 13
OneWire oneWire(TEMP_SENSOR_PIN);
DallasTemperature waterTempSensor(&oneWire);

// GY-91 (MPU9250) setup
MPU9250_asukiaaa imu;

bool beginSensors() {
  Serial.println("Initializing sensors...");
  // If you want to use GY-91 IMU, initialize I2C here:
  Wire.begin(21, 22); // SDA = GPIO 21, SCL = GPIO 22
  imu.setWire(&Wire);
  imu.beginAccel();
  imu.beginGyro();
  imu.beginMag();
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
    delay(100);
  }
  // If all retries fail or value is out of range, return NAN
  return NAN;
}

// Calibration data (hard iron offsets)
struct MagCalData {
  float offsetX;
  float offsetY;
  float offsetZ;
  bool valid;
};

// Store in RTC memory for persistence across deep sleep
RTC_DATA_ATTR MagCalData magCal = {0, 0, 0, false};

// Variables for calibration process
static float magMinX, magMinY, magMinZ, magMaxX, magMaxY, magMaxZ;
static bool calibrating = false;

void startMagCalibration() {
  calibrating = true;
  magMinX = magMinY = magMinZ =  1e6;
  magMaxX = magMaxY = magMaxZ = -1e6;
  Serial.println("Magnetometer calibration started. Move the device in all orientations.");
}

void finishMagCalibration() {
  calibrating = false;
  magCal.offsetX = (magMaxX + magMinX) / 2.0f;
  magCal.offsetY = (magMaxY + magMinY) / 2.0f;
  magCal.offsetZ = (magMaxZ + magMinZ) / 2.0f;
  magCal.valid = true;
  saveMagCalibration();
  Serial.println("Magnetometer calibration finished and saved.");
}

bool loadMagCalibration() {
  // RTC_DATA_ATTR persists across deep sleep, so just check validity
  return magCal.valid;
}

bool saveMagCalibration() {
  // RTC_DATA_ATTR is already persistent, so nothing to do
  return true;
}

void applyMagCalibration(float& mx, float& my, float& mz) {
  if (magCal.valid) {
    mx -= magCal.offsetX;
    my -= magCal.offsetY;
    mz -= magCal.offsetZ;
  }
}

// Update calibration during heading read
float getHeadingDegrees() {
  const int maxRetries = 3;
  for (int attempt = 0; attempt < maxRetries; ++attempt) {
    imu.magUpdate();
    float mx = imu.magX();
    float my = imu.magY();
    float mz = imu.magZ();

    // If calibrating, update min/max
    if (calibrating) {
      if (mx < magMinX) magMinX = mx;
      if (mx > magMaxX) magMaxX = mx;
      if (my < magMinY) magMinY = my;
      if (my > magMaxY) magMaxY = my;
      if (mz < magMinZ) magMinZ = mz;
      if (mz > magMaxZ) magMaxZ = mz;
    }

    applyMagCalibration(mx, my, mz);

    float heading = atan2(my, mx) * 180.0f / PI;
    if (!isnan(heading)) {
      if (heading < 0) heading += 360.0f;
      if (heading > 360.0f) heading -= 360.0f;
      return heading;
    }
    delay(100);
  }
  // If all retries fail or value is out of range, return NAN
  return NAN;
}

float getRelativeAltitude() {
  // Use barometric pressure for relative altitude (if BMP280 present)
  imu.accelUpdate();
  imu.gyroUpdate();
  imu.magUpdate();
  // If you have a BMP280 library, use it here. Otherwise, stub:
  // return bmp280.readAltitude(SEALEVELPRESSURE_HPA);
  return 0.0f; // Replace with actual altitude if BMP280 is used
}

float readTideHeight() {
  // Use relative altitude as a proxy for tide/water level
  return getRelativeAltitude();
}