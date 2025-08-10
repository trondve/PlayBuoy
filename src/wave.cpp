#include "wave.h"
#include "sensors.h"
#include <stdlib.h>  // For malloc/free
#include "esp_task_wdt.h"  // For watchdog timer
#include "battery.h"  // For getStableBatteryVoltage

#define SerialMon Serial

// Forward declarations for battery functions
float readBatteryVoltage();
int estimateBatteryPercent(float);

// WaveSample struct and constants
struct WaveSample {
  float altitude;
  float heading;
  unsigned long timestamp;
};

const int SAMPLE_INTERVAL_MS = 1000;

// Dynamically determine sample duration based on battery percent
int getSampleDurationMs() {
  float voltage = getStableBatteryVoltage();  // Use stable voltage instead of measuring
  int percent = estimateBatteryPercent(voltage);
  if (percent > 50) return 60000;      // 10 minutes
  if (percent > 30) return 30000;      // 5 minutes
  if (percent > 20) return 12000;      // 2 minutes
  return 6000;                         // 1 minute
}

int sampleCount = 0;

// Add static storage for last collected samples and count
// Reduced from 600 to 300 to save memory (3.6KB instead of 7.2KB)
// At >50% battery: 10 minutes = 600 samples, but we'll only keep most recent 300
#define MAX_POSSIBLE_SAMPLES 300
static WaveSample lastSamples[MAX_POSSIBLE_SAMPLES];
static int lastSampleCount = 0;

void recordWaveData() {
  SerialMon.println(" Starting wave data collection...");
  sampleCount = 0;

  int sampleDurationMs = getSampleDurationMs();
  const int maxSamples = sampleDurationMs / SAMPLE_INTERVAL_MS;
  
  // Allocate on heap to prevent stack overflow
  WaveSample* samples = (WaveSample*)malloc(maxSamples * sizeof(WaveSample));
  if (!samples) {
    SerialMon.println(" Failed to allocate memory for wave samples");
    return;
  }

  unsigned long startTime = millis();
  while (millis() - startTime < sampleDurationMs && sampleCount < maxSamples) {
    // Reset watchdog timer every 30 seconds during wave data collection
    if (sampleCount % 30 == 0) {
      esp_task_wdt_reset();
    }
    
    WaveSample s;
    s.altitude = getRelativeAltitude();
    s.heading = getHeadingDegrees();
    s.timestamp = millis() - startTime;

    samples[sampleCount++] = s;
    delay(SAMPLE_INTERVAL_MS);
  }

  SerialMon.printf(" Collected %d wave samples\n", sampleCount);

  // Store samples for later use by no-argument compute/log functions
  // If we collected more samples than our storage can hold, keep the most recent ones
  if (sampleCount <= MAX_POSSIBLE_SAMPLES) {
    lastSampleCount = sampleCount;
    for (int i = 0; i < lastSampleCount; ++i) {
      lastSamples[i] = samples[i];
    }
  } else {
    // Keep the last MAX_POSSIBLE_SAMPLES samples
    lastSampleCount = MAX_POSSIBLE_SAMPLES;
    int startIdx = sampleCount - MAX_POSSIBLE_SAMPLES;
    for (int i = 0; i < MAX_POSSIBLE_SAMPLES; ++i) {
      lastSamples[i] = samples[startIdx + i];
    }
    SerialMon.printf("  Kept most recent %d of %d samples due to memory constraints\n", 
                     MAX_POSSIBLE_SAMPLES, sampleCount);
  }
  
  // Free the heap memory
  free(samples);
}

float computeWaveHeight(WaveSample* samples, int count) {
  float minAlt = INFINITY;
  float maxAlt = -INFINITY;
  for (int i = 0; i < count; i++) {
    if (samples[i].altitude < minAlt) minAlt = samples[i].altitude;
    if (samples[i].altitude > maxAlt) maxAlt = samples[i].altitude;
  }
  return maxAlt - minAlt;
}

float computeWavePeriod(WaveSample* samples, int count) {
  if (count < 2) return 0;
  float mid = (computeWaveHeight(samples, count) / 2.0) + samples[0].altitude;

  int crossings = 0;
  unsigned long lastCross = 0;
  unsigned long totalPeriod = 0;

  for (int i = 1; i < count; i++) {
    bool wasBelow = samples[i - 1].altitude < mid;
    bool isAbove = samples[i].altitude >= mid;

    if (wasBelow && isAbove) {
      if (lastCross != 0) {
        totalPeriod += (samples[i].timestamp - lastCross);
        crossings++;
      }
      lastCross = samples[i].timestamp;
    }
  }

  return (crossings > 0) ? (totalPeriod / (float)crossings) / 1000.0 : 0.0;
}

String computeWaveDirection(WaveSample* samples, int count) {
  float total = 0;
  for (int i = 0; i < count; i++) {
    total += samples[i].heading;
  }
  float avgHeading = total / count;

  const char* directions[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  int index = (int)((avgHeading + 22.5) / 45.0) % 8;
  return String(directions[index]);
}

float computeWavePower(float height, float period) {
  return height * height * period;
}

void logWaveStats(WaveSample* samples, int count) {
  float waveHeight = computeWaveHeight(samples, count);
  float wavePeriod = computeWavePeriod(samples, count);
  String waveDirection = computeWaveDirection(samples, count);
  float wavePower = computeWavePower(waveHeight, wavePeriod);

  SerialMon.printf("Wave height: %.2f m\n", waveHeight);
  SerialMon.printf("Wave period: %.2f s\n", wavePeriod);
  SerialMon.printf("Wave direction: %s\n", waveDirection.c_str());
  SerialMon.printf("Wave power: %.2f\n", wavePower);
}

// Overloads for compatibility with main.cpp
float computeWaveHeight() {
  return computeWaveHeight(lastSamples, lastSampleCount);
}

float computeWavePeriod() {
  return computeWavePeriod(lastSamples, lastSampleCount);
}

String computeWaveDirection() {
  return computeWaveDirection(lastSamples, lastSampleCount);
}

void logWaveStats() {
  logWaveStats(lastSamples, lastSampleCount);
}
