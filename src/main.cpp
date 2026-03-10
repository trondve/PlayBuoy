#define TINY_GSM_MODEM_SIM7000
#define TINY_GSM_RX_BUFFER 1024

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <time.h>
#include <esp_system.h>
#include <math.h>
#include <algorithm>
#include <Wire.h>
#include <WiFi.h>
extern "C" {
  #include "esp_bt.h"
}
#include "driver/gpio.h"
#include "esp_sleep.h"



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

// Default DEBUG_NO_DEEP_SLEEP to 0 if not defined in config.h
#ifndef DEBUG_NO_DEEP_SLEEP
#define DEBUG_NO_DEEP_SLEEP 0
#endif

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
// GPS_POWER_PIN removed — was GPIO 4 (same as MODEM_PWRKEY), causing conflict.
// SIM7000G GNSS is internal, controlled via AT commands (AT+CGNSPWR, AT+SGPIO).

#define SerialMon Serial
#define SerialAT Serial1

TinyGsm modem(SerialAT);

// Track modem power/serial state to avoid powering early and to save battery
static bool g_modemReady = false;
static bool g_3v3RailPowered = false;
static bool g_sensorsPowered = false;
static bool g_sensorsInitialized = false;
static float g_prevBatteryVoltage = 0.0f;

// Forward declaration for functions defined later
void powerOnModem();
void powerOffModem();
void powerOn3V3Rail();
void powerOff3V3Rail();
void powerOnSensors();
void powerOffSensors();
// GPS power is controlled via AT commands in gps.cpp, not GPIO

// Put buses/pins into low-leakage state before deep sleep
void preparePinsAndSubsystemsForDeepSleep() {
  // Defensive radio off (WiFi already off, BT released at startup)
  WiFi.mode(WIFI_OFF);

  // I2C high-Z
  Wire.end();
  pinMode(21, INPUT);    // SDA
  pinMode(22, INPUT);    // SCL

  // OneWire line idle (assumes external pull-up present)
  pinMode(13, INPUT);

  // Modem UART and control lines to high-Z to avoid back-powering
  pinMode(MODEM_TX, INPUT);
  pinMode(MODEM_RX, INPUT);
  pinMode(MODEM_PWRKEY, INPUT);
  pinMode(MODEM_RST, INPUT);
  pinMode(MODEM_POWER_ON, INPUT);
  pinMode(MODEM_DTR, INPUT);
  pinMode(MODEM_RI, INPUT);

  // Ensure switched 3V3 rail stays LOW across deep sleep
  pinMode(POWER_3V3_ENABLE, OUTPUT);
  digitalWrite(POWER_3V3_ENABLE, LOW);
  gpio_hold_dis(GPIO_NUM_25);
  gpio_hold_en(GPIO_NUM_25);
  gpio_deep_sleep_hold_en();

  // Power domains: keep only RTC slow memory (holds RTC_DATA_ATTR), drop everything else
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,   ESP_PD_OPTION_OFF);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);   // rtcState lives here
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);  // not used, save ~0.5uA
  esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL,         ESP_PD_OPTION_OFF);  // save ~250uA
}

// Adjust next wake UTC to avoid 00:00-05:59 local; push to 06:00 if inside window
uint32_t adjustNextWakeUtcForQuietHours(uint32_t candidateUtc) {
  time_t cand = (time_t)candidateUtc;
  struct tm lt;
  localtime_r(&cand, &lt);   // uses configTzTime timezone
  if (lt.tm_hour >= 0 && lt.tm_hour < 6) {
    lt.tm_hour = 6;
    lt.tm_min = 0;
    lt.tm_sec = 0;
    time_t adj = mktime(&lt); // mktime interprets struct tm as local time, returns UTC epoch
    return (uint32_t)adj;
  }
  return candidateUtc;
}

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

  // Keep reset released (no pulse train for normal bring-up)
  digitalWrite(MODEM_RST, HIGH);
  delay(100);

  // Boot with PWRKEY low ~2.0s then high (conservative)
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(2000);
  digitalWrite(MODEM_PWRKEY, HIGH);

  // Modem ready for AT commands within ~5s after PWRKEY release (datasheet)
  SerialMon.println("Power sequence complete. Settling modem...");
  delay(6000);
}

