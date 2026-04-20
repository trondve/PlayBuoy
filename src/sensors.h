#pragma once
#include <Arduino.h>

//
// Sensor subsystem — water temperature (DS18B20) + IMU (MPU6500 via I2C).
// Temperature sensor uses OneWire protocol on GPIO 13.
// IMU is accessed directly in wave.cpp via I2C (already initialized by beginSensors).
//
// POWER SEQUENCING:
// - Sensors powered via GPIO 25 (3.3V rail control) — held LOW in deep sleep
// - Call powerOn3V3Rail() before beginSensors()
// - DS18B20 requires ~750ms for 12-bit temperature conversion
// - Multiple retries (up to 3) on read failures
//
// TEMPERATURE VALID RANGE:
// - Valid: -30°C to +60°C
// - Invalid: -127°C (sensor disconnected), 85°C (DS18B20 read error)
// - Reject readings outside valid range (not added to history or alerts)
//

//
// Initializes temperature sensor (DS18B20) and I2C bus for IMU.
// Must be called after powerOn3V3Rail() for voltage stability.
// Performs sensor discovery on OneWire bus.
// Returns: true if at least one sensor found, false if I2C or OneWire init failed.
//
bool beginSensors();

//
// Reads water temperature from DS18B20 (12-bit resolution, ~750ms conversion).
// Includes built-in validity check (rejects -127°C and 85°C error codes).
// Returns: temperature in °C, or NaN if sensor read failed or invalid.
// NOTES:
// - First read after sensor power-up may be warm/noisy (sensor warming)
// - Returned value is used for SoC temperature compensation (M-02 fix)
// - Added to temperature history and anomaly detection pipeline
//
float getWaterTemperature();
