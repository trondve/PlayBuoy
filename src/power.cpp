// Battery voltage measurement using esp_adc_cal for hardware-calibrated ADC
//
// Strategy: 3 bursts of 50 samples each, median-of-three.
// Each burst averages out random ADC noise (σ reduced by √50 ≈ 7×).
// Median-of-three rejects a single outlier burst (e.g. from a transient).
// No inter-burst delays needed — ESP32 ADC noise is thermal, not bursty.
//
// Typical accuracy: ±10-20 mV at battery (±5-10 mV at ADC after divider),
// depending on eFuse calibration quality.

#include "power.h"
#include "config.h"
#include <Arduino.h>
#include "esp_adc_cal.h"

#define SerialMon Serial

#define PIN_ADC_BAT 35
#define ADC_SAMPLES_PER_BURST 50
#define NUM_BURSTS 3

// ADC calibration characteristics (populated at init)
static esp_adc_cal_characteristics_t s_adcChars;

// Voltage divider ratio: battery → 2:1 divider → ADC pin
// The board has a 100K/100K divider, so actual battery = ADC reading × 2
static const float DIVIDER_RATIO = 2.0f;

// Read N ADC samples, convert each via esp_adc_cal, return average in millivolts.
// Uses 200µs inter-sample spacing to decorrelate from switching regulator noise
// while keeping total burst time short (~10ms for 50 samples).
static uint32_t readAdcBurstMv(int samples) {
  uint32_t sum = 0;
  for (int i = 0; i < samples; i++) {
    uint32_t raw = (uint32_t)analogRead(PIN_ADC_BAT);
    sum += esp_adc_cal_raw_to_voltage(raw, &s_adcChars);
    delayMicroseconds(200);
  }
  return sum / samples;
}

static float medianOfThree(float a, float b, float c) {
  if (a > b) { float t = a; a = b; b = t; }
  if (b > c) { float t = b; b = c; c = t; }
  if (a > b) { float t = a; a = b; b = t; }
  return b;
}

static bool isValidVoltage(float v) {
  return (v >= 3.0f && v <= 4.5f);
}

bool beginPowerMonitor() {
  pinMode(PIN_ADC_BAT, INPUT);
  analogSetWidth(12);
  analogSetPinAttenuation(PIN_ADC_BAT, ADC_11db);

  // Initialize esp_adc_cal with factory eFuse calibration data
  // ADC1_CHANNEL_7 corresponds to GPIO 35
  esp_adc_cal_value_t calType = esp_adc_cal_characterize(
    ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &s_adcChars);

  switch (calType) {
    case ESP_ADC_CAL_VAL_EFUSE_TP:
      SerialMon.println("ADC calibration: Two Point (eFuse)");
      break;
    case ESP_ADC_CAL_VAL_EFUSE_VREF:
      SerialMon.println("ADC calibration: Vref (eFuse)");
      break;
    default:
      SerialMon.println("ADC calibration: Default (no eFuse data)");
      break;
  }
  return true;
}

float readBatteryVoltage() {
  SerialMon.println();
  SerialMon.println("Starting battery voltage measurement..");
  SerialMon.printf("Pin: GPIO%d, ADC: 12-bit, Atten: 11 dB\n", PIN_ADC_BAT);

  // Discard first reading — ESP32 ADC has a one-time warmup artifact
  // when the channel is first selected after power-on.
  analogRead(PIN_ADC_BAT);
  delayMicroseconds(500);

  float v[NUM_BURSTS];
  for (int i = 0; i < NUM_BURSTS; i++) {
    uint32_t adcMv = readAdcBurstMv(ADC_SAMPLES_PER_BURST);
    v[i] = (float)adcMv / 1000.0f * DIVIDER_RATIO;
    SerialMon.printf("Burst V[%d]: %.3f V (ADC: %lu mV)  %s\n",
                     i + 1, v[i], adcMv, isValidVoltage(v[i]) ? "OK" : "INVALID");
  }

  float vmed = medianOfThree(v[0], v[1], v[2]);

  // Log spread for remote diagnostics — if bursts differ by >20mV something is off
  float vmin = v[0], vmax = v[0];
  for (int i = 1; i < NUM_BURSTS; i++) {
    if (v[i] < vmin) vmin = v[i];
    if (v[i] > vmax) vmax = v[i];
  }
  float spread = (vmax - vmin) * 1000.0f; // in mV
  SerialMon.printf("Median (%d): %.3f V  spread: %.0f mV  %s\n",
                   NUM_BURSTS, vmed, spread,
                   isValidVoltage(vmed) ? "OK" : "INVALID");
  if (spread > 20.0f) {
    SerialMon.printf("WARNING: ADC spread %.0f mV > 20 mV — noisy measurement\n", spread);
  }
  return vmed;
}
