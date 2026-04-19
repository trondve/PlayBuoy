#pragma once
#include <Arduino.h>

// Initializes the DS18B20 and I2C bus for GY-91
bool beginSensors();

// Returns temperature in °C from DS18B20
float getWaterTemperature();