// Ensure modem is awake before registration/attach (DTR LOW)
void wakeModemForNetwork() {
  // Conservative: ensure a definite wake edge and small settle
  digitalWrite(MODEM_DTR, HIGH);
  delay(50);
  digitalWrite(MODEM_DTR, LOW);
  delay(150);
}

void powerOffModem() {
  SerialMon.println("Powering off modem...");

  // Disable radio before power-off to cleanly deregister from network
  SerialAT.println("AT+CFUN=0");
  delay(1500);  // Wait for radio shutdown

  // Try graceful shutdown first (optional)
#if ENABLE_CPOWD_SHUTDOWN
  // Send CPOWD via raw AT if available
  extern HardwareSerial Serial1;
  Serial1.println("AT+CPOWD=1");
  unsigned long t0 = millis();
  bool normalDown = false;
  while (millis() - t0 < 8000) {
    while (Serial1.available()) {
      String line = Serial1.readStringUntil('\n');
      line.trim();
      if (line.indexOf("NORMAL POWER DOWN") >= 0) { normalDown = true; break; }
    }
    if (normalDown) break;
    delay(50);
  }
  if (!normalDown) {
#endif
  // Power off sequence (fallback) — datasheet requires PWRKEY LOW >= 1.2s
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1300);  // 1.3s LOW pulse (spec minimum: 1.2s)
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(1500);
  
  // Also power down the power control pin
  digitalWrite(MODEM_POWER_ON, LOW);
  digitalWrite(MODEM_DTR, HIGH);
  
  g_modemReady = false;
  SerialMon.println("Modem powered off.");
#if ENABLE_CPOWD_SHUTDOWN
  } else {
    digitalWrite(MODEM_POWER_ON, LOW);
    digitalWrite(MODEM_DTR, HIGH);
    g_modemReady = false;
    SerialMon.println("Modem powered off (CPOWD).");
  }
#endif
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

