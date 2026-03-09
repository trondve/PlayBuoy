// Use esp_adc_cal for hardware-calibrated battery voltage measurement
#include "power.h"
#include "config.h"
#include <Arduino.h>
#include "esp_adc_cal.h"

#define SerialMon Serial

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

#define PIN_ADC_BAT 35
#define ADC_SAMPLES 50

// ADC calibration characteristics (populated at init)
static esp_adc_cal_characteristics_t s_adcChars;
static bool s_adcCalibrated = false;

// Voltage divider ratio: battery → 2:1 divider → ADC pin
// The board has a 100K/100K divider, so actual battery = ADC reading * 2
// The 1110/1000 factor was an empirical correction for uncalibrated ADC;
// with esp_adc_cal this is no longer needed — just multiply by divider ratio.
static const float DIVIDER_RATIO = 2.0f;

static uint32_t readAdcCalibratedMv(int samples) {
  // Throw away first reading (ESP32 ADC warmup)
  UNUSED(analogRead(PIN_ADC_BAT));
  delay(2);
  uint32_t sum = 0;
  for (int i = 0; i < samples; i++) {
    uint32_t raw = (uint32_t)analogRead(PIN_ADC_BAT);
    if (s_adcCalibrated) {
      sum += esp_adc_cal_raw_to_voltage(raw, &s_adcChars);
    } else {
      // Fallback: manual conversion if calibration data unavailable
      sum += (uint32_t)((float)raw / 4095.0f * 3300.0f);
    }
    delay(2);
  }
  return sum / samples;
}

static float medianOfFive(float a, float b, float c, float d, float e) {
  float v[5] = {a, b, c, d, e};
  for (int i = 0; i < 4; i++) {
    for (int j = i + 1; j < 5; j++) {
      if (v[j] < v[i]) {
        float t = v[i];
        v[i] = v[j];
        v[j] = t;
      }
    }
  }
  return v[2];
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
      s_adcCalibrated = true;
      break;
    case ESP_ADC_CAL_VAL_EFUSE_VREF:
      SerialMon.println("ADC calibration: Vref (eFuse)");
      s_adcCalibrated = true;
      break;
    default:
      SerialMon.println("ADC calibration: Default (no eFuse data)");
      s_adcCalibrated = true; // Still use esp_adc_cal default calibration
      break;
  }
  return true;
}

float readBatteryVoltage() {
  SerialMon.println();
  SerialMon.println("Starting battery voltage measurement..");
  SerialMon.printf("Pin: GPIO%d, ADC: 12-bit, Atten: 11 dB, Calibrated: %s\n",
                   PIN_ADC_BAT, s_adcCalibrated ? "YES" : "NO");
  SerialMon.println("Stabilizing before first burst...");
  delay(500);  // ADC input is just a resistor divider, settles in <100ms

  float v[5];
  for (int i = 0; i < 5; i++) {
    uint32_t adcMv = readAdcCalibratedMv(ADC_SAMPLES);
    // Convert from ADC millivolts to actual battery voltage
    v[i] = (float)adcMv / 1000.0f * DIVIDER_RATIO;
    SerialMon.printf("Burst V[%d]: %.3f V (ADC: %lu mV)  %s\n",
                     i + 1, v[i], adcMv, isValidVoltage(v[i]) ? "OK" : "INVALID");
    if (i < 4) {
      delay(500);  // 500ms between bursts (was 1000ms, ADC settles in <100ms)
    }
  }
  float vmed = medianOfFive(v[0], v[1], v[2], v[3], v[4]);
  SerialMon.printf("Median (5): %.3f V  %s\n", vmed, isValidVoltage(vmed) ? "OK" : "INVALID");
  SerialMon.println("Measurement set complete.");
  return vmed;
}
