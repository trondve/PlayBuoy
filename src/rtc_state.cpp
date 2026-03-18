#include "rtc_state.h"
#include <Arduino.h>
#include <Preferences.h>

#define SerialMon Serial

// Define the persistent RTC state variable in RTC fast memory
RTC_DATA_ATTR rtc_state_t rtcState = {
  .bootCounter = 0,
  .lastBatteryVoltage = 0.0f,
  .lastGpsLat = 0.0f,
  .lastGpsLon = 0.0f,
  .lastGpsFixTime = 0,
  .lastGpsHdop = 99.0f,
  .lastGpsTtf = 0,
  .lastWaterTemp = 0.0f,
  .tempHistory = {NAN, NAN, NAN, NAN, NAN},
  .tempHistoryCount = 0,
  .tempSpikeDetected = false,
  .overTempDetected = false,
  .lastUploadFailed = false,
  .anchorDriftDetected = false,
  .anchorDriftCounter = 0,
  .chargingProblemDetected = false,
  .firmwareUpdateAttempted = false,
  .lastUnsentJson = {0},
  .hasUnsentData = false,
  .lastSleepMinutes = 0,
  .lastNextWakeUtc = 0,

};

// Haversine formula to calculate distance in meters between two lat/lon points
static float distanceBetween(float lat1, float lon1, float lat2, float lon2) {
  const float R = 6371000; // Earth radius in meters
  float dLat = radians(lat2 - lat1);
  float dLon = radians(lon2 - lon1);

  float a = sin(dLat / 2) * sin(dLat / 2) +
            cos(radians(lat1)) * cos(radians(lat2)) *
            sin(dLon / 2) * sin(dLon / 2);
  float c = 2 * atan2(sqrt(a), sqrt(1 - a));
  float distance = R * c;

  return distance;
}

#define ANCHOR_DRIFT_DISTANCE_THRESHOLD 50.0f

void rtcStateBegin() {
  // Restore state from NVS if this boot follows a hard reset (OTA, brownout).
  // Must happen before incrementing bootCounter so the saved count is correct.
  restoreStateFromNvs();

  rtcState.bootCounter++;

  // Initialize counters on first boot
  if (rtcState.bootCounter == 1) {
    rtcState.anchorDriftCounter = 0;
    rtcState.anchorDriftDetected = false;
  }
}

void logRtcState() {
  SerialMon.println("RTC State:");
  SerialMon.printf("- Boot count: %lu\n", rtcState.bootCounter);
  SerialMon.printf("- Battery voltage: %.2f V\n", rtcState.lastBatteryVoltage);
  SerialMon.printf("- Last GPS fix: %.6f, %.6f\n", rtcState.lastGpsLat, rtcState.lastGpsLon);
  SerialMon.printf("- Last GPS fix time: %lu\n", rtcState.lastGpsFixTime);
  SerialMon.printf("- Last water temp: %.2f C\n", rtcState.lastWaterTemp);
  SerialMon.printf("- Anchor drift detected: %s\n", rtcState.anchorDriftDetected ? "YES" : "NO");
  SerialMon.printf("- Anchor drift counter: %d\n", rtcState.anchorDriftCounter);
  SerialMon.printf("- Charging problem: %s\n", rtcState.chargingProblemDetected ? "YES" : "NO");
  SerialMon.printf("- Temp spike detected: %s\n", rtcState.tempSpikeDetected ? "YES" : "NO");
  SerialMon.printf("- Over temp detected: %s\n", rtcState.overTempDetected ? "YES" : "NO");
  SerialMon.printf("- Last upload failed: %s\n", rtcState.lastUploadFailed ? "YES" : "NO");
  SerialMon.printf("- FW update attempted: %s\n", rtcState.firmwareUpdateAttempted ? "YES" : "NO");

}

void updateLastGpsFix(float lat, float lon, uint32_t epochSec) {
  rtcState.lastGpsLat = lat;
  rtcState.lastGpsLon = lon;
  rtcState.lastGpsFixTime = epochSec;
  // Do NOT reset anchorDriftCounter/anchorDriftDetected here —
  // drift state must accumulate across boots for reliable detection.
}

