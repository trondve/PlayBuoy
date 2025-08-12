#include "power.h"
#include "config.h"
#include <Arduino.h>
#include <driver/adc.h>
#define SerialMon Serial

// LilyGo T-SIM7000G battery voltage monitoring
// Based on terminal output, GPIO 33 (ADC1_CHANNEL_5) has the battery voltage signal
#define BATTERY_ADC_CHANNEL ADC1_CHANNEL_5  // Fixed: GPIO 33 on LilyGo T-SIM7000G
#define BATTERY_ADC_PIN 33

// Fixed pin/channel for VBAT sense
static int gBatteryAdcPin = BATTERY_ADC_PIN;
static adc1_channel_t gBatteryAdcChannel = BATTERY_ADC_CHANNEL;

// Voltage divider calibration - measured ~0.359V at pin for 4.17V battery => ~11.6:1
#define VOLTAGE_DIVIDER_RATIO 46.9f  // Tuned so 0.085V ADC -> ~4.17V battery with factor=1.0
#define ADC_REFERENCE_VOLTAGE 3.3f  // ESP32 ADC reference voltage
#define ADC_RESOLUTION 4095.0f      // 12-bit ADC resolution

// Calibration factor - adjust this based on multimeter readings
#ifdef BATTERY_CALIBRATION_FACTOR
static float calibrationFactor = BATTERY_CALIBRATION_FACTOR;
#else
static float calibrationFactor = 1.0f;
#endif

// Low-level: read one ADC sample and convert to voltage. No logging.
float readBatteryVoltageSample() {
  // Use calibrated Arduino helper to get mV at the ADC pin, then scale by divider
  analogSetPinAttenuation(gBatteryAdcPin, ADC_11db);
  const int n = 30;
  uint32_t mvSum = 0; int mvCnt = 0;
  for (int i = 0; i < n; ++i) {
    uint32_t mv = analogReadMilliVolts(gBatteryAdcPin);
    if (mv > 0 && mv < 4000) { mvSum += mv; mvCnt++; }
    delay(5);
  }
  if (mvCnt == 0) return NAN;
  float rawVoltage = (mvSum / (float)mvCnt) / 1000.0f;
  float batteryVoltage = rawVoltage * VOLTAGE_DIVIDER_RATIO * calibrationFactor;
  return batteryVoltage;
}

// High-level: accurate quiet measurement using two spaced windows with trimmed mean
float readBatteryVoltage() {
  // Yesterday's working method: average raw ADC codes then scale by 3.3V and divider
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(gBatteryAdcChannel, ADC_ATTEN_DB_12);
  const int numReadings = 10; // reduce ADC conversions to limit self-heating
  int totalReading = 0; int validReadings = 0;
  for (int i = 0; i < numReadings; i++) {
    int reading = adc1_get_raw(gBatteryAdcChannel);
    if (reading > 0 && reading < 4095) { totalReading += reading; validReadings++; }
    delay(10);
  }
  if (validReadings == 0) {
    SerialMon.println(" No valid ADC readings on primary channel, falling back to mV sampling");
    float fallback = readBatteryVoltageSample();
    if (!isnan(fallback)) {
      SerialMon.printf(" Fallback sample voltage: %.3fV\n", fallback);
      return fallback;
    }
    // As a last resort, return 0 to indicate critical/unknown instead of NAN
    SerialMon.println(" Fallback sample invalid; returning 0.0V");
    return 0.0f;
  }
  int avgReading = totalReading / validReadings;
  float rawVoltage = (avgReading / ADC_RESOLUTION) * ADC_REFERENCE_VOLTAGE;
  float batteryVoltage = rawVoltage * VOLTAGE_DIVIDER_RATIO * calibrationFactor;
  SerialMon.printf("ADC: %d (valid: %d/%d), Raw: %.2fV, Calibrated: %.3fV\n", avgReading, validReadings, numReadings, rawVoltage, batteryVoltage);
  if (batteryVoltage < 3.2f || batteryVoltage > 4.6f) {
    SerialMon.printf("  Voltage %.3fV outside plausible range, using as best-effort value\n", batteryVoltage);
    // Return best-effort outlier instead of NAN per user preference
    return batteryVoltage;
  }
  // Also print calibration mapping
  SerialMon.printf("Calibration: factor=%.4f, pre-cal=%.3fV -> final=%.3fV\n", calibrationFactor, batteryVoltage / calibrationFactor, batteryVoltage);
  return batteryVoltage;
}

// Function to calibrate voltage reading based on multimeter measurement
void calibrateBatteryVoltage(float actualVoltage) {
  // Take a reading to calibrate against
  float measuredVoltage = readBatteryVoltage();
  
  if (measuredVoltage > 0.1f) {
    calibrationFactor = actualVoltage / measuredVoltage;
    SerialMon.printf("Calibration: actual=%.2fV, measured=%.2fV, factor=%.3f\n", 
                     actualVoltage, measuredVoltage, calibrationFactor);
  } else {
    SerialMon.println(" Cannot calibrate - invalid voltage reading");
  }
}



