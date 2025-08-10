#define TINY_GSM_MODEM_SIM7000
#define TINY_GSM_RX_BUFFER 1024

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <time.h>
#include <esp_system.h>

// Project modules
#include "wave.h" // Wave data processing
#include "power.h" // Power management and battery monitoring
#include "sensors.h" // Sensor initialization and data collection
#include "rtc_state.h" // RTC state management
#include "gps.h" // GPS handling
#include "json.h" // JSON payload construction
#include "modem.h"  // Modem initialization and management
#include "battery.h" // Battery monitoring and management
#include "ota.h" // OTA update handling
#include "utils.h" // Utility functions (e.g., logging, time management)
#include "config.h"  // Your NODE_ID, FIRMWARE_VERSION, GPS_SYNC_INTERVAL_SECONDS

// Add watchdog include
#include "esp_task_wdt.h"

// ESP32 OTA headers
extern "C" {
  #include "esp_ota_ops.h"
  #include "esp_partition.h"
}

// LilyGo T7000G Pin Definitions (matching your working test)
#define MODEM_RX 26
#define MODEM_TX 27
#define MODEM_PWRKEY 4
#define MODEM_RST 5
#define MODEM_POWER_ON 23
#define MODEM_DTR 32
#define MODEM_RI 33

#define SerialMon Serial
#define SerialAT Serial1

#define GPS_FIX_TIMEOUT_SEC 60

TinyGsm modem(SerialAT);

void powerOnModem() {
  SerialMon.println("Starting modem power sequence...");
  
  // Configure all modem pins
  pinMode(MODEM_PWRKEY, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);
  pinMode(MODEM_POWER_ON, OUTPUT);
  pinMode(MODEM_DTR, OUTPUT);
  pinMode(MODEM_RI, INPUT);
  
  // Power sequence (matching your working test)
  digitalWrite(MODEM_POWER_ON, LOW);
  digitalWrite(MODEM_RST, LOW);
  digitalWrite(MODEM_PWRKEY, HIGH);
  digitalWrite(MODEM_DTR, HIGH);
  delay(100);
  
  digitalWrite(MODEM_POWER_ON, HIGH);
  delay(1000);
  
  digitalWrite(MODEM_RST, HIGH);
  delay(100);
  digitalWrite(MODEM_RST, LOW);
  delay(100);
  digitalWrite(MODEM_RST, HIGH);
  delay(3000);
  
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  
  digitalWrite(MODEM_DTR, LOW);
  
  SerialMon.println("Power sequence complete. Waiting for modem...");
  delay(5000);
}

void powerOffModem() {
  SerialMon.println("Powering off modem...");
  
  // Power off sequence
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(1500); // Hold high for at least 1.2s to power off SIM7000G
  
  // Also power down the power control pin
  digitalWrite(MODEM_POWER_ON, LOW);
  digitalWrite(MODEM_DTR, HIGH);
  
  SerialMon.println("Modem powered off.");
}

// Set ESP32 RTC time from GPS epoch
void syncRtcWithGps(uint32_t gpsEpoch) {
  struct timeval tv;
  tv.tv_sec = gpsEpoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  SerialMon.printf("RTC synced to GPS time: %lu\n", gpsEpoch);
}

String getResetReasonString() {
  esp_reset_reason_t reason = esp_reset_reason();
  switch (reason) {
    case ESP_RST_POWERON: return "PowerOn";
    case ESP_RST_EXT: return "External";
    case ESP_RST_SW: return "Software";
    case ESP_RST_PANIC: return "Panic";
    case ESP_RST_INT_WDT: return "IntWDT";
    case ESP_RST_TASK_WDT: return "TaskWDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DeepSleep";
    case ESP_RST_BROWNOUT: return "Brownout";
    case ESP_RST_SDIO: return "SDIO";
    default: return "Unknown";
  }
}