void checkAnchorDrift(float currentLat, float currentLon) {
  // If we don't have a previous anchor stored, inform and return
  if (rtcState.lastGpsFixTime <= 1000000000) {
    SerialMon.println("Anchor drift check: No previous anchor");
    rtcState.anchorDriftDetected = false;
    rtcState.anchorDriftCounter = 0;
    return;
  }
  float dist = distanceBetween(currentLat, currentLon, rtcState.lastGpsLat, rtcState.lastGpsLon);
  if (dist > ANCHOR_DRIFT_DISTANCE_THRESHOLD) {
    // Accumulate consecutive drift detections across boots
    if (rtcState.anchorDriftCounter < 255) rtcState.anchorDriftCounter++;
    rtcState.anchorDriftDetected = true;
  } else {
    // No drift on this fix — reset counter (buoy is back in place)
    rtcState.anchorDriftCounter = 0;
    rtcState.anchorDriftDetected = false;
  }

  SerialMon.printf("Anchor drift check: distance=%.2f m, counter=%d, alert=%s\n",
                   dist, rtcState.anchorDriftCounter,
                   rtcState.anchorDriftDetected ? "YES" : "NO");
}

void pushTemperatureHistory(float temp) {
  if (isnan(temp)) return;
  // Shift history: oldest falls off [0], newest goes to end
  uint8_t maxEntries = 5;
  if (rtcState.tempHistoryCount < maxEntries) {
    rtcState.tempHistory[rtcState.tempHistoryCount] = temp;
    rtcState.tempHistoryCount++;
  } else {
    for (uint8_t i = 0; i < maxEntries - 1; i++) {
      rtcState.tempHistory[i] = rtcState.tempHistory[i + 1];
    }
    rtcState.tempHistory[maxEntries - 1] = temp;
  }
}

float getTemperatureTrend() {
  // Returns approximate °C per reading interval (positive = warming)
  // Uses simple difference between newest and oldest valid readings
  if (rtcState.tempHistoryCount < 2) return 0.0f;
  float oldest = NAN, newest = NAN;
  for (uint8_t i = 0; i < rtcState.tempHistoryCount; i++) {
    if (!isnan(rtcState.tempHistory[i])) { oldest = rtcState.tempHistory[i]; break; }
  }
  for (int i = rtcState.tempHistoryCount - 1; i >= 0; i--) {
    if (!isnan(rtcState.tempHistory[i])) { newest = rtcState.tempHistory[i]; break; }
  }
  if (isnan(oldest) || isnan(newest)) return 0.0f;
  return newest - oldest;
}

void checkTemperatureAnomalies() {
  float currentTemp = rtcState.lastWaterTemp;
  if (isnan(currentTemp)) {
    SerialMon.println("Temp anomaly check: no valid temperature");
    return;
  }
  // Check spike: >2°C change from previous reading
  if (rtcState.tempHistoryCount >= 2) {
    float prev = rtcState.tempHistory[rtcState.tempHistoryCount - 2];
    if (!isnan(prev)) {
      float delta = fabsf(currentTemp - prev);
      rtcState.tempSpikeDetected = (delta > 2.0f);
      if (rtcState.tempSpikeDetected) {
        SerialMon.printf("TEMP SPIKE: %.1f°C change (%.1f -> %.1f)\n", delta, prev, currentTemp);
      }
    }
  }
  // Check over-temp: >35°C is unusual for a Norwegian lake
  rtcState.overTempDetected = (currentTemp > 35.0f);
  if (rtcState.overTempDetected) {
    SerialMon.printf("OVER-TEMP: %.1f°C exceeds 35°C threshold\n", currentTemp);
  }
  float trend = getTemperatureTrend();
  SerialMon.printf("Temp anomaly check: current=%.1f°C, trend=%.2f°C, spike=%s, overTemp=%s\n",
                   currentTemp, trend,
                   rtcState.tempSpikeDetected ? "YES" : "NO",
                   rtcState.overTempDetected ? "YES" : "NO");
}

// Mark that the last data upload succeeded
void markUploadSuccess() {
  rtcState.lastUploadFailed = false;
  SerialMon.println("Upload marked as success.");
}

// Mark that the last data upload failed
void markUploadFailed() {
  rtcState.lastUploadFailed = true;
  SerialMon.println("Upload marked as failure.");
}

void markFirmwareUpdateAttempted() {
  rtcState.firmwareUpdateAttempted = true;
  SerialMon.println("Firmware update attempted — flag set for next boot.");
}

void clearFirmwareUpdateAttempted() {
  if (rtcState.firmwareUpdateAttempted) {
    SerialMon.println("Clearing firmware update flag (successful boot after OTA).");
  }
  rtcState.firmwareUpdateAttempted = false;
}

