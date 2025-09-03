#include "json.h"
#include <ArduinoJson.h>
#include "rtc_state.h"
#include "config.h"
#include "sensors.h"
#include "power.h"
#include <time.h>

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
  uint32_t uptime,
  String resetReason,
  String operatorName,
  String apn,
  String ip,
  int signalQuality,
  float rtcWaterTemp,
  int hoursToSleep,
  uint32_t nextWakeUtc,
  float batteryChangeSinceLast
) {
  StaticJsonDocument<1024> doc;

  doc["nodeId"] = nodeId;
  doc["name"] = name;
  doc["version"] = firmwareVersion;
  // Send timestamp as Unix epoch integer (UTC)
  doc["timestamp"] = timestamp;
  doc["lat"] = lat;
  doc["lon"] = lon;

  JsonObject wave = doc.createNestedObject("wave");
  wave["height"] = waveHeight;
  wave["period"] = wavePeriod;
  wave["direction"] = waveDirection;
  wave["power"] = wavePower;

  doc["temp"] = waterTemp;
  doc["battery"] = batteryVoltage;
  // Calibration factor removed; using direct measured value

  // Flag invalid temperature
  if (isnan(waterTemp)) {
    doc["temp_valid"] = false;
  } else {
    doc["temp_valid"] = true;
  }

  float tideHeight = readTideHeight();
  if (!isnan(tideHeight)) {
    JsonObject tide = doc.createNestedObject("tide");
    tide["current_height"] = tideHeight;
  }

  // Removed heading fields

  doc["uptime"] = uptime;
  doc["reset_reason"] = resetReason;

  // New fields
  doc["hours_to_sleep"] = hoursToSleep;
  doc["next_wake_utc"] = nextWakeUtc;
  doc["battery_change_since_last"] = batteryChangeSinceLast;

  // RTC snapshot values for visibility (keep waterTemp only)
  JsonObject rtc = doc.createNestedObject("rtc");
  rtc["waterTemp"] = rtcWaterTemp;

  // Modem/network diagnostics
  JsonObject net = doc.createNestedObject("net");
  net["operator"] = operatorName;
  net["apn"] = apn;
  net["ip"] = ip;
  net["signal"] = signalQuality;

  JsonObject alerts = doc.createNestedObject("alerts");
  alerts["anchorDrift"]     = rtcState.anchorDriftDetected;
  alerts["chargingIssue"]   = rtcState.chargingProblemDetected;
  alerts["tempSpike"]       = rtcState.tempSpikeDetected;
  alerts["overTemp"]        = rtcState.overTempDetected;
  alerts["uploadFailed"]    = rtcState.lastUploadFailed;

  String output;
  serializeJson(doc, output);
  return output;
}