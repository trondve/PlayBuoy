#pragma once
#include <Arduino.h>

bool beginPowerMonitor();
float readBatteryVoltage();
void calibrateBatteryVoltage(float actualVoltage);
// Single quiet ADC sample converted to battery voltage (no logging). Returns NAN if invalid
float readBatteryVoltageSample();
// Enhanced staggered measurement over longer duration; returns calibrated voltage or NAN
float readBatteryVoltageEnhanced(int totalReadings, int delayBetweenReadingsMs, int quickReadsPerGroup, int minValidGroups);
// Expose current calibration factor used for battery voltage scaling
float getBatteryCalibrationFactor();
