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

// Power management pins
#define POWER_3V3_ENABLE 25  // GPIO 25 to control 3.3V rail power
#define GPS_POWER_PIN 4      // GPIO 4 for GPS power control (same as MODEM_PWRKEY)

#define SerialMon Serial
#define SerialAT Serial1

#define GPS_FIX_TIMEOUT_SEC 60

TinyGsm modem(SerialAT);

// Track modem power/serial state to avoid powering early and to save battery
static bool g_modemReady = false;
static bool g_3v3RailPowered = false;
static bool g_sensorsPowered = false;
static bool g_gpsPinHigh = false;
static bool g_sensorsInitialized = false;
static float g_prevBatteryVoltage = 0.0f;

// Forward declaration for functions defined later
void powerOnModem();
void powerOffModem();
void powerOn3V3Rail();
void powerOff3V3Rail();
void powerOnSensors();
void powerOffSensors();
void powerOnGPS();
void powerOffGPS();

void ensureModemReady() {
  if (g_modemReady) {
    // Probe modem liveness with a raw AT; if unresponsive, force re-power
    while (SerialAT.available()) SerialAT.read(); // flush
    SerialAT.print("AT\r\n");
    unsigned long t0 = millis();
    bool ok = false;
    String resp;
    while (millis() - t0 < 1000) {
      while (SerialAT.available()) {
        char c = (char)SerialAT.read();
        resp += c;
        if (resp.indexOf("OK") >= 0) { ok = true; break; }
      }
      if (ok) break;
      delay(10);
    }
    if (ok) return;
    SerialMon.println("Modem not responsive; re-powering...");
    g_modemReady = false;
  }
  powerOnModem();
  SerialAT.begin(57600, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(2000);
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
  
  // Softer power sequence: keep DTR HIGH (sleep), avoid extra RST pulsing
  digitalWrite(MODEM_DTR, HIGH);
  digitalWrite(MODEM_POWER_ON, LOW);
  digitalWrite(MODEM_RST, LOW);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(100);

  digitalWrite(MODEM_POWER_ON, HIGH);
  delay(1000);

  // Single reset line release, no pulse train
  digitalWrite(MODEM_RST, HIGH);
  delay(100);

  // Boot with PWRKEY low>=1s then high
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1200);
  digitalWrite(MODEM_PWRKEY, HIGH);

  // Keep modem asleep (DTR HIGH) and idle longer before UART/attach
  SerialMon.println("Power sequence complete. Settling modem...");
  delay(7000);
}

// Ensure modem is awake before registration/attach (DTR LOW)
void wakeModemForNetwork() {
  digitalWrite(MODEM_DTR, LOW);
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
  
  g_modemReady = false;
  SerialMon.println("Modem powered off.");
}

// Power management functions for 3.3V rail
void powerOn3V3Rail() {
  if (g_3v3RailPowered) {
    SerialMon.println("3.3V rail already powered on.");
    return;
  }
  
  SerialMon.println("Powering on 3.3V rail...");
  pinMode(POWER_3V3_ENABLE, OUTPUT);
  digitalWrite(POWER_3V3_ENABLE, HIGH);
  g_3v3RailPowered = true;
  SerialMon.println("3.3V rail powered on.");
}

void powerOff3V3Rail() {
  if (!g_3v3RailPowered) {
    SerialMon.println("3.3V rail already powered off.");
    return;
  }
  
  SerialMon.println("Powering off 3.3V rail...");
  digitalWrite(POWER_3V3_ENABLE, LOW);
  g_3v3RailPowered = false;
  SerialMon.println("3.3V rail powered off.");
}

// Power management functions for sensors
void powerOnSensors() {
  if (g_sensorsPowered) {
    SerialMon.println("Sensors already powered on.");
    return;
  }
  
  SerialMon.println("Powering on sensors...");
  // Sensors are powered by the 3.3V rail, so they should be available now
  g_sensorsPowered = true;
  SerialMon.println("Sensors powered on.");
}

void powerOffSensors() {
  if (!g_sensorsPowered) {
    SerialMon.println("Sensors already powered off.");
    return;
  }
  
  SerialMon.println("Powering off sensors...");
  // Sensors will be powered off when 3.3V rail is turned off
  g_sensorsPowered = false;
  SerialMon.println("Sensors powered off.");
}

