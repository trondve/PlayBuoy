#pragma once
#include <Arduino.h>

//
// Power management — battery monitoring and subsystem control.
// Measures battery voltage via ADC, controls power rails via GPIO,
// manages deep sleep configurations.
//
// BATTERY CHARACTERISTICS (Samsung INR18650-35E via LiitoKala Lii-35S):
// - Nominal voltage: 3.7V
// - Minimum: 3.55V (under 2A peak modem draw)
// - Maximum: 4.2V (fresh charge)
// - OCV table: 101 points (0-100% SoC)
// - Temperature compensation: +1.5mV per °C change
//

//
// Initializes ADC for battery voltage measurement (ESP32 ADC1, GPIO 36).
// Reads pin 36 (3.3V reference attenuated 1/3.6) connected to battery via divider.
// Calibration: reference voltage = 1.1V, full-scale (4095 ADC counts) = 3.96V.
// Returns: true if ADC initialized, false on error (shouldn't happen).
//
bool beginPowerMonitor();

//
// Reads stable battery voltage (single burst: 3 samples × 50 points, median-of-three).
// Burst duration ~30ms. Call early in boot cycle to minimize stale data.
// Returns: voltage in volts (typical: 3.5-4.2V), 0.0 on error.
// NOTE: Result is cached and used throughout boot cycle (see setStableBatteryVoltage).
//
float readBatteryVoltage();

//
// Power control helpers (defined in main.cpp, declared here for dependency injection).
//

// Powers down 3.3V rail (GPIO 25 LOW).
// Disables sensors, temperature sensor, IMU.
// Safe to call multiple times (idempotent).
void powerOff3V3Rail();

// Powers down modem (GPIO 23 + 4 LOW, respects SIM7000G timing spec ~1.3s).
// Tears down serial, releases TinyGsm resources.
// Must call before deep sleep to save battery.
void powerOffModem();

// Configures all GPIO and peripherals for minimum power leakage during sleep.
// Sets all controlled pins to INPUT (high-Z) to prevent back-powering,
// disables I2C/OneWire, releases WiFi and Bluetooth.
// Must call before esp_deep_sleep_start().
void preparePinsAndSubsystemsForDeepSleep();

//
// Sleep scheduling helper.
//

// Adjusts next wake time to avoid "quiet hours" (00:00-05:59 local time, CET/CEST).
// If candidateUtc falls in quiet window, pushes wake to 06:00 local next day.
// Used to minimize API load during European nighttime.
// Returns: adjusted UTC epoch (unchanged if outside quiet window, or 06:00 if inside).
// NOTE: H-01 fix: DST transition handled via mktime auto-detect (tm_isdst = -1).
uint32_t adjustNextWakeUtcForQuietHours(uint32_t candidateUtc);