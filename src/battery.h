#pragma once
#include <Arduino.h>

//
// Battery state-of-charge (SoC) estimation and sleep scheduling.
// Uses Samsung INR18650-35E OCV lookup table (101 points, 0-100%).
// Implements temperature-compensated SoC, hysteresis anti-oscillation,
// and season-aware sleep duration calculation.
//
// SAFETY CRITICAL THRESHOLDS:
// - ≤3.70V → critical battery cutoff (protects modem minimum 3.55V under 2A peak)
// - ≤25% SoC → deep sleep (skip modem/GPS to preserve energy)
// - Brownout + <40% → skip full cycle, return to sleep immediately
// - Season-aware: winter 90-day sleep, summer 30-min wake cycle
//

//
// Detects charge state changes via hysteresis (±30mV around 3.7V threshold).
// Sets isCharging flag, used to detect "charging lost" condition.
// Must be called early in boot cycle (after readBatteryVoltage).
// No return value (updates internal state, logs if needed).
//
void checkBatteryChargeState();

//
// Critical battery protection: if voltage ≤3.70V or SoC ≤ critical percent,
// immediately enters deep sleep (skips modem, GPS, upload).
// Pre-compute conservative sleep duration, return to sleep mode.
// Returns: true if protection triggered (device will deep sleep), false if voltage OK.
// CRITICAL: must be called early in setup() before any subsystem powers on.
//
bool handleUndervoltageProtection();

//
// Estimates state-of-charge (0-100%) from battery voltage.
// Uses 101-point OCV table for Samsung INR18650-35E (LiitoKala Lii-35S).
// Includes temperature compensation: +1.5mV/°C for cold water (<10°C).
// Uses binary search in OCV table for O(log n) lookup, linear interpolation between points.
// Returns: 0-100 integer percentage, or edge values if outside table range.
// NOTES:
// - M-02: Temperature compensation prevents over-reporting SoC at cold temps
// - M-05: Epsilon guard 1e-3f (1 mV) prevents div-by-zero in flat voltage bands
// - Flat plateau 3.55-3.75V covers ~20-55% SoC (normal for energy cells)
//
int estimateBatteryPercent(float voltage);

//
// Determines next sleep duration (minutes) based on battery SoC and season.
// THREE-SEASON MODEL:
// - Winter (Oct-Mar, 59°N Norway): 90-day hibernation (minimal solar, darkness)
// - Shoulder (Apr-May, Sep): 2-hour wake cycles (transitional light)
// - Summer (Jun-Aug): 30-minute wake cycles (long daylight, plenty of power)
// SoC MODULATION: high battery (>60%) accelerates cycle; low (<40%) extends sleep.
// fastPath: if true, skips RTC retry loop (used by brownout fast-path before NTP).
// Returns: sleep minutes (300-129600), respects minimum 5-minute floor.
//
int determineSleepDuration(int batteryPercent, bool fastPath = false);

//
// Logs battery status (voltage, SoC%, hysteresis state) to Serial.
// Called in boot() after battery measurement and before cycle decision.
// Diagnostic output only (no side effects).
//
void logBatteryStatus();

//
// Caches battery voltage measured at boot for use throughout cycle.
// Voltage is measured once at startup, reused for all SoC calculations.
// Prevents mid-cycle re-measurement which could cause hysteresis flip.
//
void setStableBatteryVoltage(float voltage);

//
// Retrieves cached stable battery voltage (measured at boot).
// Used by all SoC and charge-state functions to ensure consistency.
//
float getStableBatteryVoltage();

//
// Power control and sleep planning helpers (defined in main.cpp).
//

// Powers down 3.3V rail (sensors and IMU disabled).
void powerOff3V3Rail();

// Powers down modem (LTE-M/NB-IoT disabled).
void powerOffModem();

// Adjusts next wake time to avoid quiet hours (00:00-05:59 local time, Europe/Oslo).
uint32_t adjustNextWakeUtcForQuietHours(uint32_t candidateUtc);

// Configures GPIO and peripherals for minimum power leakage before deep sleep.
void preparePinsAndSubsystemsForDeepSleep();