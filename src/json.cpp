#include "json.h"
#include <ArduinoJson.h>
#include "rtc_state.h"
#include "config.h"
#include "sensors.h"

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
  uint32_t uptime,
  String resetReason
) {
  StaticJsonDocument<512> doc;

  doc["nodeId"] = nodeId;
  doc["name"] = name;
  doc["version"] = firmwareVersion;
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

  doc["heading"] = heading;
  doc["heading_valid"] = !isnan(heading);

  doc["uptime"] = uptime;
  doc["reset_reason"] = resetReason;

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