void storeUnsentJson(const String& json) {
  size_t len = json.length();
  if (len >= sizeof(rtcState.lastUnsentJson)) len = sizeof(rtcState.lastUnsentJson) - 1;
  strncpy(rtcState.lastUnsentJson, json.c_str(), len);
  rtcState.lastUnsentJson[len] = '\0';
  rtcState.hasUnsentData = true;
}

void clearUnsentJson() {
  rtcState.lastUnsentJson[0] = '\0';
  rtcState.hasUnsentData = false;
}

bool hasUnsentJson() {
  return rtcState.hasUnsentData && rtcState.lastUnsentJson[0] != '\0';
}

String getUnsentJson() {
  if (hasUnsentJson()) {
    return String(rtcState.lastUnsentJson);
  }
  return String();
}

// ── NVS persistence (survives OTA / hard reset) ──────────────────────
//
// Only the fields that matter across a hard reset are saved.
// The unsent JSON buffer is intentionally excluded — it's 512 bytes
// and NVS writes should be small. If upload failed before OTA, the
// server simply won't get that one reading.

static const char* NVS_NAMESPACE = "rtc_snap";

void saveStateToNvs() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    SerialMon.println("NVS: failed to open for writing");
    return;
  }

  prefs.putULong("bootCnt",    rtcState.bootCounter);
  prefs.putFloat("batV",       rtcState.lastBatteryVoltage);
  prefs.putFloat("gpsLat",     rtcState.lastGpsLat);
  prefs.putFloat("gpsLon",     rtcState.lastGpsLon);
  prefs.putULong("gpsFix",     rtcState.lastGpsFixTime);
  prefs.putFloat("gpsHdop",    rtcState.lastGpsHdop);
  prefs.putUShort("gpsTtf",    rtcState.lastGpsTtf);
  prefs.putFloat("wTemp",      rtcState.lastWaterTemp);
  prefs.putUChar("thCnt",      rtcState.tempHistoryCount);
  prefs.putBytes("tHist",      rtcState.tempHistory, sizeof(rtcState.tempHistory));
  prefs.putUChar("driftCnt",   rtcState.anchorDriftCounter);
  prefs.putBool("driftDet",    rtcState.anchorDriftDetected);
  prefs.putBool("chgProb",     rtcState.chargingProblemDetected);
  prefs.putBool("otaPend",     true);  // flag: restore needed on next boot

  prefs.end();
  SerialMon.println("NVS: state saved before hard reset");
}

void restoreStateFromNvs() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) {
    return;  // no NVS namespace yet — first boot ever, nothing to restore
  }

  bool pending = prefs.getBool("otaPend", false);
  prefs.end();

  if (!pending) {
    return;  // normal deep-sleep wake, RTC memory is fine
  }

  // Re-open read-write to restore and clear
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    SerialMon.println("NVS: failed to reopen for restore");
    return;
  }

  SerialMon.println("NVS: restoring state after hard reset");

  rtcState.bootCounter           = prefs.getULong("bootCnt", 0);
  rtcState.lastBatteryVoltage    = prefs.getFloat("batV", 0.0f);
  rtcState.lastGpsLat            = prefs.getFloat("gpsLat", 0.0f);
  rtcState.lastGpsLon            = prefs.getFloat("gpsLon", 0.0f);
  rtcState.lastGpsFixTime        = prefs.getULong("gpsFix", 0);
  rtcState.lastGpsHdop           = prefs.getFloat("gpsHdop", 99.0f);
  rtcState.lastGpsTtf            = prefs.getUShort("gpsTtf", 0);
  rtcState.lastWaterTemp         = prefs.getFloat("wTemp", 0.0f);
  rtcState.tempHistoryCount      = prefs.getUChar("thCnt", 0);
  prefs.getBytes("tHist", rtcState.tempHistory, sizeof(rtcState.tempHistory));
  rtcState.anchorDriftCounter    = prefs.getUChar("driftCnt", 0);
  rtcState.anchorDriftDetected   = prefs.getBool("driftDet", false);
  rtcState.chargingProblemDetected = prefs.getBool("chgProb", false);
  rtcState.firmwareUpdateAttempted = true;  // we know we got here via OTA

  // Clear the pending flag so next deep-sleep wake doesn't re-restore
  prefs.putBool("otaPend", false);
  prefs.end();

  SerialMon.printf("NVS: restored bootCounter=%lu, waterTemp=%.1f, gpsLat=%.4f\n",
                   rtcState.bootCounter, rtcState.lastWaterTemp, rtcState.lastGpsLat);
}
