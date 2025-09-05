// Use the exact working method from WORKING_BATTERY_MEASUREMENT_main.cpp
#include "power.h"
#include "config.h"
#include <Arduino.h>
#define SerialMon Serial

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

#define PIN_ADC_BAT 35
#define ADC_SAMPLES 50

static uint16_t readAnalogRawAverage(int pin, int samples) {
  UNUSED(analogRead(pin));
  delay(2);
  uint32_t sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += (uint32_t)analogRead(pin);
    delay(2);
  }
  return (uint16_t)(sum / samples);
}

static float method1_user_formula(uint16_t raw) {
  float v = ((float)raw / 4095.0f) * 2.0f * 3.3f * (1110.0f / 1000.0f);
  return v;
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
  return true;
}

float readBatteryVoltage() {
  SerialMon.println();
  SerialMon.println("Starting battery voltage measurement..");
  SerialMon.printf("Pin: GPIO%d, ADC: 12-bit, Atten: 11 dB\n", PIN_ADC_BAT);
  SerialMon.println("Stabilizing for 2 s before first burst...");
  delay(2000);

  float v[5];
  for (int i = 0; i < 5; i++) {
    uint16_t raw = readAnalogRawAverage(PIN_ADC_BAT, ADC_SAMPLES);
    v[i] = method1_user_formula(raw);
    SerialMon.printf("Burst V[%d]: %.3f V  %s\n", i + 1, v[i], isValidVoltage(v[i]) ? "OK" : "INVALID");
    if (i < 4) {
      delay(1000);
    }
  }
  float vmed = medianOfFive(v[0], v[1], v[2], v[3], v[4]);
  SerialMon.printf("Median (5): %.3f V  %s\n", vmed, isValidVoltage(vmed) ? "OK" : "INVALID");
  SerialMon.println("Measurement set complete.");
  return vmed;
}