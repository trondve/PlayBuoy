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
  float heading,
  uint32_t uptime,
  String resetReason,
  String operatorName,
  String apn,
  String ip,
  int signalQuality,
  float rtcBatteryVoltage,
  float rtcWaterTemp
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
  // Include pre-calibrated (raw) voltage and calibration factor
  {
    float f = getBatteryCalibrationFactor();
    float preCal = batteryVoltage;
    if (f > 1e-6f) preCal = batteryVoltage / f;
    doc["battery_precal"] = preCal;
    doc["battery_cal_factor"] = f;
  }

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

  // RTC snapshot values for visibility
  JsonObject rtc = doc.createNestedObject("rtc");
  rtc["batteryVoltage"] = rtcBatteryVoltage;
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