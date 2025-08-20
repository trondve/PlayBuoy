#pragma once

#include <Arduino.h>

//
// Persistent state stored in RTC memory, survives deep sleep cycles.
// This tracks system state and alerts.
//
typedef struct {
  // Boot and system counters
  uint32_t bootCounter;              // Count of device wakeups/boots

  // Battery and power monitoring
  float lastBatteryVoltage;          // Last measured battery voltage
  uint32_t lastSolarChargeTime;      // millis() timestamp when solar charge was last detected

  // GPS state for anchor drift detection
  float lastGpsLat;                  // Last known GPS latitude
  float lastGpsLon;                  // Last known GPS longitude
  uint32_t lastGpsFixTime;           // Unix epoch timestamp of last GPS fix

  // Water temperature monitoring
  float lastWaterTemp;               // Last recorded water temperature
  bool tempSpikeDetected;            // Flag for sudden temperature spike
  bool overTempDetected;             // Flag for temperature exceeding threshold

  // Firmware update and upload status
  bool firmwareUpdateAttempted;      // Flag indicating OTA update was attempted
  bool lastUploadFailed;             // Flag indicating last upload failure

  // Anchor drift alert and counter
  bool anchorDriftDetected;          // Flag for confirmed anchor drift alert
  uint8_t anchorDriftCounter;        // Counter for consecutive drift detections

  // Battery charging alert
  bool chargingProblemDetected;      // Flag if no charge detected over 24 hours

  // Data buffering for failed uploads
  char lastUnsentJson[512];         // Buffer for last unsent JSON payload
  bool hasUnsentData;               // Flag if there is unsent data



} rtc_state_t;

//
// Global persistent variable stored in RTC memory.
//
RTC_DATA_ATTR extern rtc_state_t rtcState;

//
// Lifecycle management and logging
//
void rtcStateBegin();               // Called once per boot, increments boot counter
void logRtcState();                 // Logs the full rtcState struct to Serial

//
// GPS and anchor drift management
//
void updateLastGpsFix(float lat, float lon, uint32_t epochSec);
void checkAnchorDrift(float currentLat, float currentLon);

//
// Power and temperature monitoring
//
void checkBatteryChargeState();
void checkTemperatureAnomalies();

//
// Upload status and firmware update flags
//
void markUploadSuccess();
void markUploadFailed();
void markFirmwareUpdateAttempted();
void clearFirmwareUpdateAttempted();

// Data buffering helpers
void storeUnsentJson(const String& json);
void clearUnsentJson();
bool hasUnsentJson();
String getUnsentJson();