// Power management functions for GPS
void powerOnGPS() {
  if (g_gpsPinHigh) {
    SerialMon.println("GPS power pin already HIGH.");
    return;
  }
  
  SerialMon.println("Setting GPS power pin HIGH...");
  pinMode(GPS_POWER_PIN, OUTPUT);
  digitalWrite(GPS_POWER_PIN, HIGH);
  g_gpsPinHigh = true;
  SerialMon.println("GPS power pin set HIGH.");
}

void powerOffGPS() {
  if (!g_gpsPinHigh) {
    SerialMon.println("GPS power pin already LOW.");
    return;
  }
  
  SerialMon.println("Setting GPS power pin LOW...");
  digitalWrite(GPS_POWER_PIN, LOW);
  g_gpsPinHigh = false;
  SerialMon.println("GPS power pin set LOW.");
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
  esp_reset_reason_t rr = esp_reset_reason();
  if (rr == ESP_RST_DEEPSLEEP) {
    esp_sleep_wakeup_cause_t wc = esp_sleep_get_wakeup_cause();
    switch (wc) {
      case ESP_SLEEP_WAKEUP_TIMER: {
        if (rtcState.lastSleepHours > 0) {
          return String("WokeUpFromTimerSleep(") + String((int)rtcState.lastSleepHours) + String("h)");
        }
        return String("WokeUpFromTimerSleep");
      }
      case ESP_SLEEP_WAKEUP_EXT0: return "WokeUpFromGpioSleep(EXT0)";
      case ESP_SLEEP_WAKEUP_EXT1: return "WokeUpFromGpioSleep(EXT1)";
      case ESP_SLEEP_WAKEUP_TOUCHPAD: return "WokeUpFromTouchSleep";
      case ESP_SLEEP_WAKEUP_ULP: return "WokeUpFromUlP";
      case ESP_SLEEP_WAKEUP_GPIO: return "WokeUpFromGpioSleep";
      case ESP_SLEEP_WAKEUP_UART: return "WokeUpFromUart";
      case ESP_SLEEP_WAKEUP_WIFI: return "WokeUpFromWifi";
      case ESP_SLEEP_WAKEUP_COCPU: return "WokeUpFromCoCPU";
      case ESP_SLEEP_WAKEUP_ALL: return "WokeUpFromAll";
      case ESP_SLEEP_WAKEUP_UNDEFINED: return "WokeUpFromUndefined";
      default: return "WokeUpFromUnknown";
    }
  }
  switch (rr) {
    case ESP_RST_POWERON: return "PowerOn";
    case ESP_RST_EXT: return "ExternalReset";
    case ESP_RST_SW: return "SoftwareReset";
    case ESP_RST_PANIC: return "PanicReset";
    case ESP_RST_INT_WDT: return "IntWDT";
    case ESP_RST_TASK_WDT: return "TaskWDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_BROWNOUT: return "BrownoutRecovery";
    case ESP_RST_SDIO: return "SDIO";
    default: return "Unknown";
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  logWakeupReason();  // Log why we woke up
  
  // Check if we're recovering from a brownout
  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_BROWNOUT) {
    SerialMon.println("=== BROWNOUT RECOVERY DETECTED ===");
    SerialMon.println("Device recovered from brownout reset");
    SerialMon.println("Implementing conservative power management");
    SerialMon.println("================================");
  }

  // Initialize power management pins
  pinMode(POWER_3V3_ENABLE, OUTPUT);
  digitalWrite(POWER_3V3_ENABLE, LOW);  // Start with 3.3V rail off
  g_3v3RailPowered = false;
  g_sensorsPowered = false;
  g_gpsPinHigh = false;

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
  

  // Measure battery early using the new method and store as stable value
  if (!beginPowerMonitor()) SerialMon.println("Power monitor init failed.");
  float stableBatteryVoltage = readBatteryVoltage();
  g_prevBatteryVoltage = rtcState.lastBatteryVoltage; // capture previous persisted value
  setStableBatteryVoltage(stableBatteryVoltage);
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
  // OPTIMIZED: Reduced from 5 minutes to 3 minutes for wave data collection
  SerialMon.println("=== Starting wave data collection with power management (3 minutes) ===");
  
  // Check voltage before high-power operation using stable reading
  float voltage = getStableBatteryVoltage();
  if (!isnan(voltage)) {
    if (voltage < 3.2f) {
      SerialMon.printf("WARNING: Low voltage (%.2fV) before wave data collection\n", voltage);
      SerialMon.println("Consider reducing power consumption or delaying operation");
    } else if (voltage < 3.5f) {
      SerialMon.printf("CAUTION: Moderate voltage (%.2fV) - monitoring closely\n", voltage);
    } else {
      SerialMon.printf("Voltage OK (%.2fV) for wave data collection\n", voltage);
    }
  }
  
  // Power on 3.3V rail, wait 5 seconds, then power on sensors
  powerOn3V3Rail();
  delay(5000);  // 5 second delay as requested
  powerOnSensors();
  if (!g_sensorsInitialized) {
    if (!beginSensors()) SerialMon.println("Sensor init failed.");
    g_sensorsInitialized = true;
    float t0 = getWaterTemperature();
    if (!isnan(t0)) rtcState.lastWaterTemp = t0;
  }
  
  esp_task_wdt_reset();
  recordWaveData();
  esp_task_wdt_reset();
  logWaveStats();
  
  // Power off sensors, wait 5 seconds, then power off 3.3V rail
  powerOffSensors();
  delay(5000);  // 5 second delay as requested
  powerOff3V3Rail();
  
  SerialMon.println("=== Wave data collection complete ===");

  // 2) Skip pre-GPS HTTP time sync; GPS flow will do NTP + XTRA
  bool networkConnected = false;

  // 3) Then attempt GPS (now with valid time)
  // OPTIMIZED: Smart timeout based on battery level and first fix status
  SerialMon.println("Starting GNSS fix procedure...");
  GpsFixResult fix;
  if (shouldGetNewGpsFix) {
    bool isFirstFix = (rtcState.lastGpsFixTime == 0);
    
    // Bring up modem and configure GNSS just before NTP/XTRA/GPS flow to save power
    ensureModemReady();
    // Enable GPS, GLONASS, BeiDou; disable Galileo
    modem.sendAT("+CGNSMOD=1,1,0,1");
    modem.waitResponse(1000);

    fix = getGpsFixDynamic(isFirstFix);
    if (fix.success) {
      // Check drift against previous position before overwriting it
      if (rtcState.lastGpsFixTime > 1000000000) {
        checkAnchorDrift(fix.latitude, fix.longitude);
      }
      updateLastGpsFix(fix.latitude, fix.longitude, fix.fixTimeEpoch);
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
    // Turn off GNSS, wait 5 seconds, set GPS power pin back to LOW, wait 5 seconds
    gpsEnd();
    delay(5000);  // 5 second delay as requested
    powerOffGPS();
    delay(5000);  // 5 second delay as requested
    
    // Re-establish cellular data connection for firmware updates and JSON upload
    SerialMon.println("Re-establishing cellular data connection for upload...");
    ensureModemReady();
    delay(4000);  // increased settle before first connect attempt
    networkConnected = connectToNetwork(NETWORK_PROVIDER);
    
    // If regular connection fails, try APN testing
    if (!networkConnected) {
      SerialMon.println("Regular connection failed, testing multiple APNs...");
      networkConnected = testMultipleAPNs();
    }
  } else {
    fix.latitude = rtcState.lastGpsLat;
    fix.longitude = rtcState.lastGpsLon;
    fix.fixTimeEpoch = rtcState.lastGpsFixTime;
    fix.success = true;
    
    // GPS was skipped, but we still need cellular data for firmware updates and JSON upload
    SerialMon.println("GPS skipped, establishing cellular data connection for upload...");
    ensureModemReady();
    delay(4000);  // increased settle before first connect attempt
    networkConnected = connectToNetwork(NETWORK_PROVIDER);
    
    // If regular connection fails, try APN testing
    if (!networkConnected) {
      SerialMon.println("Regular connection failed, testing multiple APNs...");
      networkConnected = testMultipleAPNs();
    }
  }

  checkTemperatureAnomalies();

  logRtcState();

  uint32_t uptime = millis() / 1000; // seconds since boot
  String resetReason = getResetReasonString();

  // Skip legacy HTTP time sync; RTC was set during GPS NTP step if available

  // Get current timestamp from RTC, with fallback to GPS time
  SerialMon.println("Building JSON payload...");
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
    SerialMon.printf("Using RTC timestamp (UTC epoch): %lu\n", currentTimestamp);
  }

  // Compute planned sleep and next wake based on current state/time
  int batteryPercent = estimateBatteryPercent(getStableBatteryVoltage());
  int sleepHours = determineSleepDuration(batteryPercent);
#if DEBUG_NO_DEEP_SLEEP
  sleepHours = 3;
#endif
  uint32_t nextWakeUtc = (currentTimestamp >= 24 * 3600 ? currentTimestamp : 0) + (uint32_t)sleepHours * 3600UL;
  float batteryDelta = getStableBatteryVoltage() - (g_prevBatteryVoltage > 0.1f ? g_prevBatteryVoltage : getStableBatteryVoltage());

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
    uptime,           // <-- new
    resetReason,       // <-- new
    String(""), String(""), String(""), 0, // net fields will be set later when connected
    rtcState.lastWaterTemp,
    sleepHours,
    nextWakeUtc,
    batteryDelta
  );

  // (Initial JSON suppressed; final JSON will be printed before upload)
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
  bool shouldProceedWithNewData = true;
  
  if (hasUnsentJson()) {
    SerialMon.println("Attempting to resend buffered unsent data...");
    // Network should already be connected from GPS attempt above
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
      SerialMon.println("Network not connected for buffered data.");
      markUploadFailed();
      shouldProceedWithNewData = false;
    }
  }

  // Only proceed with new data if buffered data was handled successfully
  if (shouldProceedWithNewData) {
    // Network should already be connected from GPS attempt above (established after GPS shutdown)
    
    // Time should already be synced from NTP during GPS flow

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
      // Refresh next wake with possibly updated timestamp
      nextWakeUtc = (currentTimestamp >= 24 * 3600 ? currentTimestamp : 0) + (uint32_t)sleepHours * 3600UL;
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
        uptime,
        resetReason,
        op,
        String(NETWORK_PROVIDER),
        ipStr,
        rssi,
        rtcState.lastWaterTemp,
        sleepHours,
        nextWakeUtc,
        batteryDelta
      );
      SerialMon.println("Final JSON (with network diagnostics):");
      SerialMon.println(json);
      SerialMon.println("=== CRITICAL: Attempting JSON upload ===");
      bool success = sendJsonToServer(
        API_SERVER,
        API_PORT,
        API_ENDPOINT,
        json
      );
      SerialMon.printf("=== CRITICAL: JSON upload result: %s ===\n", success ? "SUCCESS" : "FAILED");
      if (success) {
        markUploadSuccess();
        clearUnsentJson();
      } else {
        markUploadFailed();
        storeUnsentJson(json);
      }
      
      // Tear down cellular data after JSON upload and firmware check, wait 2 seconds
      SerialMon.println("Tearing down cellular data after upload...");
      delay(2000);  // 2 second delay as requested
    } else {
      SerialMon.println("Network connection failed.");
      markUploadFailed();
      storeUnsentJson(json);
    }
  }
  // Store planned sleep/wake info in RTC for next boot's wake reason context
  rtcState.lastSleepHours = (uint16_t)sleepHours;
  rtcState.lastNextWakeUtc = nextWakeUtc;

  SerialMon.printf("Sleeping for %d hour(s)...\n", sleepHours);
  if (rtcState.lastNextWakeUtc >= 24 * 3600) {
    struct tm tm_utc;
    time_t t = (time_t)rtcState.lastNextWakeUtc;
    gmtime_r(&t, &tm_utc);
    char whenBuf[32];
    strftime(whenBuf, sizeof(whenBuf), "%d/%m/%y - %H:%M", &tm_utc);
    SerialMon.printf("Next wake (UTC): %s\n", whenBuf);
  }
  delay(100);

  // Before sleep: cut power to 3.3V rail to disable GY-91 LED, wait 2 seconds
  powerOff3V3Rail();
  delay(2000);  // 2 second delay as requested
  
  // Before sleep: power down modem/GPS/Cellular data completely to conserve power
  powerOffModem();

#if DEBUG_NO_DEEP_SLEEP
  SerialMon.println("DEBUG_NO_DEEP_SLEEP active: staying awake and delaying instead of deep sleep.");
  // Stay awake but idle for the sleep duration, resetting WDT periodically
  uint32_t remainingMs = (uint32_t)sleepHours * 3600UL * 1000UL;
  const uint32_t chunkMs = 10000UL; // 10s chunks to keep logs responsive
  while (remainingMs > 0) {
    esp_task_wdt_reset();
    uint32_t d = remainingMs > chunkMs ? chunkMs : remainingMs;
    delay(d);
    remainingMs -= d;
  }
#else
  esp_sleep_enable_timer_wakeup((uint64_t)sleepHours * 3600ULL * 1000000ULL);
  esp_deep_sleep_start();
#endif
}
