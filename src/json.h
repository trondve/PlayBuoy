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
  uint32_t uptime,           // <-- new
  String resetReason,        // <-- new
  // New: modem/network diagnostics
  String operatorName,
  String apn,
  String ip,
  int signalQuality,
  // Keep only rtcWaterTemp in RTC snapshot
  float rtcWaterTemp,
  // New fields for server
  int hoursToSleep,
  uint32_t nextWakeUtc,
  float batteryChangeSinceLast
);