void setup() {
  SerialMon.begin(115200);
  delay(3000);
  logWakeupReason();  // Log why we woke up

  rtcStateBegin();

  if (rtcState.firmwareUpdateAttempted) {
    clearFirmwareUpdateAttempted();
    SerialMon.println("OTA flag cleared after reboot.");
  }

  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t otaState;
  if (esp_ota_get_state_partition(running, &otaState) == ESP_OK) {
    if (otaState == ESP_OTA_IMG_PENDING_VERIFY) {
      SerialMon.println("Verifying OTA firmware...");
      bool firmwareValid = true; // implement your own check if needed

      if (firmwareValid) {
        SerialMon.println("Marking firmware as valid.");
        esp_ota_mark_app_valid_cancel_rollback();
      } else {
        SerialMon.println("Firmware invalid. Rolling back.");
        esp_ota_mark_app_invalid_rollback_and_reboot();
      }
    }
  }

  // Initialize watchdog timer (timeout 45 minutes = 2700 seconds, panic on timeout)
  esp_task_wdt_init(2700, true);
  esp_task_wdt_add(NULL); // Add current thread to WDT

  // Initialize RTC with Europe/Oslo timezone (automatic DST)
  configTzTime(TIMEZONE, NTP_SERVER);
  SerialMon.println("RTC initialized with Europe/Oslo timezone (CET/CEST with DST)");
  
  // Set a default time if NTP is not available (August 10, 2025, 10:00:00 CEST)
  // This ensures we have a reasonable time even without GPS or NTP
  struct timeval tv;
  tv.tv_sec = 1754812800;  // August 10, 2025 10:00:00 CEST (UTC+2) = 08:00:00 UTC
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  SerialMon.println("Set default RTC time: August 10, 2025 10:00:00 CEST");

  powerOnModem();
  SerialAT.begin(57600, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(1000);
  
  // Test basic communication before proceeding
  SerialMon.println("Testing basic modem communication...");
  SerialAT.println("AT");
  delay(1000);
  
  String response = "";
  while (SerialAT.available()) {
    response += (char)SerialAT.read();
  }
  
  if (response.indexOf("OK") >= 0) {
    SerialMon.println("✅ Modem communication successful");
  } else {
    SerialMon.println("❌ Modem communication failed. Response: " + response);
  }
  
  // Get modem info
  SerialMon.println("Modem Info: " + modem.getModemInfo());

  if (!beginSensors()) SerialMon.println("Sensor init failed.");
  if (!beginPowerMonitor()) SerialMon.println("Power monitor not detected.");

  // Measure battery voltage BEFORE any power-intensive operations
  // This ensures we get a stable reading without voltage drops from other processes
  SerialMon.println("=== BATTERY MEASUREMENT (STABLE STATE) ===");
  float totalVoltage = 0.0f;
  int validReadings = 0;
  for (int i = 0; i < 5; i++) {
    float voltage = readBatteryVoltage();
    if (voltage >= 3.8f && voltage <= 4.3f) {
      totalVoltage += voltage;
      validReadings++;
    }
    delay(100);
  }
  
  float stableBatteryVoltage = 0.0f;
         if (validReadings > 0) {
         stableBatteryVoltage = totalVoltage / validReadings;
         SerialMon.printf("Stable battery voltage: %.2fV (from %d readings)\n", stableBatteryVoltage, validReadings);
         calibrateBatteryVoltage(4.16f);  // Calibrate against current multimeter reading
         setStableBatteryVoltage(stableBatteryVoltage);  // Store for use throughout cycle
  } else {
    SerialMon.println("⚠️  Could not get stable voltage readings for calibration");
    stableBatteryVoltage = 4.0f;  // Safe fallback
    setStableBatteryVoltage(stableBatteryVoltage);
  }
  SerialMon.println("=== END BATTERY MEASUREMENT ===");

  checkBatteryChargeState();

  logBatteryStatus();  // New battery status logging here

  if (handleUndervoltageProtection()) {
    // Device will deep sleep if battery critically low
  }

  // Build firmware URL using config values
  String firmwareUrl = "https://" + String(OTA_SERVER) + String(OTA_PATH) + "/" + String(NODE_ID) + ".bin";
  SerialMon.printf("Checking for firmware update at: %s\n", firmwareUrl.c_str());
  
  if (checkAndPerformOTA(firmwareUrl.c_str())) {
    // OTA update in progress, will restart on completion
  }
}

void loop() {
  // Feed the watchdog at the start of each loop
  esp_task_wdt_reset();

  uint32_t now = time(NULL);

  bool shouldGetNewGpsFix = true;
  if (rtcState.lastGpsFixTime > 1000000000) {  // sanity check for valid epoch time
    uint32_t age = now - rtcState.lastGpsFixTime;
    if (age < GPS_SYNC_INTERVAL_SECONDS) {
      SerialMon.printf("Last GPS fix is recent (%u seconds ago). Skipping new fix.\n", age);
      shouldGetNewGpsFix = false;
    }
  }

  GpsFixResult fix;
  if (shouldGetNewGpsFix) {
    // Use dynamic GPS timeout based on battery and whether this is first fix
    bool isFirstFix = (rtcState.lastGpsFixTime == 0);
    fix = getGpsFixDynamic(isFirstFix);
    if (fix.success) {
      updateLastGpsFix(fix.latitude, fix.longitude, fix.fixTimeEpoch);
      checkAnchorDrift(fix.latitude, fix.longitude);
      // Sync RTC with GPS time
      if (fix.fixTimeEpoch > 1000000000) { // sanity check for valid epoch
        syncRtcWithGps(fix.fixTimeEpoch);
      }
    } else {
      // GPS failed, but we can still use last known GPS time to sync RTC
      if (rtcState.lastGpsFixTime > 1000000000) {
        // Calculate current time based on last GPS time + elapsed time
        uint32_t elapsedSinceLastFix = time(NULL) - rtcState.lastGpsFixTime;
        uint32_t currentTime = rtcState.lastGpsFixTime + elapsedSinceLastFix;
        syncRtcWithGps(currentTime);
        SerialMon.printf("GPS failed, synced RTC with last GPS time + %u seconds\n", elapsedSinceLastFix);
      }
      // Use last known position
      fix.latitude = rtcState.lastGpsLat;
      fix.longitude = rtcState.lastGpsLon;
      fix.fixTimeEpoch = rtcState.lastGpsFixTime;
      fix.success = false; // Mark as failed for logging
    }
  } else {
    fix.latitude = rtcState.lastGpsLat;
    fix.longitude = rtcState.lastGpsLon;
    fix.fixTimeEpoch = rtcState.lastGpsFixTime;
    fix.success = true;
  }

  recordWaveData();
  logWaveStats();

  checkTemperatureAnomalies();

  logRtcState();

  uint32_t uptime = millis() / 1000; // seconds since boot
  String resetReason = getResetReasonString();

  // Get current timestamp from RTC, with fallback to GPS time
  uint32_t currentTimestamp = time(NULL);
  if (currentTimestamp < 24 * 3600) {  // If RTC time is not valid
    if (rtcState.lastGpsFixTime > 1000000000) {
      // Use last GPS time as fallback
      currentTimestamp = rtcState.lastGpsFixTime;
      SerialMon.printf("Using last GPS time as timestamp: %lu\n", currentTimestamp);
    } else {
      // No valid time available
      currentTimestamp = 0;
      SerialMon.println("No valid timestamp available, using 0");
    }
  } else {
    SerialMon.printf("Using RTC timestamp: %lu\n", currentTimestamp);
  }

  String json = buildJsonPayload(
    fix.latitude,
    fix.longitude,
    computeWaveHeight(),
    computeWavePeriod(),
    computeWaveDirection(),
    computeWavePower(computeWaveHeight(), computeWavePeriod()),
    getWaterTemperature(),
    getStableBatteryVoltage(),  // Use stable voltage instead of measuring during upload
    currentTimestamp,  // Use current RTC time instead of last GPS time
    NODE_ID,
    NAME,
    FIRMWARE_VERSION,
    getHeadingDegrees(),
    uptime,           // <-- new
    resetReason       // <-- new
  );

  SerialMon.println("JSON payload:");
  SerialMon.println(json);

  // Try to send any buffered unsent JSON first
  bool bufferedDataSent = false;
  bool networkConnected = false;
  bool shouldProceedWithNewData = true;
  
  if (hasUnsentJson()) {
    SerialMon.println("Attempting to resend buffered unsent data...");
    networkConnected = connectToNetwork(NETWORK_PROVIDER);
    
    // If regular connection fails, try APN testing
    if (!networkConnected) {
      SerialMon.println("Regular connection failed, testing multiple APNs...");
      networkConnected = testMultipleAPNs();
    }
    
    if (networkConnected) {
      bool success = sendJsonToServer(
        API_SERVER,
        API_PORT,
        API_ENDPOINT,
        getUnsentJson()
      );
      if (success) {
        SerialMon.println("Buffered data upload successful.");
        clearUnsentJson();
        markUploadSuccess();
        bufferedDataSent = true;
      } else {
        SerialMon.println("Buffered data upload failed, will retry next wakeup.");
        markUploadFailed();
        shouldProceedWithNewData = false;
      }
    } else {
      SerialMon.println("Network connection failed for buffered data.");
      markUploadFailed();
      shouldProceedWithNewData = false;
    }
  }

  // Only proceed with new data if buffered data was handled successfully
  if (shouldProceedWithNewData) {
    networkConnected = connectToNetwork(NETWORK_PROVIDER);
    
    // If regular connection fails, try APN testing
    if (!networkConnected) {
      SerialMon.println("Regular connection failed, testing multiple APNs...");
      networkConnected = testMultipleAPNs();
    }
    
    if (networkConnected) {
      bool success = sendJsonToServer(
        API_SERVER,
        API_PORT,
        API_ENDPOINT,
        json
      );
      if (success) {
        markUploadSuccess();
        clearUnsentJson();
      } else {
        markUploadFailed();
        storeUnsentJson(json);
      }
    } else {
      SerialMon.println("Network connection failed.");
      markUploadFailed();
      storeUnsentJson(json);
    }
  }
  int batteryPercent = estimateBatteryPercent(getStableBatteryVoltage());  // Use stable voltage
  int sleepHours = determineSleepDuration(batteryPercent);

  SerialMon.printf("Sleeping for %d hour(s)...\n", sleepHours);
  delay(100);

  // powerOffModem();

  esp_sleep_enable_timer_wakeup((uint64_t)sleepHours * 3600ULL * 1000000ULL);
  esp_deep_sleep_start();
}
