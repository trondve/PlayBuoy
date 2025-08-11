#include "power.h"
#include <driver/adc.h>
#define SerialMon Serial

// LilyGo T-SIM7000G battery voltage monitoring
// Based on terminal output, GPIO 33 (ADC1_CHANNEL_5) has the battery voltage signal
#define BATTERY_ADC_CHANNEL ADC1_CHANNEL_5  // GPIO 33 - where we actually get readings
#define BATTERY_ADC_PIN 33

// Alternative channels to try if primary doesn't work
#define ALT_ADC_CHANNEL_1 ADC1_CHANNEL_4   // GPIO 32
#define ALT_ADC_CHANNEL_2 ADC1_CHANNEL_6   // GPIO 34
#define ALT_ADC_CHANNEL_3 ADC1_CHANNEL_7   // GPIO 35

// Voltage divider calibration - fine-tuned based on actual readings
// For 4.165V battery, we're getting ~109 ADC units, ratio needs adjustment
#define VOLTAGE_DIVIDER_RATIO 50.0f  // Adjusted for more accurate readings
#define ADC_REFERENCE_VOLTAGE 3.3f  // ESP32 ADC reference voltage
#define ADC_RESOLUTION 4095.0f      // 12-bit ADC resolution

// Calibration factor - adjust this based on multimeter readings
static float calibrationFactor = 1.0f;

// Alternative battery voltage reading: simple average on primary channel (quiet, minimal logging)
float readBatteryVoltageAlternative() {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_12);
  int valid = 0;
  float sum = 0.0f;
  for (int i = 0; i < 20; ++i) {
    int reading = adc1_get_raw(BATTERY_ADC_CHANNEL);
    if (reading > 0 && reading < 4095) {
      float raw = (reading / ADC_RESOLUTION) * ADC_REFERENCE_VOLTAGE;
      float v = raw * VOLTAGE_DIVIDER_RATIO * calibrationFactor;
      if (v >= 2.5f && v <= 5.5f) { sum += v; valid++; }
    }
    delay(10);
  }
  if (valid == 0) return 4.0f; // safe fallback
  return sum / valid;
}

// Low-level: read one ADC sample and convert to voltage. No logging.
float readBatteryVoltageSample() {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_12);
  int reading = adc1_get_raw(BATTERY_ADC_CHANNEL);
  if (reading <= 0 || reading >= 4095) return NAN;
  float rawVoltage = (reading / ADC_RESOLUTION) * ADC_REFERENCE_VOLTAGE;
  float batteryVoltage = rawVoltage * VOLTAGE_DIVIDER_RATIO * calibrationFactor;
  if (batteryVoltage < 2.5f || batteryVoltage > 5.5f) return NAN;
  return batteryVoltage;
}

// High-level: accurate quiet measurement using two spaced windows with trimmed mean
float readBatteryVoltage() {
  // Ensure quiet rails: caller should avoid calling during modem/GNSS activity
  auto trimmedMean = [] (int samples, int spacingMs) -> float {
    const int maxSamples = 200;
    float buf[maxSamples];
    int n = 0;
    for (int i = 0; i < samples && n < maxSamples; ++i) {
      float v = readBatteryVoltageSample();
      if (!isnan(v)) buf[n++] = v;
      delay(spacingMs);
    }
    if (n < 5) return NAN;
    // Sort and trim 10% at each end
    for (int i = 0; i < n - 1; ++i) {
      for (int j = i + 1; j < n; ++j) {
        if (buf[j] < buf[i]) { float t = buf[i]; buf[i] = buf[j]; buf[j] = t; }
      }
    }
    int trim = n / 10; // 10%
    if (trim > 10) trim = 10;
    int start = trim;
    int end = n - trim;
    if (end - start < 3) { start = 0; end = n; }
    float sum = 0.0f; int cnt = 0;
    for (int i = start; i < end; ++i) { sum += buf[i]; cnt++; }
    return (cnt > 0) ? (sum / cnt) : NAN;
  };

  // Window A: 75 samples, 15 ms spacing (~1.1 s)
  float a = trimmedMean(75, 15);
  delay(900); // idle ~0.9 s
  // Window B: 75 samples, 15 ms spacing (~1.1 s)
  float b = trimmedMean(75, 15);

  // If either window failed, fall back to the other; if both failed, use alternative method
  float v = NAN;
  if (!isnan(a) && !isnan(b)) v = (a + b) * 0.5f;
  else if (!isnan(a)) v = a;
  else if (!isnan(b)) v = b;
  else return readBatteryVoltageAlternative();

  // Reasonableness check; if noisy, do a quick third window
  if (v < 3.0f || v > 4.5f) return readBatteryVoltageAlternative();
  SerialMon.printf("Battery (quiet): %.3fV\n", v);
  return v;
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
  // Configure ADC once
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_12);
  return true;
}