bool beginPowerMonitor() {
  // Configure ADC width and channel for fixed VBAT pin
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(gBatteryAdcChannel, ADC_ATTEN_DB_12);

  // Always-on marker to verify power monitor init
  SerialMon.printf("[power.cpp] Build %s %s, FW=%s, calib=%.4f, divider=%.2f, pin=%d\n",
                   __DATE__, __TIME__, FIRMWARE_VERSION, calibrationFactor, VOLTAGE_DIVIDER_RATIO, gBatteryAdcPin);
  // Enable common VBAT sense switches used on LilyGO boards
  const int vbatEnPins[] = {14, 25};
  for (size_t i = 0; i < sizeof(vbatEnPins)/sizeof(vbatEnPins[0]); ++i) {
    pinMode(vbatEnPins[i], OUTPUT);
    digitalWrite(vbatEnPins[i], HIGH);
  }
  delay(10);
  // Print a quick raw mV diagnostic to help verify divider
  analogSetPinAttenuation(gBatteryAdcPin, ADC_11db);
  uint32_t mvSum = 0; int mvCnt = 0;
  for (int i = 0; i < 10; ++i) { uint32_t mv = analogReadMilliVolts(gBatteryAdcPin); if (mv > 0 && mv < 4000) { mvSum += mv; mvCnt++; } delay(5); }
  if (mvCnt > 0) {
    float avgMv = mvSum / (float)mvCnt;
    float estBatt2 = (avgMv / 1000.0f) * 2.0f;
    float estBatt1185 = (avgMv / 1000.0f) * 11.85f;
    SerialMon.printf("[power.cpp] ADC33 avg=%.0fmV -> est battery: ratio2=%.3fV, ratio11.85=%.3fV\n", avgMv, estBatt2, estBatt1185);
  }
  SerialMon.flush();
  return true;
}

float readBatteryVoltageEnhanced(int totalReadings, int delayBetweenReadingsMs, int quickReadsPerGroup, int minValidGroups) {
  // Staggered measurement inspired by earlier main.cpp approach
  float groupVoltages[32];
  if (totalReadings > 32) totalReadings = 32;
  int validGroups = 0;
  const int warmupGroups = 8; // ignore first N groups to let ADC sampling cap settle

  for (int i = 0; i < totalReadings; ++i) {
    float sum = 0.0f; int valid = 0;
    for (int j = 0; j < quickReadsPerGroup; ++j) {
      float v = readBatteryVoltage();
      if (v >= 3.2f && v <= 4.6f) { sum += v; valid++; }
      delay(100);
    }
    if (valid > 0) {
      float avg = sum / valid;
      if (i < warmupGroups) {
        SerialMon.printf("Warmup %d: %.3fV (avg of %d quick readings) [ignored]\n", i + 1, avg, valid);
      } else {
        groupVoltages[validGroups++] = avg;
        SerialMon.printf("Reading %d: %.3fV (avg of %d quick readings)\n", i + 1, avg, valid);
      }
    } else {
      SerialMon.printf("Reading %d: No valid quick readings\n", i + 1);
    }
    delay(delayBetweenReadingsMs);
  }

  if (validGroups >= minValidGroups && validGroups > 0) {
    // Copy and sort values for robust stats
    float sorted[32];
    for (int i = 0; i < validGroups; ++i) sorted[i] = groupVoltages[i];
    for (int i = 0; i < validGroups - 1; ++i) {
      for (int j = i + 1; j < validGroups; ++j) {
        if (sorted[j] < sorted[i]) { float t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t; }
      }
    }

    // Compute basic mean/stddev
    float meanAll = 0.0f; for (int i = 0; i < validGroups; ++i) meanAll += sorted[i];
    meanAll /= validGroups;
    float varAll = 0.0f; for (int i = 0; i < validGroups; ++i) varAll += (sorted[i] - meanAll) * (sorted[i] - meanAll);
    float stdAll = sqrtf(varAll / validGroups);

    float minV = sorted[0];
    float maxV = sorted[validGroups - 1];

    // Quartiles for IQR filter (simple index method)
    int q1Idx = (int)floorf((validGroups - 1) * 0.25f);
    int q3Idx = (int)floorf((validGroups - 1) * 0.75f);
    float q1 = sorted[q1Idx];
    float q3 = sorted[q3Idx];
    float iqr = q3 - q1;
    float lowBound = q1 - 1.5f * iqr;
    float highBound = q3 + 1.5f * iqr;

    // Build inlier set
    float inliers[32];
    int m = 0;
    for (int i = 0; i < validGroups; ++i) {
      float v = sorted[i];
      if (v >= lowBound && v <= highBound) inliers[m++] = v;
    }
    if (m < minValidGroups) { // if over-filtered, fall back to all
      for (int i = 0; i < validGroups; ++i) inliers[i] = sorted[i];
      m = validGroups;
    }

    // Median of inliers
    float median;
    if (m % 2 == 1) median = inliers[m / 2];
    else median = 0.5f * (inliers[m / 2 - 1] + inliers[m / 2]);

    // Trimmed mean (10% each side on inliers)
    int trim = m / 10; if (trim > 3) trim = 3; // cap trimming for small m
    int start = trim, end = m - trim;
    if (end - start < 1) { start = 0; end = m; }
    float sumTrim = 0.0f; int cntTrim = 0;
    for (int i = start; i < end; ++i) { sumTrim += inliers[i]; cntTrim++; }
    float meanTrim = (cntTrim > 0) ? (sumTrim / cntTrim) : median;

    SerialMon.printf("Enhanced battery measurement complete:\n");
    SerialMon.printf("- Valid groups: %d/%d (ignored first %d warmup groups)\n", validGroups, totalReadings, warmupGroups);
    SerialMon.printf("- Mean(all): %.3fV, Std(all): %.3fV, Min: %.3fV, Max: %.3fV\n", meanAll, stdAll, minV, maxV);
    SerialMon.printf("- IQR filter: Q1=%.3fV, Q3=%.3fV, inliers=%d\n", q1, q3, m);
    SerialMon.printf("- Median(inliers): %.3fV, TrimmedMean(inliers): %.3fV\n", median, meanTrim);

    // Prefer median of inliers as robust estimate; if extremely tight, meanTrim ~ median anyway
    return median;
  }

  SerialMon.printf("  Insufficient valid readings for enhanced measurement (got %d, need %d)\n", validGroups, minValidGroups);
  return NAN;
}