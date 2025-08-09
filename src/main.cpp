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

#define MODEM_RX 26
#define MODEM_TX 27
#define MODEM_PWRKEY 12

#define SerialMon Serial
#define SerialAT Serial1

#define GPS_FIX_TIMEOUT_SEC 60

TinyGsm modem(SerialAT);

void powerOnModem() {
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(3000);
}

void powerOffModem() {
  // Power off modem using PWRKEY (if supported by your hardware)
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(1500); // Hold high for at least 1.2s to power off SIM7000G
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

  // Initialize watchdog timer (timeout 15 minutes = 900 seconds, panic on timeout)
  esp_task_wdt_init(900, true);
  esp_task_wdt_add(NULL); // Add current thread to WDT

  powerOnModem();
  SerialAT.begin(9600, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(3000);
  modem.restart();

  SerialMon.println("Modem Info: " + modem.getModemInfo());

  if (!beginSensors()) SerialMon.println("Sensor init failed.");
  if (!beginPowerMonitor()) SerialMon.println("Power monitor not detected.");

  checkBatteryChargeState();

  logBatteryStatus();  // New battery status logging here

  if (handleUndervoltageProtection()) {
    // Device will deep sleep if battery critically low
  }

  if (checkAndPerformOTA("http://your-server.com/firmware.bin")) {
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
    fix = getGpsFix(GPS_FIX_TIMEOUT_SEC);
    if (fix.success) {
      updateLastGpsFix(fix.latitude, fix.longitude, fix.fixTimeEpoch);
      checkAnchorDrift(fix.latitude, fix.longitude);
      // Sync RTC with GPS time
      if (fix.fixTimeEpoch > 1000000000) { // sanity check for valid epoch
        syncRtcWithGps(fix.fixTimeEpoch);
      }
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

  String json = buildJsonPayload(
    fix.latitude,
    fix.longitude,
    computeWaveHeight(),
    computeWavePeriod(),
    computeWaveDirection(),
    computeWavePower(computeWaveHeight(), computeWavePeriod()),
    getWaterTemperature(),
    readBatteryVoltage(),
    rtcState.lastGpsFixTime,
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
  if (hasUnsentJson()) {
    SerialMon.println("Attempting to resend buffered unsent data...");
    if (connectToNetwork("telenor.smart")) {
      bool success = sendJsonToServer(
        "playbuoyapi.no",
        443,
        "/upload",
        getUnsentJson()
      );
      if (success) {
        SerialMon.println("Buffered data upload successful.");
        clearUnsentJson();
        markUploadSuccess();
      } else {
        SerialMon.println("Buffered data upload failed, will retry next wakeup.");
        markUploadFailed();
        // Do not proceed with new data upload if old data is still pending
        goto sleep_now;
      }
    } else {
      SerialMon.println("Network connection failed for buffered data.");
      markUploadFailed();
      goto sleep_now;
    }
  }

  if (connectToNetwork("telenor.smart")) {
    bool success = sendJsonToServer(
      "playbuoyapi.no",
      443,
      "/upload",
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

sleep_now:
  int batteryPercent = estimateBatteryPercent(readBatteryVoltage());
  int sleepHours = determineSleepDuration(batteryPercent);

  SerialMon.printf("Sleeping for %d hour(s)...\n", sleepHours);
  delay(100);

  // powerOffModem();

  esp_sleep_enable_timer_wakeup((uint64_t)sleepHours * 3600ULL * 1000000ULL);
  esp_deep_sleep_start();
}
