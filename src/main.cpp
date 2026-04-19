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
  localtime_r(&cand, &lt);   // convert to local time (uses configTzTime timezone)

  if (lt.tm_hour >= 0 && lt.tm_hour < 6) {
    // Wake falls in quiet hours (00:00-06:00 local); defer to 06:00 local
    lt.tm_hour = 6;
    lt.tm_min = 0;
    lt.tm_sec = 0;

    // Reset DST flag to -1 (auto-detect) to ensure mktime computes correct interpretation for 06:00 on DST transition days
    // On spring-forward (Mar 29), 06:00 is unambiguous (either 06:00 CET or 06:00 CEST depending on hour)
    // On fall-back (Oct 25), 06:00 is unambiguous (only one 06:00 local)
    lt.tm_isdst = -1;

    time_t adj = mktime(&lt); // mktime interprets struct tm as local time, returns UTC epoch
    if (adj == (time_t)-1) return candidateUtc; // mktime failed, keep original

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
        if (rtcState.lastSleepMinutes > 0) {
          uint32_t m = rtcState.lastSleepMinutes;
          if (m >= 60)
            return String("WokeUpFromTimerSleep(") + String(m / 60) + "h" + String(m % 60) + "m)";
          else
            return String("WokeUpFromTimerSleep(") + String(m) + "m)";
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
  delay(500);  // 500ms is sufficient for USB-CDC enumeration; 3000ms wasted ~0.15mAh per boot

  SerialMon.println("\n========== BOOT CYCLE STARTED ==========");
  SerialMon.println("Step 1: Disabling Bluetooth and releasing memory...");

  // Permanently release Bluetooth radio and memory (~30KB freed, BT never used)
  btStop();
  esp_bt_controller_disable();
  esp_bt_controller_deinit();
  esp_bt_mem_release(ESP_BT_MODE_BTDM);
  SerialMon.println("✓ Bluetooth disabled, ~30KB memory freed");

  SerialMon.println("Step 2: Logging wakeup reason...");
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
  SerialMon.println("Step 3: Releasing GPIO deep-sleep hold (GPIO 25 3.3V rail control)...");
  gpio_deep_sleep_hold_dis();
  esp_err_t holdErr = gpio_hold_dis(GPIO_NUM_25);
  if (holdErr != ESP_OK) {
    SerialMon.printf("⚠ WARNING: GPIO 25 hold release failed (err=%d). Retrying once...\n", holdErr);
    holdErr = gpio_hold_dis(GPIO_NUM_25);
    if (holdErr != ESP_OK) {
      SerialMon.printf("✗ ERROR: GPIO 25 hold release FAILED permanently. Sensors may not power up.\n");
      // Don't return — try to proceed anyway, but mark that this cycle may be broken
    }
  } else {
    SerialMon.println("✓ GPIO 25 hold released successfully");
  }

  // Initialize power management pins
  SerialMon.println("Step 4: Initializing power management pins...");
  pinMode(POWER_3V3_ENABLE, OUTPUT);
  digitalWrite(POWER_3V3_ENABLE, LOW);  // Start with 3.3V rail off
  g_3v3RailPowered = false;
  g_sensorsPowered = false;
  SerialMon.println("✓ Power management pins initialized (3.3V rail OFF)");

  SerialMon.println("Step 5: Initializing RTC state management...");
  rtcStateBegin();
  SerialMon.println("✓ RTC state initialized");

  SerialMon.println("Step 6: Checking OTA rollback state...");
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t otaState;
  if (esp_ota_get_state_partition(running, &otaState) == ESP_OK) {
    if (otaState == ESP_OTA_IMG_PENDING_VERIFY) {
      SerialMon.println("⚠ OTA image pending verify (rollback enabled). Will mark valid after successful run.");
    }
  }

  SerialMon.println("Step 7: Initializing watchdog timer (45 minutes timeout)...");
  // Initialize watchdog timer (timeout 45 minutes = 2700 seconds, panic on timeout)
  esp_task_wdt_init(2700, true);
  esp_task_wdt_add(NULL); // Add current thread to WDT
  SerialMon.println("✓ Watchdog initialized (45 min timeout)");

  SerialMon.println("Step 8: Configuring RTC timezone (CET/CEST)...");
  // Initialize RTC timezone. We'll sync time from GPS, and if that fails, try HTTP Date header.
  configTzTime(TIMEZONE, NTP_SERVER);
  SerialMon.println("✓ RTC timezone configured (CET/CEST)");
  

  SerialMon.println("Step 9: Measuring battery voltage (early, before any subsystems)...");
  // Measure battery early using the new method and store as stable value
  // NOTE: Do NOT read temperature here — the 3V3 rail is off, DS18B20 is unpowered.
  // Temperature is read later in loop() after powering the 3V3 rail and initializing sensors.
  if (!beginPowerMonitor()) {
    SerialMon.println("✗ Power monitor init failed.");
  } else {
    SerialMon.println("✓ Power monitor initialized");
  }
  float stableBatteryVoltage = readBatteryVoltage();
  g_prevBatteryVoltage = rtcState.lastBatteryVoltage; // capture previous persisted value
  setStableBatteryVoltage(stableBatteryVoltage);
  SerialMon.printf("✓ Battery voltage measured: %.3fV\n", stableBatteryVoltage);
  // NOTE: rtcState.lastBatteryVoltage is NOT updated here — it still holds the
  // previous cycle's voltage, used by determineSleepDuration() for SoC hysteresis.
  // It gets updated after the sleep decision in loop().

  checkBatteryChargeState();
  logBatteryStatus();

  SerialMon.println("Step 10: Evaluating brownout recovery path...");
  // Brownout fast-track: if we just brown-out reset and battery is below threshold,
  // skip the full cycle and go straight to deep sleep to preserve the battery.
  // A brownout means the voltage sagged under load (modem 2A peak), so running
  // the full cycle with modem/GPS would likely cause another brownout.
  if (brownoutRecovery) {
    int pct = estimateBatteryPercent(stableBatteryVoltage);
    if (pct < BROWNOUT_SKIP_PCT) {
      SerialMon.printf("✗ BROWNOUT + LOW BATTERY (%d%% / %.3fV) — skipping full cycle, sleeping immediately.\n",
                       pct, stableBatteryVoltage);
      // fastPath=true: skip 10s RTC retry loop (NTP hasn't run yet)
      int sleepMinutes = determineSleepDuration(pct, true);
      SerialMon.printf("  Sleep duration: %d minutes\n", sleepMinutes);
      rtcState.lastBatteryVoltage = stableBatteryVoltage; // persist for next boot's hysteresis
      rtcState.lastSleepMinutes = (uint32_t)sleepMinutes;
      // Apply quiet hours adjustment (same as normal path)
      uint32_t now = (uint32_t)time(NULL);
      uint32_t candidate = (now >= SECONDS_PER_DAY ? now : 0) + (uint32_t)sleepMinutes * 60UL;
      uint32_t nextWake = adjustNextWakeUtcForQuietHours(candidate);
      rtcState.lastNextWakeUtc = nextWake;
      uint32_t sleepSec = 300;
      now = (uint32_t)time(NULL);
      if (nextWake > now) sleepSec = nextWake - now;
      if (sleepSec < 300) sleepSec = 300;
      SerialMon.printf("  Next wake: %lu (in %u seconds)\n", nextWake, sleepSec);
      SerialMon.println("  Preparing pins for deep sleep...");
      preparePinsAndSubsystemsForDeepSleep();
      esp_sleep_enable_timer_wakeup((uint64_t)sleepSec * 1000000ULL);
      SerialMon.println("  Entering deep sleep (brownout fast-path)...");
      esp_deep_sleep_start();
    } else {
      SerialMon.printf("✓ Brownout recovery but battery adequate (%d%%) — proceeding with full cycle.\n", pct);
    }
  }

  SerialMon.println("Step 11: Checking for critical undervoltage protection cutoff...");
  if (handleUndervoltageProtection()) {
    // Device will deep sleep if battery critically low
    SerialMon.println("✗ Undervoltage protection triggered - entering sleep/low-power mode");
  } else {
    SerialMon.println("✓ Undervoltage check passed - proceeding to main loop");
  }
  SerialMon.println("========== SETUP COMPLETE, ENTERING MAIN LOOP ==========\n");
}

void loop() {
  SerialMon.println("\n========== MAIN LOOP START ==========");
  // Feed the watchdog at the start of each loop
  esp_task_wdt_reset();
  SerialMon.println("✓ Watchdog reset");

  uint32_t now = time(NULL);

  SerialMon.println("\n--- PHASE 1: GPS DECISION LOGIC ---");
  bool shouldGetNewGpsFix = true;
  if (rtcState.lastGpsFixTime > 1000000000) {  // sanity check for valid epoch time
    uint32_t age = now - rtcState.lastGpsFixTime;
    if (age < GPS_SYNC_INTERVAL_SECONDS) {
      SerialMon.printf("ℹ Last GPS fix is recent (%u seconds ago). Skipping new fix.\n", age);
      shouldGetNewGpsFix = false;
    } else {
      SerialMon.printf("GPS fix is stale (%u seconds old, threshold %u). Will attempt new fix.\n", age, GPS_SYNC_INTERVAL_SECONDS);
    }
  } else {
    SerialMon.println("No previous GPS fix found. Will attempt first fix (cold-start).");
  }

  // 1) Collect wave data first (modem is still off for lower power)
  SerialMon.println("\n--- PHASE 2: WAVE DATA COLLECTION ---");
  SerialMon.println("Starting IMU sampling for wave spectral analysis...");

  SerialMon.println("  Powering on 3.3V rail (sensors)...");
  // Power on 3.3V rail, wait for stabilization, then power on sensors
  powerOn3V3Rail();
  delay(150);  // LDO rail stabilizes in <10ms, DS18B20 needs ~50ms, 150ms is safe
  SerialMon.println("  ✓ 3.3V rail powered");

  SerialMon.println("  Initializing IMU and temperature sensor...");
  powerOnSensors();
  if (!g_sensorsInitialized) {
    if (!beginSensors()) {
      SerialMon.println("  ✗ Sensor initialization failed.");
    } else {
      SerialMon.println("  ✓ Sensors initialized");
    }
    g_sensorsInitialized = true;
    float t0 = getWaterTemperature();
    if (!isnan(t0)) {
      SerialMon.printf("  ✓ Initial water temperature: %.2f°C\n", t0);
      rtcState.lastWaterTemp = t0;
      pushTemperatureHistory(t0);
    } else {
      SerialMon.println("  ⚠ Water temperature invalid on first read (warming up)");
    }
  }

  SerialMon.println("  Collecting wave data (10Hz IMU for 100s)...");
  esp_task_wdt_reset();
  recordWaveData();
  SerialMon.println("  ✓ Wave data collection complete");

  esp_task_wdt_reset();
  logWaveStats();

  SerialMon.println("  Powering down sensors and 3.3V rail...");
  // Power off sensors, then power off 3.3V rail
  powerOffSensors();
  delay(100);  // brief settle before rail off (no datasheet requirement)
  powerOff3V3Rail();
  SerialMon.println("  ✓ Sensors and 3.3V rail powered down");
  SerialMon.println("✓ PHASE 2 COMPLETE: Wave data collection finished\n");

  // 2) Skip pre-GPS HTTP time sync; GPS flow will do NTP + XTRA
  bool networkConnected = false;

  // 3) Then attempt GPS (now with valid time)
  // OPTIMIZED: Smart timeout based on battery level and first fix status
  SerialMon.println("\n--- PHASE 3: GNSS POSITIONING AND NTP/XTRA SYNC ---");
  GpsFixResult fix;
  if (shouldGetNewGpsFix) {
    bool isFirstFix = (rtcState.lastGpsFixTime == 0);
    SerialMon.printf("Attempting GPS fix (%s)...\n", isFirstFix ? "COLD-START" : "warm start");

    // Bring up modem just before NTP/XTRA/GPS flow to save power
    // CGNSMOD is configured inside gps.cpp gnssConfigure() — no need to set it here
    SerialMon.println("  Powering on modem...");
    ensureModemReady();
    SerialMon.println("  ✓ Modem ready");

    SerialMon.println("  Running GNSS acquisition (NTP sync → XTRA download → 60s warmup → fix polling)...");
    fix = getGpsFixDynamic(isFirstFix);
    if (fix.success) {
      SerialMon.printf("  ✓ GPS FIX ACQUIRED: lat=%.6f, lon=%.6f, HDOP=%.1f, TTF=%u seconds\n",
                       fix.latitude, fix.longitude, fix.hdop, fix.ttfSeconds);
      // Always log drift (or no-previous-anchor) before overwriting stored anchor
      checkAnchorDrift(fix.latitude, fix.longitude);
      updateLastGpsFix(fix.latitude, fix.longitude, fix.fixTimeEpoch);
      rtcState.lastGpsHdop = fix.hdop;
      rtcState.lastGpsTtf = fix.ttfSeconds;
      if (fix.fixTimeEpoch > 1000000000) {
        syncRtcWithGps(fix.fixTimeEpoch);
        SerialMon.printf("  ✓ RTC synchronized with GPS time\n");
      }
    } else {
      SerialMon.println("  ✗ GPS fix failed");
      if (rtcState.lastGpsFixTime > 1000000000) {
        uint32_t elapsedSinceLastFix = time(NULL) - rtcState.lastGpsFixTime;
        uint32_t currentTime = rtcState.lastGpsFixTime + elapsedSinceLastFix;
        syncRtcWithGps(currentTime);
        SerialMon.printf("  Using last GPS fix (%.6f, %.6f) from %u seconds ago\n",
                         rtcState.lastGpsLat, rtcState.lastGpsLon, elapsedSinceLastFix);
      }
      fix.latitude = rtcState.lastGpsLat;
      fix.longitude = rtcState.lastGpsLon;
      fix.fixTimeEpoch = rtcState.lastGpsFixTime;
      fix.success = false;
    }
    // Turn off GNSS engine via AT command
    SerialMon.println("  Shutting down GNSS engine...");
    gpsEnd();
    delay(500);   // brief settle after GNSS stop before re-establishing cellular
    SerialMon.println("  ✓ GNSS engine stopped");

    // Re-establish cellular data connection for firmware updates and JSON upload
    // Modem is already powered from GPS phase — skip pre-cycle to save ~14s
    SerialMon.println("  Re-establishing cellular data connection (modem already warm)...");
    ensureModemReady();
    delay(1000);  // brief settle before connect (modem already warm from GPS)
    networkConnected = connectToNetwork(NETWORK_PROVIDER, true);

    // If regular connection fails, try APN testing
    if (!networkConnected) {
      SerialMon.println("  ⚠ Regular connection failed, testing multiple APNs...");
      networkConnected = testMultipleAPNs();
    }
    if (networkConnected) {
      SerialMon.println("  ✓ Cellular connection established");
    } else {
      SerialMon.println("  ✗ Cellular connection failed");
    }
  } else {
    SerialMon.println("Skipping GPS (recent fix available), using cached coordinates...");
    fix.latitude = rtcState.lastGpsLat;
    fix.longitude = rtcState.lastGpsLon;
    fix.fixTimeEpoch = rtcState.lastGpsFixTime;
    fix.success = true;

    // GPS was skipped, but we still need cellular data for firmware updates and JSON upload
    SerialMon.println("Establishing cellular data connection for upload (fresh modem power-on)...");
    ensureModemReady();
    delay(1000);  // brief settle before connect (modem just powered on)
    networkConnected = connectToNetwork(NETWORK_PROVIDER, true);

    // If regular connection fails, try APN testing
    if (!networkConnected) {
      SerialMon.println("  ⚠ Regular connection failed, testing multiple APNs...");
      networkConnected = testMultipleAPNs();
    }
    if (networkConnected) {
      SerialMon.println("  ✓ Cellular connection established");
    } else {
      SerialMon.println("  ✗ Cellular connection failed");
    }
  }

  SerialMon.println("\n--- PHASE 4: TEMPERATURE ANOMALY CHECK ---");
  SerialMon.println("Evaluating water temperature spike/trend detection...");
  checkTemperatureAnomalies();
  SerialMon.println("✓ Temperature anomaly check complete");

  logRtcState();

  uint32_t uptime = millis() / 1000; // seconds since boot
  String resetReason = getResetReasonString();

  // Skip legacy HTTP time sync; RTC was set during GPS NTP step if available

  SerialMon.println("\n--- PHASE 5: TIMESTAMP AND BATTERY RE-CHECK ---");
  // Get current timestamp from RTC, with fallback to GPS time
  uint32_t currentTimestamp = time(NULL);
  if (currentTimestamp < SECONDS_PER_DAY) {  // If RTC time is not valid
    if (rtcState.lastGpsFixTime > 1000000000) {
      // Use last GPS time as fallback
      currentTimestamp = rtcState.lastGpsFixTime;
      SerialMon.printf("⚠ RTC invalid, using last GPS time as fallback: %lu\n", currentTimestamp);
    } else {
      // No valid time available
      currentTimestamp = 0;
      SerialMon.println("✗ No valid timestamp available, using 0");
    }
  } else {
    SerialMon.printf("✓ Using RTC timestamp (UTC epoch): %lu\n", currentTimestamp);
  }

  // Re-measure battery right before sleep decision for most accurate SoC.
  // The initial measurement was taken before modem/GPS (~30 minutes ago).
  SerialMon.println("Re-measuring battery voltage (after modem/GPS activities)...");
  {
    float freshVoltage = readBatteryVoltage();
    SerialMon.printf("  Battery: %.3fV (initial boot: %.3fV, delta: %+.3fV)\n",
                     freshVoltage, getStableBatteryVoltage(),
                     freshVoltage - getStableBatteryVoltage());
    setStableBatteryVoltage(freshVoltage);
  }

  // Compute planned sleep and next wake based on current state/time
  SerialMon.println("Computing sleep schedule based on SoC and season...");
  int batteryPercent = estimateBatteryPercent(getStableBatteryVoltage());
  int sleepMinutes = determineSleepDuration(batteryPercent);
  SerialMon.printf("  Battery SoC: %d%%, Sleep duration: %d minutes\n", batteryPercent, sleepMinutes);
  // Now that hysteresis has used the previous cycle's voltage, persist the fresh one
  rtcState.lastBatteryVoltage = getStableBatteryVoltage();
#if DEBUG_NO_DEEP_SLEEP
  sleepMinutes = 180;
#endif
  uint32_t nowUtc = (uint32_t)time(NULL);
  uint32_t candidateWakeUtc = (currentTimestamp >= SECONDS_PER_DAY ? currentTimestamp : nowUtc) + (uint32_t)sleepMinutes * 60UL;
  uint32_t nextWakeUtc = adjustNextWakeUtcForQuietHours(candidateWakeUtc);
  float batteryDelta = getStableBatteryVoltage() - (g_prevBatteryVoltage > 0.1f ? g_prevBatteryVoltage : getStableBatteryVoltage());

  SerialMon.println("\n--- PHASE 6: JSON PAYLOAD CONSTRUCTION AND UPLOAD ---");
  // JSON is built once, after network connect attempt (see below)
  String json;
  // Print a human-friendly current local date/time
  time_t nowTs = time(NULL);
  if (nowTs >= SECONDS_PER_DAY) {
    struct tm lt; localtime_r(&nowTs, &lt);
    char buf[64]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &lt);
    SerialMon.printf("Current local time: %s\n", buf);
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

  SerialMon.println("Checking for buffered data from previous failed uploads...");
  if (hasUnsentJson()) {
    SerialMon.println("✓ Found buffered unsent data - attempting to resend...");
    // Network should already be connected from GPS attempt above
    if (networkConnected) {
      bool success = sendJsonToServer(
        API_SERVER,
        API_PORT,
        API_ENDPOINT,
        getUnsentJson()
      );
      if (success) {
        SerialMon.println("✓ Buffered data upload successful.");
        clearUnsentJson();
        markUploadSuccess();
        bufferedDataSent = true;
      } else {
        SerialMon.println("✗ Buffered data upload failed - will retry next cycle.");
        markUploadFailed();
        shouldProceedWithNewData = false;
      }
    } else {
      SerialMon.println("✗ Network not connected - cannot upload buffered data.");
      markUploadFailed();
      shouldProceedWithNewData = false;
    }
  } else {
    SerialMon.println("✓ No buffered data waiting");
  }

  // Only proceed with new data if buffered data was handled successfully
  if (shouldProceedWithNewData) {
    // Network should already be connected from GPS attempt above (established after GPS shutdown)
    
    // Time should already be synced from NTP during GPS flow

    // Check for firmware updates if network is still connected
    // Re-validate network status — connection may have dropped since initial check
    SerialMon.println("Checking for firmware updates (OTA)...");
    if (networkConnected && modem.isGprsConnected()) {
       SerialMon.printf("  OTA server: %s\n", OTA_SERVER);
       SerialMon.printf("  OTA path: %s\n", OTA_PATH);
       SerialMon.printf("  Node ID: %s\n", NODE_ID);

       String baseUrl = "http://" + String(OTA_SERVER) + "/" + String(NODE_ID);
       SerialMon.printf("  Checking for updates at: %s\n", baseUrl.c_str());

       if (checkForFirmwareUpdate(baseUrl.c_str())) {
         // OTA update in progress, will restart on completion
         SerialMon.println("✓ OTA update in progress - will restart on completion");
       } else {
         SerialMon.println("  No firmware update needed (version current)");
       }
    } else if (networkConnected) {
       SerialMon.println("⚠ OTA skipped: network connection lost since registration");
    } else {
       SerialMon.println("⊘ OTA skipped: no network connection");
    }
    
    SerialMon.println("Building JSON payload with current measurements...");
    // Build JSON once with network info if available, empty strings if not
    String op = "", ipStr = "";
    int rssi = 0;
    if (networkConnected) {
      // Refresh timestamp from network-synced RTC
      uint32_t ts = time(NULL);
      if (ts >= SECONDS_PER_DAY) {
        currentTimestamp = ts;
        SerialMon.printf("  Refreshed timestamp from network-synced RTC: %lu\n", currentTimestamp);
      }
      op = modem.getOperator();
      { IPAddress lip = modem.localIP(); ipStr = String((int)lip[0]) + "." + String((int)lip[1]) + "." + String((int)lip[2]) + "." + String((int)lip[3]); }
      rssi = modem.getSignalQuality();
      SerialMon.printf("  Network: %s, IP: %s, Signal: %d%%\n", op.c_str(), ipStr.c_str(), rssi);
    } else {
      SerialMon.println("  (No network - using cached values)");
    }
    // Refresh next wake after potential time update and apply quiet-hours adjustment
    nowUtc = (uint32_t)time(NULL);
    candidateWakeUtc = (currentTimestamp >= SECONDS_PER_DAY ? currentTimestamp : nowUtc) + (uint32_t)sleepMinutes * 60UL;
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
      sleepMinutes,
      nextWakeUtc,
      batteryDelta
    );
    SerialMon.println("  JSON payload constructed:");
    SerialMon.println("  " + json);
    SerialMon.printf("  Payload size: %u bytes\n", json.length());

    if (networkConnected) {
      SerialMon.println("✓ Attempting JSON upload to API server...");
      SerialMon.printf("  Target: %s:%d%s\n", API_SERVER, API_PORT, API_ENDPOINT);
      bool success = sendJsonToServer(
        API_SERVER,
        API_PORT,
        API_ENDPOINT,
        json
      );
      if (success) {
        SerialMon.println("✓✓✓ JSON UPLOAD SUCCESSFUL ✓✓✓");
        markUploadSuccess();
        clearUnsentJson();
      } else {
        SerialMon.println("✗ JSON UPLOAD FAILED - buffering for retry on next cycle");
        markUploadFailed();
        storeUnsentJson(json);
      }

      // Cellular data will be torn down when modem powers off before sleep
    } else {
      SerialMon.println("✗ Network connection failed - cannot upload JSON");
      markUploadFailed();
      storeUnsentJson(json);
    }
  } else {
    SerialMon.println("Skipping new data upload (buffered data upload failed or in progress)");
  }
  SerialMon.println("\n--- PHASE 7: SLEEP PREPARATION AND POWER-DOWN ---");
  // Store planned sleep/wake info in RTC for next boot's wake reason context
  rtcState.lastSleepMinutes = (uint32_t)sleepMinutes;
  rtcState.lastNextWakeUtc = nextWakeUtc;

  SerialMon.println("Sleep and wake schedule:");
  if (sleepMinutes >= 60) {
    SerialMon.printf("  Duration: %dh %dm (%u total minutes)\n", sleepMinutes / 60, sleepMinutes % 60, sleepMinutes);
  } else {
    SerialMon.printf("  Duration: %d minute(s)\n", sleepMinutes);
  }
  if (rtcState.lastNextWakeUtc >= SECONDS_PER_DAY) {
    struct tm tm_local;
    time_t t = (time_t)rtcState.lastNextWakeUtc;
    localtime_r(&t, &tm_local);
    char whenBuf[32];
    strftime(whenBuf, sizeof(whenBuf), "%d/%m/%y - %H:%M:%S", &tm_local);
    SerialMon.printf("  Next wake (local): %s\n", whenBuf);
    SerialMon.printf("  Next wake (UTC): %lu\n", rtcState.lastNextWakeUtc);
  }
  delay(100);

  SerialMon.println("Shutting down subsystems...");

  // Ensure 3V3 rail is off (should already be off from sensor phase)
  SerialMon.println("  Powering down 3.3V rail (sensors)...");
  powerOff3V3Rail();
  SerialMon.println("  ✓ 3.3V rail powered down");

  // Tear down PDP context before modem power-off to prevent registered-during-sleep leak.
  // Order: CNACT → CGACT → CGATT → CIPSHUT (same as gps.cpp tearDownPDP)
  if (g_modemReady) {
    SerialMon.println("  Tearing down PDP context (preventing radio registration leak)...");
    modem.sendAT("+CNACT=0,0");    modem.waitResponse(5000);
    modem.sendAT("+CGACT=0,1");    modem.waitResponse(5000);
    modem.sendAT("+CGATT=0");      modem.waitResponse(5000);
    modem.sendAT("+CIPSHUT");      modem.waitResponse(8000);
    SerialMon.println("  ✓ PDP context torn down");
  }

  // Power down modem completely before sleep
  SerialMon.println("  Powering down modem...");
  powerOffModem();
  SerialMon.println("  ✓ Modem powered down");

#if DEBUG_NO_DEEP_SLEEP
  SerialMon.println("\n⚠ DEBUG_NO_DEEP_SLEEP ACTIVE: Staying awake and delaying instead of deep sleep");
  SerialMon.println("(This is for debugging only - in production, device enters deep sleep here)\n");
  // Stay awake but idle for the sleep duration, resetting WDT periodically
  uint64_t remainingMs = (uint64_t)sleepMinutes * 60ULL * 1000ULL; // uint64 avoids overflow at 129600 min
  const uint32_t chunkMs = 10000UL; // 10s chunks to keep logs responsive
  while (remainingMs > 0) {
    esp_task_wdt_reset();
    uint32_t d = (remainingMs > chunkMs) ? chunkMs : (uint32_t)remainingMs;
    delay(d);
    remainingMs -= d;
    SerialMon.printf("  [DEBUG delay] %llu ms remaining...\n", remainingMs);
  }
  SerialMon.println("\n✓ DEBUG_NO_DEEP_SLEEP wake timeout complete - returning to setup() for next cycle\n");
#else
  // Prepare all pins/subsystems for minimum leakage, then sleep
  SerialMon.println("  Preparing pins and peripherals for minimum power leakage...");
  preparePinsAndSubsystemsForDeepSleep();
  SerialMon.println("  ✓ System in minimum-leakage state");

  uint32_t sleepSec = 300; // minimum 5 minutes to prevent reboot loops
  nowUtc = (uint32_t)time(NULL);
  if (nextWakeUtc > nowUtc) sleepSec = nextWakeUtc - nowUtc;
  if (sleepSec < 300) sleepSec = 300; // enforce minimum sleep floor

  SerialMon.printf("\n========== ENTERING DEEP SLEEP ==========\n");
  SerialMon.printf("  Sleep duration: %u seconds (from RTC epoch %lu)\n", sleepSec, nextWakeUtc);
  SerialMon.printf("  Current UTC: %lu\n", nowUtc);
  SerialMon.printf("  GPIO 25 (3V3 hold): Will be enabled in deep sleep\n");
  SerialMon.printf("  All peripheral power: OFF\n");
  SerialMon.printf("  Modem: OFF\n");
  SerialMon.printf("  Next wake: RTC timer in %u seconds\n", sleepSec);
  SerialMon.flush();

  esp_sleep_enable_timer_wakeup((uint64_t)sleepSec * 1000000ULL);
  esp_deep_sleep_start();
  // Code will NOT reach here - esp_deep_sleep_start() does not return
#endif
}
