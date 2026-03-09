#pragma once

#include <Arduino.h>

void recordWaveData();

float computeWaveHeight();
float computeWavePeriod();
String computeWaveDirection();
float computeWavePower(float height, float period);
float computeMeanTilt();      // Mean buoy tilt angle (degrees from vertical)
float computeAccelRms();      // Acceleration RMS (m/s², proxy for wave energy)
void logWaveStats();
