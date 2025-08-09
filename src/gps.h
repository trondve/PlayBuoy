#pragma once

#include <Arduino.h>

// Holds a GPS fix result
typedef struct {
  bool success;
  float latitude;
  float longitude;
  float accuracy;
  uint32_t fixTimeEpoch;  // GPS-provided time (optional, if supported)
} GpsFixResult;

void gpsBegin();                         // Enables GPS module
GpsFixResult getGpsFix(uint16_t timeoutSec = 600);  // Attempts fix within timeout
void gpsEnd();                           // Optional: powers down GPS to save power
