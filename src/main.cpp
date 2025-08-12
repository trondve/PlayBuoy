#define TINY_GSM_MODEM_SIM7000
#define TINY_GSM_RX_BUFFER 1024

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <time.h>
#include <esp_system.h>
#include <math.h>
#include <algorithm>

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

// Track modem power/serial state to avoid powering early and to save battery
static bool g_modemReady = false;
// Forward declaration for function defined later
void powerOnModem();

void ensureModemReady() {
  if (g_modemReady) return;
  powerOnModem();
  SerialAT.begin(57600, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(1000);
  g_modemReady = true;
}

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
      SerialMon.println("OTA image pending verify (rollback enabled). Will mark valid after successful run.");
    }
  }

  // Initialize watchdog timer (timeout 45 minutes = 2700 seconds, panic on timeout)
  esp_task_wdt_init(2700, true);
  esp_task_wdt_add(NULL); // Add current thread to WDT

  // Initialize RTC timezone. We'll sync time from GPS, and if that fails, try HTTP Date header.
  configTzTime(TIMEZONE, NTP_SERVER);
  SerialMon.println("RTC timezone configured (CET/CEST)");
  
  // Do not overwrite RTC with a fixed default; retain time across deep sleep.

  // Defer modem power-on and serial init until needed (GPS or network)

  // Measure battery early to avoid sensor bus activity influencing ADC
  if (!beginPowerMonitor()) SerialMon.println("Power monitor not detected.");

  // Enhanced staggered battery measurement for maximum accuracy (~60s)
  SerialMon.println("=== ENHANCED BATTERY MEASUREMENT (120 SECONDS STAGGERED) ===");
  float stableBatteryVoltage = readBatteryVoltageEnhanced(/*totalReadings*/24, /*delayBetweenReadingsMs*/5000, /*quickReadsPerGroup*/3, /*minValidGroups*/12);
  if (isnan(stableBatteryVoltage)) {
    // Fallback to quiet windows method if enhanced fails
    SerialMon.println("Enhanced measurement insufficient, falling back to quiet windows");
    stableBatteryVoltage = readBatteryVoltage();
  }
  setStableBatteryVoltage(stableBatteryVoltage);
  // Update RTC snapshot values for visibility in logs
  rtcState.lastBatteryVoltage = stableBatteryVoltage;
  float tempC = getWaterTemperature();
  if (!isnan(tempC)) {
    rtcState.lastWaterTemp = tempC;
  }
  SerialMon.println("=== END BATTERY MEASUREMENT ===");

  checkBatteryChargeState();

  logBatteryStatus();  // Battery status

  if (handleUndervoltageProtection()) {
    // Device will deep sleep if battery critically low
  }

  // Initialize the rest of sensors after battery measurement (less interference)
  if (!beginSensors()) SerialMon.println("Sensor init failed.");
  // After sensors are initialized, capture first valid temp into RTC snapshot
  {
    float t = getWaterTemperature();
    if (!isnan(t)) rtcState.lastWaterTemp = t;
  }
  
  // OTA check will happen in loop() after cellular connection is established
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

  // 1) Collect wave data first (modem is still off for lower power)
  recordWaveData();
  logWaveStats();

  // 2) Then attempt GPS (powers modem only if needed); disable GNSS immediately after
  GpsFixResult fix;
  if (shouldGetNewGpsFix) {
    bool isFirstFix = (rtcState.lastGpsFixTime == 0);
    fix = getGpsFixDynamic(isFirstFix);
    if (fix.success) {
      updateLastGpsFix(fix.latitude, fix.longitude, fix.fixTimeEpoch);
      checkAnchorDrift(fix.latitude, fix.longitude);
      if (fix.fixTimeEpoch > 1000000000) {
        syncRtcWithGps(fix.fixTimeEpoch);
      }
    } else {
      if (rtcState.lastGpsFixTime > 1000000000) {
        uint32_t elapsedSinceLastFix = time(NULL) - rtcState.lastGpsFixTime;
        uint32_t currentTime = rtcState.lastGpsFixTime + elapsedSinceLastFix;
        syncRtcWithGps(currentTime);
        SerialMon.printf("GPS failed, synced RTC with last GPS time + %u seconds\n", elapsedSinceLastFix);
      }
      fix.latitude = rtcState.lastGpsLat;
      fix.longitude = rtcState.lastGpsLon;
      fix.fixTimeEpoch = rtcState.lastGpsFixTime;
      fix.success = false;
    }
    // Always disable GNSS immediately after the attempt to save power
    gpsEnd();
  } else {
    fix.latitude = rtcState.lastGpsLat;
    fix.longitude = rtcState.lastGpsLon;
    fix.fixTimeEpoch = rtcState.lastGpsFixTime;
    fix.success = true;
  }

  checkTemperatureAnomalies();

  logRtcState();

  uint32_t uptime = millis() / 1000; // seconds since boot
  String resetReason = getResetReasonString();

  // Ensure time is valid before building JSON: if GPS failed or was skipped, try network time sync once more
  if ((shouldGetNewGpsFix && !fix.success) || !shouldGetNewGpsFix) {
    // If network is not up yet, we'll sync right after connecting below; here we just attempt if already valid
    if (modem.isGprsConnected()) {
      syncTimeFromNetwork();
    }
  }

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
    resetReason,       // <-- new
    String(""), String(""), String(""), 0, // net fields will be set later when connected
    rtcState.lastBatteryVoltage,
    rtcState.lastWaterTemp
  );

  SerialMon.println("JSON payload:");
  SerialMon.println(json);
  // Print a human-friendly current local date/time
  time_t nowTs = time(NULL);
  if (nowTs >= 24 * 3600) {
    struct tm lt; localtime_r(&nowTs, &lt);
    char buf[64]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &lt);
    SerialMon.printf("The current date and time is: %s\n", buf);
  }

  // If booting from a pending OTA image, we consider reaching here as a successful run
  {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t otaState;
    if (esp_ota_get_state_partition(running, &otaState) == ESP_OK &&
        otaState == ESP_OTA_IMG_PENDING_VERIFY) {
      SerialMon.println("Marking OTA image as valid after successful run.");
      esp_ota_mark_app_valid_cancel_rollback();
    }
  }

  // Try to send any buffered unsent JSON first
  bool bufferedDataSent = false;
  bool networkConnected = false;
  bool shouldProceedWithNewData = true;
  
  if (hasUnsentJson()) {
    SerialMon.println("Attempting to resend buffered unsent data...");
    ensureModemReady();
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
    ensureModemReady();
    networkConnected = connectToNetwork(NETWORK_PROVIDER);
    
    // If regular connection fails, try APN testing
    if (!networkConnected) {
      SerialMon.println("Regular connection failed, testing multiple APNs...");
      networkConnected = testMultipleAPNs();
    }
    
    // If GPS failed or we skipped GPS, try to sync time via HTTP Date now that PDP is up
    if ((shouldGetNewGpsFix && !fix.success) || !shouldGetNewGpsFix) {
      if (syncTimeFromNetwork()) {
        // Recompute currentTimestamp after successful time sync
        SerialMon.println("Time synced from network; rebuilding timestamp for JSON");
      }
    }

    // Check for firmware updates if network is connected
    if (networkConnected) {
       SerialMon.printf(" OTA: OTA_SERVER = %s\n", OTA_SERVER);
       SerialMon.printf(" OTA: OTA_PATH = %s\n", OTA_PATH);
       SerialMon.printf(" OTA: NODE_ID = %s\n", NODE_ID);
       
       String baseUrl = "http://" + String(OTA_SERVER) + "/" + String(NODE_ID);
       SerialMon.printf(" OTA: Constructed baseUrl: %s\n", baseUrl.c_str());
       
       if (checkForFirmwareUpdate(baseUrl.c_str())) {
         // OTA update in progress, will restart on completion
       }
    }
    
    if (networkConnected) {
      // If we synced time from network just above, prefer to rebuild currentTimestamp now
      uint32_t ts = time(NULL);
      if (ts >= 24 * 3600) {
        currentTimestamp = ts;
      }
      // Rebuild JSON so it includes updated timestamp and network diagnostics
      String op = modem.getOperator();
      String ipStr = String((int)modem.localIP()[0]) + "." + String((int)modem.localIP()[1]) + "." + String((int)modem.localIP()[2]) + "." + String((int)modem.localIP()[3]);
      int rssi = modem.getSignalQuality();
      json = buildJsonPayload(
        fix.latitude,
        fix.longitude,
        computeWaveHeight(),
        computeWavePeriod(),
        computeWaveDirection(),
        computeWavePower(computeWaveHeight(), computeWavePeriod()),
        getWaterTemperature(),
        getStableBatteryVoltage(),
        currentTimestamp,
        NODE_ID,
        NAME,
        FIRMWARE_VERSION,
        getHeadingDegrees(),
        uptime,
        resetReason,
        op,
        String(NETWORK_PROVIDER),
        ipStr,
        rssi,
        rtcState.lastBatteryVoltage,
        rtcState.lastWaterTemp
      );
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

  powerOffModem();

  esp_sleep_enable_timer_wakeup((uint64_t)sleepHours * 3600ULL * 1000000ULL);
  esp_deep_sleep_start();
}
