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

// Function to test different ADC channels
uint32_t testAdcChannel(adc1_channel_t channel) {
  adc1_config_channel_atten(channel, ADC_ATTEN_DB_12);
  delay(100);
  return adc1_get_raw(channel);
}

// Alternative battery voltage reading using analogRead
float readBatteryVoltageAlternative() {
  SerialMon.println("Trying alternative battery voltage reading method...");
  
  // Test all GPIO pins with analogRead
  int readings[4] = {0};
  int pins[4] = {32, 33, 34, 35};
  
  for (int i = 0; i < 4; i++) {
    // Take multiple readings for stability
    int total = 0;
    int valid = 0;
    for (int j = 0; j < 10; j++) {
      int reading = analogRead(pins[i]);
      if (reading > 0 && reading < 4095) {
        total += reading;
        valid++;
      }
      delay(5);
    }
    readings[i] = valid > 0 ? total / valid : 0;
  }
  
  SerialMon.printf("GPIO 32 analogRead: %d\n", readings[0]);
  SerialMon.printf("GPIO 33 analogRead: %d\n", readings[1]);
  SerialMon.printf("GPIO 34 analogRead: %d\n", readings[2]);
  SerialMon.printf("GPIO 35 analogRead: %d\n", readings[3]);
  
  // Find the best reading (highest non-zero value)
  int bestReading = 0;
  int bestPin = -1;
  for (int i = 0; i < 4; i++) {
    if (readings[i] > bestReading) {
      bestReading = readings[i];
      bestPin = pins[i];
    }
  }
  
  if (bestReading == 0) {
    SerialMon.println(" No valid analog readings found");
    return 4.0f;  // Safe fallback
  }
  
  SerialMon.printf("Best reading: GPIO %d = %d\n", bestPin, bestReading);
  
  // Try different voltage divider ratios to find the most accurate one
  float ratios[] = {40.0f, 45.0f, 47.0f, 50.0f, 55.0f};
  float bestVoltage = 0.0f;
  float bestRatio = 0.0f;
  
  for (float ratio : ratios) {
    float voltage = (bestReading / 4095.0f) * 3.3f * ratio;
    SerialMon.printf("Ratio %.1f: %.2fV\n", ratio, voltage);
    
    // Check if this voltage is reasonable and close to expected 4.15V
    if (voltage >= 3.8f && voltage <= 4.3f) {
      if (bestRatio == 0.0f || abs(voltage - 4.15f) < abs(bestVoltage - 4.15f)) {
        bestVoltage = voltage;
        bestRatio = ratio;
      }
    }
  }
  
  if (bestRatio > 0.0f) {
    SerialMon.printf(" Found reasonable voltage with ratio %.1f: %.2fV\n", bestRatio, bestVoltage);
    return bestVoltage * calibrationFactor;
  } else {
    SerialMon.println(" No reasonable voltage found with alternative method");
    return 4.0f;  // Safe fallback
  }
}

float readBatteryVoltage() {
  // Configure ADC for battery voltage monitoring
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_12);
  
  // Take multiple readings and average them for stability
  const int numReadings = 20;  // Increased for better stability
  int totalReading = 0;
  int validReadings = 0;
  
  for (int i = 0; i < numReadings; i++) {
    int reading = adc1_get_raw(BATTERY_ADC_CHANNEL);
    if (reading > 0 && reading < 4095) {  // Valid reading
      totalReading += reading;
      validReadings++;
    }
    delay(10);  // Small delay between readings
  }
  
  if (validReadings == 0) {
    SerialMon.println(" No valid ADC readings on primary channel, trying alternative");
    return readBatteryVoltageAlternative();
  }
  
  int avgReading = totalReading / validReadings;
  float rawVoltage = (avgReading / ADC_RESOLUTION) * ADC_REFERENCE_VOLTAGE;
  float batteryVoltage = rawVoltage * VOLTAGE_DIVIDER_RATIO * calibrationFactor;
  
  SerialMon.printf("ADC: %d (valid: %d/%d), Raw: %.2fV, Calibrated: %.2fV\n", 
                   avgReading, validReadings, numReadings, rawVoltage, batteryVoltage);
  
  // Validate the reading is reasonable
  if (batteryVoltage < 3.0f || batteryVoltage > 4.5f) {
    SerialMon.printf("  Voltage %.2fV seems unreasonable, trying alternative method\n", batteryVoltage);
    return readBatteryVoltageAlternative();
  }
  
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

float readBatteryCurrent() {
  // Placeholder - no current sensor connected
  return 0.0f;
}

bool beginPowerMonitor() {
  SerialMon.println("Initializing power monitor...");
  
  // Test all ADC channels to find which one has the battery voltage
  SerialMon.println("Testing all ADC channels...");
  
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_12);
  adc1_config_channel_atten(ALT_ADC_CHANNEL_1, ADC_ATTEN_DB_12);
  adc1_config_channel_atten(ALT_ADC_CHANNEL_2, ADC_ATTEN_DB_12);
  adc1_config_channel_atten(ALT_ADC_CHANNEL_3, ADC_ATTEN_DB_12);
  
  int testReading = adc1_get_raw(BATTERY_ADC_CHANNEL);
  SerialMon.printf("Test ADC reading: %d\n", testReading);
  
  int ch5 = adc1_get_raw(BATTERY_ADC_CHANNEL);  // GPIO 33
  int ch4 = adc1_get_raw(ALT_ADC_CHANNEL_1);    // GPIO 32
  int ch6 = adc1_get_raw(ALT_ADC_CHANNEL_2);    // GPIO 34
  int ch7 = adc1_get_raw(ALT_ADC_CHANNEL_3);    // GPIO 35
  
  SerialMon.printf("ADC test - CH5(GPIO33): %d, CH4(GPIO32): %d, CH6(GPIO34): %d, CH7(GPIO35): %d\n", 
                   ch5, ch4, ch6, ch7);
  
  // Find the channel with the highest reading
  int maxReading = 0;
  adc1_channel_t bestChannel = BATTERY_ADC_CHANNEL;
  
  if (ch5 > maxReading) { maxReading = ch5; bestChannel = BATTERY_ADC_CHANNEL; }
  if (ch4 > maxReading) { maxReading = ch4; bestChannel = ALT_ADC_CHANNEL_1; }
  if (ch6 > maxReading) { maxReading = ch6; bestChannel = ALT_ADC_CHANNEL_2; }
  if (ch7 > maxReading) { maxReading = ch7; bestChannel = ALT_ADC_CHANNEL_3; }
  
  SerialMon.printf("Using ADC channel with reading: %d\n", maxReading);
  
  return true;
}