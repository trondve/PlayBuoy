#pragma once
#include <Arduino.h>

//
// JSON payload construction for API upload.
// Generates ~700-900 byte JSON document with buoy measurements, diagnostics, alerts.
// Uses ArduinoJson library (StaticJsonDocument<2048>).
// All float fields sanitized against NaN/Inf (M-12 fix).
//

//
// Constructs complete JSON payload for API server.
// Includes: position, wave data, temperature, battery, network info, timestamps, alerts.
// All NaN/Inf floats replaced with 0.0 before serialization (prevents invalid JSON).
// Returns: JSON string ready for HTTP POST.
//
// Payload structure (major fields):
//   nodeId, name, version, timestamp (UTC epoch)
//   lat, lon (WGS84)
//   wave: height, period, direction, power
//   buoy: tilt (degrees from vertical), accel_rms (m/s²)
//   temp, temp_trend, battery, battery_percent, temp_valid
//   uptime, boot_count, reset_reason
//   minutes_to_sleep, next_wake_utc, battery_change_since_last
//   rtc: waterTemp
//   gps: hdop, ttf
//   net: operator, apn, ip, signal
//   alerts: anchorDrift, chargingIssue, tempSpike, overTemp, uploadFailed
//
String buildJsonPayload(
  float lat,                            // Latitude (-90 to +90)
  float lon,                            // Longitude (-180 to +180)
  float waveHeight,                     // Significant wave height (m), capped at WAVE_HS_MAX_M
  float wavePeriod,                     // Peak wave period (seconds)
  String waveDirection,                 // Cardinal direction (N/NE/E/..., "N/A" if unavailable)
  float wavePower,                      // Wave power (kW/m), derived from height and period
  float waterTemp,                      // Water temperature (°C), may be NaN on first read
  float batteryVoltage,                 // Battery voltage (V, 3.5-4.2 typical)
  uint32_t timestamp,                   // UTC epoch timestamp (seconds since 1970)
  const char* nodeId,                   // Buoy identifier (config-defined, e.g. "playbuoy_grinde")
  const char* name,                     // Buoy name (config-defined, e.g. "Litla Grindevatnet")
  const char* firmwareVersion,          // Firmware version string (e.g. "1.0.3")
  uint32_t uptime,                      // Milliseconds since boot (for cycle time estimation)
  String resetReason,                   // Boot cause (PowerOn/Brownout/WDT/SoftReset/etc.)
  // Modem/network diagnostics
  String operatorName,                  // Network operator name if connected (e.g. "Telenor"), empty if not
  String apn,                           // APN used for data connection, empty if not connected
  String ip,                            // Local IP address assigned by network, empty if not connected
  int signalQuality,                    // Signal strength (0-31 RSSI percentage), 0 if no network
  // RTC snapshot
  float rtcWaterTemp,                   // Temperature snapshot from RTC (for trend calculation)
  // Sleep planning
  int minutesToSleep,                   // Next sleep duration (minutes) — determined by season and SoC
  uint32_t nextWakeUtc,                 // Planned next wake time (UTC epoch), adjusted for quiet hours
  float batteryChangeSinceLast           // Battery voltage delta since previous boot (for charge tracking)
);
