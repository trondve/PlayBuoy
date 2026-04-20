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
// Uses double-precision internally to avoid precision loss at small angles (<100m)
static float distanceBetween(float lat1, float lon1, float lat2, float lon2) {
  const double R = 6371000.0; // Earth radius in meters
  double dLat = radians((double)(lat2 - lat1));
  double dLon = radians((double)(lon2 - lon1));

  double a = sin(dLat / 2.0) * sin(dLat / 2.0) +
            cos(radians((double)lat1)) * cos(radians((double)lat2)) *
            sin(dLon / 2.0) * sin(dLon / 2.0);
  double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
  double distance = R * c;

  return (float)distance;  // Cast back to float for storage
}

#define ANCHOR_DRIFT_DISTANCE_THRESHOLD 50.0f

void rtcStateBegin() {
  // Restore state from NVS if this boot follows a hard reset (OTA, brownout).
  // Must happen before incrementing bootCounter so the saved count is correct.
  bool wasHardReset = restoreStateFromNvs();

  // Save bootCounter before increment to detect first boot
  uint32_t bootCounterBefore = rtcState.bootCounter;
  rtcState.bootCounter++;

  // On first boot (power-on reset) or hard reset, zero the unsent JSON buffer.
  // RTC memory may not be cleared on every boot path, leaving stale data.
  if (bootCounterBefore == 0 || wasHardReset) {
    rtcState.lastUnsentJson[0] = '\0';
    rtcState.hasUnsentData = false;
  }

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

  // Reset spike flag at start of each check; only set if detected this cycle
  rtcState.tempSpikeDetected = false;

  // Check spike: >2°C change from previous reading (single-sample transient)
  if (rtcState.tempHistoryCount >= 2) {
    float prev = rtcState.tempHistory[rtcState.tempHistoryCount - 2];
    if (!isnan(prev)) {
      float delta = fabsf(currentTemp - prev);
      if (delta > 2.0f) {
        rtcState.tempSpikeDetected = true;
        SerialMon.printf("TEMP SPIKE: %.1f°C change this cycle (%.1f -> %.1f)\n", delta, prev, currentTemp);
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
  if (len >= sizeof(rtcState.lastUnsentJson)) {
    // Payload exceeds buffer — storing a truncated fragment would produce invalid JSON
    // that the server rejects (HTTP 400), causing an infinite retry loop. Drop it instead.
    SerialMon.printf("storeUnsentJson: payload %u bytes exceeds buffer %u — dropping to avoid retry loop\n",
                     (unsigned)len, (unsigned)sizeof(rtcState.lastUnsentJson));
    rtcState.hasUnsentData = false;
    return;
  }
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

bool restoreStateFromNvs() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) {
    return false;  // no NVS namespace yet — first boot ever, nothing to restore
  }

  bool pending = prefs.getBool("otaPend", false);
  prefs.end();

  if (!pending) {
    return false;  // normal deep-sleep wake, RTC memory is fine
  }

  // Re-open read-write to restore and clear
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    SerialMon.println("NVS: failed to reopen for restore");
    return false;
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

  // Validate restored data; treat corrupted NVS as cold-boot fallback
  bool validRestore = true;
  if (rtcState.bootCounter > 1e6) validRestore = false;  // Unreasonable boot count
  if (rtcState.lastWaterTemp < -50.0f || rtcState.lastWaterTemp > 100.0f) validRestore = false;  // Out-of-range temp
  if (rtcState.lastGpsLat < -90.0f || rtcState.lastGpsLat > 90.0f) validRestore = false;  // Invalid latitude
  if (rtcState.lastGpsLon < -180.0f || rtcState.lastGpsLon > 180.0f) validRestore = false;  // Invalid longitude
  if (rtcState.lastGpsFixTime > 0 && rtcState.lastGpsFixTime < 1000000000) validRestore = false;  // Before year 2001

  if (!validRestore) {
    SerialMon.println("NVS: restored data validation FAILED — treating as cold boot");
    prefs.putBool("otaPend", false);
    prefs.end();
    return false;  // Treat as validation failure; caller will use RTC defaults
  }

  // Clear the pending flag so next deep-sleep wake doesn't re-restore
  prefs.putBool("otaPend", false);
  prefs.end();

  SerialMon.printf("NVS: restored bootCounter=%lu, waterTemp=%.1f, gpsLat=%.4f\n",
                   rtcState.bootCounter, rtcState.lastWaterTemp, rtcState.lastGpsLat);
  return true;
}
