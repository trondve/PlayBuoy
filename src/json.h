#pragma once
#include <Arduino.h>

String buildJsonPayload(
  float lat,
  float lon,
  float waveHeight,
  float wavePeriod,
  String waveDirection,
  float wavePower,
  float waterTemp,
  float batteryVoltage,
  uint32_t timestamp,
  const char* nodeId,
  const char* name,                
  const char* firmwareVersion,
  float heading,
  uint32_t uptime,           // <-- new
  String resetReason         // <-- new
);
