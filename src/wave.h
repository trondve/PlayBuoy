#pragma once

#include <Arduino.h>

void recordWaveData();

float computeWaveHeight();
float computeWavePeriod();
String computeWaveDirection();
float computeWavePower(float height, float period);
void logWaveStats();
