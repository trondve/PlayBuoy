#pragma once
#include <Arduino.h>

// Initializes the DS18B20 and GY‑91 IMU (MPU-9250)
bool beginSensors();

// Returns temperature in °C from DS18B20
float getWaterTemperature();

// Returns heading in degrees from GY-91 (magnetometer)
float getHeadingDegrees();

// Returns relative altitude from GY-91 (barometer)
float getRelativeAltitude();

// Returns tide/water level (proxy: relative altitude)
float readTideHeight();

// Magnetometer calibration routines
void startMagCalibration();
void finishMagCalibration();
bool loadMagCalibration();
bool saveMagCalibration();
void applyMagCalibration(float& mx, float& my, float& mz);
