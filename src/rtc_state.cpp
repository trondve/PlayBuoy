#include "rtc_state.h"
#include <Arduino.h>

#define SerialMon Serial

// Define the persistent RTC state variable in RTC fast memory
RTC_DATA_ATTR rtc_state_t rtcState = {
  .bootCounter = 0,
  .lastBatteryVoltage = 0.0f,
  .lastSolarChargeTime = 0,
  .lastGpsLat = 0.0f,
  .lastGpsLon = 0.0f,
  .lastGpsFixTime = 0,
  .lastWaterTemp = 0.0f,
  .tempSpikeDetected = false,
  .overTempDetected = false,
  .firmwareUpdateAttempted = false,
  .lastUploadFailed = false,
  .anchorDriftDetected = false,
  .anchorDriftCounter = 0,
  .chargingProblemDetected = false,
  .lastUnsentJson = {0},
  .hasUnsentData = false,
  .lastSleepHours = 0,
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

#define ANCHOR_DRIFT_THRESHOLD 3
#define ANCHOR_DRIFT_DISTANCE_THRESHOLD 50.0f

void rtcStateBegin() {
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
  SerialMon.printf("- Firmware update attempted: %s\n", rtcState.firmwareUpdateAttempted ? "YES" : "NO");
  SerialMon.printf("- Last upload failed: %s\n", rtcState.lastUploadFailed ? "YES" : "NO");

}

void updateLastGpsFix(float lat, float lon, uint32_t epochSec) {
  rtcState.lastGpsLat = lat;
  rtcState.lastGpsLon = lon;
  rtcState.lastGpsFixTime = epochSec;
  rtcState.anchorDriftCounter = 0;
  rtcState.anchorDriftDetected = false;
}

void checkAnchorDrift(float currentLat, float currentLon) {
  float dist = distanceBetween(currentLat, currentLon, rtcState.lastGpsLat, rtcState.lastGpsLon);
  bool driftDetectedNow = dist > ANCHOR_DRIFT_DISTANCE_THRESHOLD;

  if (driftDetectedNow) {
    rtcState.anchorDriftCounter++;
    if (rtcState.anchorDriftCounter >= ANCHOR_DRIFT_THRESHOLD) {
      rtcState.anchorDriftDetected = true;
    }
  } else {
    rtcState.anchorDriftCounter = 0;
    rtcState.anchorDriftDetected = false;
  }

  SerialMon.printf("Anchor drift check: distance=%.2f m, counter=%d, alert=%s\n",
                   dist, rtcState.anchorDriftCounter,
                   rtcState.anchorDriftDetected ? "YES" : "NO");
}

// Stub: Detects sudden water temperature spikes or over temperature alerts
void checkTemperatureAnomalies() {
  SerialMon.println("Checking temperature anomalies...");
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

// Mark that a firmware update attempt was started
void markFirmwareUpdateAttempted() {
  rtcState.firmwareUpdateAttempted = true;
  SerialMon.println("Firmware update attempt flagged.");
}

// Clear the firmware update attempt flag after successful update
void clearFirmwareUpdateAttempted() {
  rtcState.firmwareUpdateAttempted = false;
  SerialMon.println("Firmware update attempt flag cleared.");
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
