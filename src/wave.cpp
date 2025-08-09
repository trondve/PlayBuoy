#include "wave.h"
#include "sensors.h"

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
  float voltage = readBatteryVoltage();
  int percent = estimateBatteryPercent(voltage);
  if (percent > 50) return 600000;      // 10 minutes
  if (percent > 30) return 300000;      // 5 minutes
  if (percent > 20) return 120000;      // 2 minutes
  return 60000;                         // 1 minute
}

int sampleCount = 0;

// Add static storage for last collected samples and count
#define MAX_POSSIBLE_SAMPLES 600
static WaveSample lastSamples[MAX_POSSIBLE_SAMPLES];
static int lastSampleCount = 0;

void recordWaveData() {
  SerialMon.println("🌊 Starting wave data collection...");
  sampleCount = 0;

  int sampleDurationMs = getSampleDurationMs();
  const int maxSamples = sampleDurationMs / SAMPLE_INTERVAL_MS;
  WaveSample samples[maxSamples];

  unsigned long startTime = millis();
  while (millis() - startTime < sampleDurationMs && sampleCount < maxSamples) {
    WaveSample s;
    s.altitude = getRelativeAltitude();
    s.heading = getHeadingDegrees();
    s.timestamp = millis() - startTime;

    samples[sampleCount++] = s;
    delay(SAMPLE_INTERVAL_MS);
  }

  SerialMon.printf("🌊 Collected %d wave samples\n", sampleCount);

  // Store samples for later use by no-argument compute/log functions
  lastSampleCount = sampleCount > MAX_POSSIBLE_SAMPLES ? MAX_POSSIBLE_SAMPLES : sampleCount;
  for (int i = 0; i < lastSampleCount; ++i) {
    lastSamples[i] = samples[i];
  }
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