// powerOnGPS()/powerOffGPS() removed — GPIO 4 is MODEM_PWRKEY.
// Setting it HIGH/LOW from here corrupted modem power state.
// SIM7000G GNSS is internal, controlled via AT+CGNSPWR/AT+SGPIO in gps.cpp.

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

  // Permanently release Bluetooth radio and memory (~30KB freed, BT never used)
  btStop();
  esp_bt_controller_disable();
  esp_bt_controller_deinit();
  esp_bt_mem_release(ESP_BT_MODE_BTDM);

  logWakeupReason();  // Log why we woke up

  // Track brownout for fast-path sleep decision after battery measurement
  esp_reset_reason_t reason = esp_reset_reason();
  bool brownoutRecovery = (reason == ESP_RST_BROWNOUT);
  if (brownoutRecovery) {
    SerialMon.println("=== BROWNOUT RECOVERY DETECTED ===");
    SerialMon.println("Battery sagged under load — will check voltage and may sleep immediately.");
    SerialMon.println("================================");
  }

  // Release any deep-sleep holds from previous cycle before driving pins
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(GPIO_NUM_25);

  // Initialize power management pins
  pinMode(POWER_3V3_ENABLE, OUTPUT);
  digitalWrite(POWER_3V3_ENABLE, LOW);  // Start with 3.3V rail off
  g_3v3RailPowered = false;
  g_sensorsPowered = false;

  rtcStateBegin();

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
  // NOTE: Do NOT read temperature here — the 3V3 rail is off, DS18B20 is unpowered.
  // Temperature is read later in loop() after powering the 3V3 rail and initializing sensors.
  if (!beginPowerMonitor()) SerialMon.println("Power monitor init failed.");
  float stableBatteryVoltage = readBatteryVoltage();
  g_prevBatteryVoltage = rtcState.lastBatteryVoltage; // capture previous persisted value
  setStableBatteryVoltage(stableBatteryVoltage);
  rtcState.lastBatteryVoltage = stableBatteryVoltage;
  SerialMon.println("=== END BATTERY MEASUREMENT ===");

  checkBatteryChargeState();
  logBatteryStatus();

  // Brownout fast-track: if we just brown-out reset and battery is below 40%,
  // skip the full cycle and go straight to deep sleep to preserve the battery.
  // A brownout means the voltage sagged under load (modem 2A peak), so running
  // the full cycle with modem/GPS would likely cause another brownout.
  if (brownoutRecovery) {
    int pct = estimateBatteryPercent(stableBatteryVoltage);
    if (pct < 40) {
      SerialMon.printf("BROWNOUT + LOW BATTERY (%d%% / %.3fV) — sleeping immediately.\n",
                       pct, stableBatteryVoltage);
      int sleepHours = determineSleepDuration(pct);
      rtcState.lastSleepHours = (uint16_t)sleepHours;
      uint32_t sleepSec = (uint32_t)sleepHours * 3600UL;
      if (sleepSec < 300) sleepSec = 300;
      preparePinsAndSubsystemsForDeepSleep();
      esp_sleep_enable_timer_wakeup((uint64_t)sleepSec * 1000000ULL);
      esp_deep_sleep_start();
    } else {
      SerialMon.printf("Brownout recovery but battery OK (%d%%) — proceeding with full cycle.\n", pct);
    }
  }

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
  SerialMon.println("=== Starting wave data collection ===");

  // Power on 3.3V rail, wait for stabilization, then power on sensors
  powerOn3V3Rail();
  delay(150);  // LDO rail stabilizes in <10ms, DS18B20 needs ~50ms, 150ms is safe
  powerOnSensors();
  if (!g_sensorsInitialized) {
    if (!beginSensors()) SerialMon.println("Sensor init failed.");
    g_sensorsInitialized = true;
    float t0 = getWaterTemperature();
    if (!isnan(t0)) {
      rtcState.lastWaterTemp = t0;
      pushTemperatureHistory(t0);
    }
  }
  
  esp_task_wdt_reset();
  recordWaveData();
  esp_task_wdt_reset();
  logWaveStats();
  
  // Power off sensors, then power off 3.3V rail
  powerOffSensors();
  delay(100);  // brief settle before rail off (no datasheet requirement)
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
      // Always log drift (or no-previous-anchor) before overwriting stored anchor
      checkAnchorDrift(fix.latitude, fix.longitude);
      updateLastGpsFix(fix.latitude, fix.longitude, fix.fixTimeEpoch);
      rtcState.lastGpsHdop = fix.hdop;
      rtcState.lastGpsTtf = fix.ttfSeconds;
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
    // Turn off GNSS engine via AT command
    gpsEnd();
    delay(500);   // brief settle after GNSS stop before re-establishing cellular
    
    // Re-establish cellular data connection for firmware updates and JSON upload
    // Modem is already powered from GPS phase — skip pre-cycle to save ~14s
    SerialMon.println("Re-establishing cellular data connection for upload...");
    ensureModemReady();
    delay(1000);  // brief settle before connect (modem already warm from GPS)
    networkConnected = connectToNetwork(NETWORK_PROVIDER, true);

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
    delay(1000);  // brief settle before connect (modem just powered on)
    networkConnected = connectToNetwork(NETWORK_PROVIDER, true);
    
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
  uint32_t nowUtc = (uint32_t)time(NULL);
  uint32_t candidateWakeUtc = (currentTimestamp >= 24 * 3600 ? currentTimestamp : nowUtc) + (uint32_t)sleepHours * 3600UL;
  uint32_t nextWakeUtc = adjustNextWakeUtcForQuietHours(candidateWakeUtc);
  float batteryDelta = getStableBatteryVoltage() - (g_prevBatteryVoltage > 0.1f ? g_prevBatteryVoltage : getStableBatteryVoltage());

  // JSON is built once, after network connect attempt (see below)
  String json;
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
  clearFirmwareUpdateAttempted();

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

    // Check for firmware updates if network is still connected
    // Re-validate network status — connection may have dropped since initial check
    if (networkConnected && modem.isGprsConnected()) {
       SerialMon.printf(" OTA: OTA_SERVER = %s\n", OTA_SERVER);
       SerialMon.printf(" OTA: OTA_PATH = %s\n", OTA_PATH);
       SerialMon.printf(" OTA: NODE_ID = %s\n", NODE_ID);

       String baseUrl = "http://" + String(OTA_SERVER) + "/" + String(NODE_ID);
       SerialMon.printf(" OTA: Constructed baseUrl: %s\n", baseUrl.c_str());

       if (checkForFirmwareUpdate(baseUrl.c_str())) {
         // OTA update in progress, will restart on completion
       }
    } else if (networkConnected) {
       SerialMon.println("OTA skipped: network connection lost since registration");
    }
    
    // Build JSON once with network info if available, empty strings if not
    String op = "", ipStr = "";
    int rssi = 0;
    if (networkConnected) {
      // Refresh timestamp from network-synced RTC
      uint32_t ts = time(NULL);
      if (ts >= 24 * 3600) {
        currentTimestamp = ts;
      }
      op = modem.getOperator();
      ipStr = String((int)modem.localIP()[0]) + "." + String((int)modem.localIP()[1]) + "." + String((int)modem.localIP()[2]) + "." + String((int)modem.localIP()[3]);
      rssi = modem.getSignalQuality();
    }
    // Refresh next wake after potential time update and apply quiet-hours adjustment
    nowUtc = (uint32_t)time(NULL);
    candidateWakeUtc = (currentTimestamp >= 24 * 3600 ? currentTimestamp : nowUtc) + (uint32_t)sleepHours * 3600UL;
    nextWakeUtc = adjustNextWakeUtcForQuietHours(candidateWakeUtc);
    json = buildJsonPayload(
      fix.latitude,
      fix.longitude,
      computeWaveHeight(),
      computeWavePeriod(),
      computeWaveDirection(),
      computeWavePower(computeWaveHeight(), computeWavePeriod()),
      rtcState.lastWaterTemp,
      getStableBatteryVoltage(),
      currentTimestamp,
      NODE_ID,
      NAME,
      FIRMWARE_VERSION,
      uptime,
      resetReason,
      op,
      networkConnected ? String(NETWORK_PROVIDER) : String(""),
      ipStr,
      rssi,
      rtcState.lastWaterTemp,
      sleepHours,
      nextWakeUtc,
      batteryDelta
    );
    SerialMon.println("JSON payload:");
    SerialMon.println(json);

    if (networkConnected) {
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

      // Cellular data will be torn down when modem powers off before sleep
      SerialMon.println("Upload complete, proceeding to sleep.");
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
    struct tm tm_local;
    time_t t = (time_t)rtcState.lastNextWakeUtc;
    localtime_r(&t, &tm_local);
    char whenBuf[32];
    strftime(whenBuf, sizeof(whenBuf), "%d/%m/%y - %H:%M", &tm_local);
    SerialMon.printf("Next wake (Europe/Oslo): %s\n", whenBuf);
  }
  delay(100);

  // Ensure 3V3 rail is off (should already be off from sensor phase)
  powerOff3V3Rail();

  // Tear down PDP context before modem power-off to prevent registered-during-sleep leak.
  // Order: CNACT → CGACT → CGATT → CIPSHUT (same as gps.cpp tearDownPDP)
  if (g_modemReady) {
    SerialMon.println("Tearing down PDP context before sleep...");
    modem.sendAT("+CNACT=0,0");    modem.waitResponse(5000);
    modem.sendAT("+CGACT=0,1");    modem.waitResponse(5000);
    modem.sendAT("+CGATT=0");      modem.waitResponse(5000);
    modem.sendAT("+CIPSHUT");      modem.waitResponse(8000);
  }

  // Power down modem completely before sleep
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
  // Prepare all pins/subsystems for minimum leakage, then sleep
  preparePinsAndSubsystemsForDeepSleep();
  uint32_t sleepSec = 300; // minimum 5 minutes to prevent reboot loops
  nowUtc = (uint32_t)time(NULL);
  if (nextWakeUtc > nowUtc) sleepSec = nextWakeUtc - nowUtc;
  if (sleepSec < 300) sleepSec = 300; // enforce minimum sleep floor
  esp_sleep_enable_timer_wakeup((uint64_t)sleepSec * 1000000ULL);
  esp_deep_sleep_start();
#endif
}
