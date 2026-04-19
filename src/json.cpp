#include "json.h"
#include <ArduinoJson.h>
#include "rtc_state.h"
#include "config.h"
#include "power.h"
#include "battery.h"
#include "wave.h"
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
  int minutesToSleep,
  uint32_t nextWakeUtc,
  float batteryChangeSinceLast
) {
  StaticJsonDocument<2048> doc;

  // Sanitize float fields: replace NaN/Inf with 0 to prevent invalid JSON
  auto sanitize = [](float x) -> float { return isfinite(x) ? x : 0.0f; };

  doc["nodeId"] = nodeId;
  doc["name"] = name;
  doc["version"] = firmwareVersion;
  // Send timestamp as Unix epoch integer (UTC)
  doc["timestamp"] = timestamp;
  doc["lat"] = sanitize(lat);
  doc["lon"] = sanitize(lon);

  JsonObject wave = doc.createNestedObject("wave");
  wave["height"] = sanitize(waveHeight);
  wave["period"] = sanitize(wavePeriod);
  wave["direction"] = sanitize(waveDirection);
  wave["power"] = sanitize(wavePower);

  // Buoy diagnostics from IMU
  JsonObject buoy = doc.createNestedObject("buoy");
  buoy["tilt"] = sanitize(computeMeanTilt());       // degrees from vertical
  buoy["accel_rms"] = sanitize(computeAccelRms());  // m/s², proxy for conditions

  doc["temp"] = sanitize(waterTemp);
  doc["temp_trend"] = sanitize(getTemperatureTrend()); // °C change over last 5 readings
  doc["battery"] = sanitize(batteryVoltage);
  doc["battery_percent"] = estimateBatteryPercent(batteryVoltage);

  doc["temp_valid"] = !isnan(waterTemp);

  doc["uptime"] = uptime;
  doc["boot_count"] = rtcState.bootCounter;
  doc["reset_reason"] = resetReason;

  // New fields
  doc["minutes_to_sleep"] = minutesToSleep;
  doc["next_wake_utc"] = nextWakeUtc;
  doc["battery_change_since_last"] = batteryChangeSinceLast;

  // RTC snapshot values for visibility (keep waterTemp only)
  JsonObject rtc = doc.createNestedObject("rtc");
  rtc["waterTemp"] = sanitize(rtcWaterTemp);

  // GPS diagnostics
  JsonObject gps = doc.createNestedObject("gps");
  gps["hdop"] = sanitize(rtcState.lastGpsHdop);
  gps["ttf"] = rtcState.lastGpsTtf;

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