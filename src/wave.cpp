// Wave measurement using FFT spectral analysis
// Replaces time-domain double integration with frequency-domain approach:
// acceleration spectrum → displacement spectrum via 1/(2πf)⁴ → Hs, Tp
//
// This eliminates drift from double integration and the empirical DISP_AMP_SCALE
// fudge factor. Spectral Hs = 4*sqrt(m0) is the standard oceanographic method.

#include "wave.h"
#include "sensors.h"
#include "config.h"
#include "esp_task_wdt.h"
#include <Wire.h>
#include <math.h>
#include <algorithm>

#define SerialMon Serial

// Sampling configuration
static const float FS_HZ = 10.0f;           // IMU sample rate
static const uint32_t DT_MS = (uint32_t)(1000.0f / FS_HZ);

// Band limits for heave acceleration pre-filtering
static const float HP_CUTOFF_HZ = 0.05f;    // Wider band for spectral analysis
static const float LP_CUTOFF_HZ = 2.0f;     // Allow higher frequencies into FFT

// Gravity tracker low-pass frequency
static const float G_TRACK_FC_HZ = 0.02f;   // very slow gravity tracker

// FFT configuration
static const uint32_t FFT_N = 1024;          // Power of 2, ~102.4s at 10 Hz

// Wave band limits for spectral integration
static const float WAVE_FREQ_MIN = 0.05f;    // Min wave frequency (20s period)
static const float WAVE_FREQ_MAX = 1.0f;     // Max wave frequency (1s period)

// IMU registers (MPU6500/9250)
#define MPU6500_ADDR           0x68
#define MPU6500_WHO_AM_I       0x75
#define MPU6500_PWR_MGMT_1     0x6B
#define MPU6500_CONFIG         0x1A
#define MPU6500_ACCEL_CONFIG   0x1C
#define MPU6500_ACCEL_CONFIG2  0x1D
#define MPU6500_SMPLRT_DIV     0x19
#define MPU6500_ACCEL_XOUT_H   0x3B

// Runtime state
static bool imuInitialized = false;
static bool iirInitialized = false;

// Collection buffer: stores heave acceleration samples
static const uint32_t MAX_SAMPLES = 1600; // 160 s @ 10 Hz (~2:40, enough for 1024 FFT + 57s settling)
static float accelBuf[MAX_SAMPLES];
static uint32_t sampleCount = 0;

// Running heave acceleration stats (computed incrementally)
static double s_heaveAbsSum = 0.0;
static double s_heaveSqSum = 0.0;
static uint32_t s_heaveStatCount = 0;

// Gravity tracker state (reset each recordWaveData call)
static float g_lp_x = 0.0f, g_lp_y = 0.0f, g_lp_z = 9.80665f;

// Last computed results for public getters
static float s_lastHs = 0.0f;
static float s_lastTp = 0.0f;
static uint16_t s_lastWaves = 0;

// Tilt and acceleration metrics (computed incrementally during sampling)
static double s_tiltSum = 0.0;
static uint32_t s_tiltCount = 0;
static float s_accelRms = 0.0f;

// IIR bandpass filter state for acceleration pre-filtering
static float a_hp, b_hp, a_lp, b_lp;
static float hp_y_prev = 0.0f, hp_x_prev = 0.0f;
static float lp_y_prev = 0.0f;

// I2C helpers
static inline bool i2cWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU6500_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static inline bool i2cReadBytes(uint8_t reg, uint8_t count, uint8_t* out) {
  Wire.beginTransmission(MPU6500_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  uint8_t read = Wire.requestFrom((uint8_t)MPU6500_ADDR, count);
  if (read != count) return false;
  for (uint8_t i = 0; i < count; ++i) out[i] = Wire.read();
  return true;
}

static bool initMPU6500() {
  SerialMon.println("Initializing MPU6500 directly...");

  // Reset
  if (!i2cWrite(MPU6500_PWR_MGMT_1, 0x80)) {
    SerialMon.println("Failed to reset MPU6500");
    return false;
  }
  delay(100);

  // Wake
  if (!i2cWrite(MPU6500_PWR_MGMT_1, 0x00)) {
    SerialMon.println("Failed to wake up MPU6500");
    return false;
  }
  delay(100);

  // Check WHO_AM_I
  uint8_t whoAmI;
  if (!i2cReadBytes(MPU6500_WHO_AM_I, 1, &whoAmI)) {
    SerialMon.println("Failed to read WHO_AM_I");
    return false;
  }

  SerialMon.printf("WHO_AM_I: 0x%02X\n", whoAmI);

  if (whoAmI == 0x70) {
    SerialMon.println("Detected: MPU6500 (No magnetometer)");
  } else if (whoAmI == 0x71 || whoAmI == 0x73) {
    SerialMon.println("Detected: MPU9250 (Has magnetometer)");
  } else {
    SerialMon.printf("Unknown WHO_AM_I: 0x%02X\n", whoAmI);
    return false;
  }

  // DLPF
  if (!i2cWrite(MPU6500_CONFIG, 0x03)) {
    SerialMon.println("Failed to configure low-pass filter");
    return false;
  }

  // Accel +/-2g
  if (!i2cWrite(MPU6500_ACCEL_CONFIG, 0x00)) {
    SerialMon.println("Failed to configure accelerometer");
    return false;
  }

  // Accel DLPF ~44 Hz
  if (!i2cWrite(MPU6500_ACCEL_CONFIG2, 0x03)) {
    SerialMon.println("Failed to configure accelerometer DLPF");
    return false;
  }

  // Sample rate divider for 10 Hz (1 kHz base when DLPF enabled)
  if (!i2cWrite(MPU6500_SMPLRT_DIV, 99)) {
    SerialMon.println("Failed to configure sample rate divider");
    return false;
  }

  SerialMon.println("MPU6500 initialized successfully!");
  imuInitialized = true;
  return true;
}

// ±2g: 16384 LSB/g → 9.80665/16384 m/s² per LSB
static const float ACCEL_SCALE = 9.80665f / 16384.0f;

static void readMPU6500(float &ax, float &ay, float &az) {
  uint8_t buf[6];
  if (!i2cReadBytes(MPU6500_ACCEL_XOUT_H, 6, buf)) {
    ax = ay = az = 0.0f;
    return;
  }
  int16_t accelX = (int16_t)((buf[0] << 8) | buf[1]);
  int16_t accelY = (int16_t)((buf[2] << 8) | buf[3]);
  int16_t accelZ = (int16_t)((buf[4] << 8) | buf[5]);

  ax = accelX * ACCEL_SCALE;
  ay = accelY * ACCEL_SCALE;
  az = accelZ * ACCEL_SCALE;
}

static void computeIIRCoeffs(float fs, float fc_hp, float fc_lp) {
  float dt = 1.0f / fs;
  float RC_hp = 1.0f / (2.0f * PI * fc_hp);
  float alpha_hp = RC_hp / (RC_hp + dt);
  a_hp = alpha_hp; b_hp = alpha_hp;
  float RC_lp = 1.0f / (2.0f * PI * fc_lp);
  float alpha_lp = dt / (RC_lp + dt);
  a_lp = alpha_lp; b_lp = 1.0f - alpha_lp;
}

static inline float bandLimit(float xin) {
  float y_hp = a_hp * (hp_y_prev + xin - hp_x_prev);
  hp_x_prev = xin;
  hp_y_prev = y_hp;
  float y_lp = a_lp * y_hp + b_lp * lp_y_prev;
  lp_y_prev = y_lp;
  return y_lp;
}

// ---- FFT implementation (radix-2 Cooley-Tukey, in-place) ----

// Imaginary part buffer (reused from static allocation)
static float fftIm[FFT_N];

static void fftInPlace(float* re, float* im, uint32_t n) {
  // Bit-reversal permutation
  uint32_t j = 0;
  for (uint32_t i = 0; i < n - 1; i++) {
    if (i < j) {
      float tr = re[i]; re[i] = re[j]; re[j] = tr;
      float ti = im[i]; im[i] = im[j]; im[j] = ti;
    }
    uint32_t k = n >> 1;
    while (k <= j) { j -= k; k >>= 1; }
    j += k;
  }
  // Cooley-Tukey butterfly
  for (uint32_t len = 2; len <= n; len <<= 1) {
    float angle = -2.0f * PI / (float)len;
    float wr = cosf(angle), wi = sinf(angle);
    for (uint32_t i = 0; i < n; i += len) {
      float wr_k = 1.0f, wi_k = 0.0f;
      for (uint32_t k = 0; k < len / 2; k++) {
        uint32_t u = i + k, v = i + k + len / 2;
        float tr = wr_k * re[v] - wi_k * im[v];
        float ti = wr_k * im[v] + wi_k * re[v];
        re[v] = re[u] - tr;
        im[v] = im[u] - ti;
        re[u] += tr;
        im[u] += ti;
        float new_wr = wr_k * wr - wi_k * wi;
        wi_k = wr_k * wi + wi_k * wr;
        wr_k = new_wr;
      }
    }
  }
}

// Spectral wave analysis results
struct SpectralWaveStats {
  float Hs;       // Significant wave height (m)
  float Tp;       // Peak period (s)
  float P;        // Wave power proxy (kW/m)
  uint16_t nBins; // Number of spectral bins in wave band
};

static SpectralWaveStats spectralAnalysis(float* accel, uint32_t nSamples, float fs) {
  SpectralWaveStats result = {0.0f, 0.0f, 0.0f, 0};

  if (nSamples < FFT_N) return result;

  // Use last FFT_N samples (skip initial transients)
  uint32_t offset = nSamples - FFT_N;
  float* re = accel; // Will work in-place on accelBuf

  // Explicitly zero buffers for defensive programming
  // (re is overwritten immediately below; fftIm zeroed later, but do it here too)
  memset(re, 0, sizeof(float) * FFT_N);
  memset(fftIm, 0, sizeof(float) * FFT_N);

  // Copy the segment we want to the beginning of the buffer
  if (offset > 0) {
    for (uint32_t i = 0; i < FFT_N; i++) {
      re[i] = accel[offset + i];
    }
  }

  // Remove mean (DC offset)
  double mean = 0.0;
  for (uint32_t i = 0; i < FFT_N; i++) mean += re[i];
  mean /= (double)FFT_N;
  for (uint32_t i = 0; i < FFT_N; i++) re[i] -= (float)mean;

  // Apply Hanning window
  for (uint32_t i = 0; i < FFT_N; i++) {
    float w = 0.5f * (1.0f - cosf(2.0f * PI * (float)i / (float)(FFT_N - 1)));
    re[i] *= w;
  }

  // Zero imaginary part
  memset(fftIm, 0, sizeof(float) * FFT_N);

  // Compute FFT
  fftInPlace(re, fftIm, FFT_N);

  // Compute one-sided acceleration power spectral density (PSD)
  // PSD(f) = 2 * |X(f)|^2 / (N * fs) for f > 0
  // The factor of 2 accounts for folding the negative frequencies
  // Hanning window correction factor: 1/mean(w^2) = 8/3
  float df = fs / (float)FFT_N;
  float psdScale = 2.0f / ((float)FFT_N * fs) * (8.0f / 3.0f); // includes window correction

  // Spectral integration for wave parameters
  // Displacement PSD = Acceleration PSD / (2*pi*f)^4
  uint32_t binMin = (uint32_t)(WAVE_FREQ_MIN / df);
  uint32_t binMax = (uint32_t)(WAVE_FREQ_MAX / df);
  if (binMin < 1) binMin = 1; // Skip DC
  if (binMax > FFT_N / 2) binMax = FFT_N / 2;

  double m0 = 0.0;          // Zeroth moment (displacement variance)
  float peakPsd = 0.0f;
  uint32_t peakBin = binMin;

  for (uint32_t k = binMin; k <= binMax; k++) {
    float accelPsd = (re[k] * re[k] + fftIm[k] * fftIm[k]) * psdScale;
    float f = (float)k * df;

    // Low-frequency cutoff: zero PSD below WAVE_FREQ_MIN to prevent 1/ω⁴ blowup
    // At very low frequencies, ω becomes tiny, so 1/ω⁴ amplifies noise catastrophically
    if (f < WAVE_FREQ_MIN) {
      continue;  // Skip this bin
    }

    float omega = 2.0f * PI * f;
    float omega4 = omega * omega * omega * omega;

    // Displacement PSD = acceleration PSD / omega^4
    float dispPsd = accelPsd / omega4;

    m0 += (double)dispPsd * (double)df;

    if (dispPsd > peakPsd) {
      peakPsd = dispPsd;
      peakBin = k;
    }
  }

  result.nBins = (uint16_t)(binMax - binMin + 1);

  if (m0 <= 0.0) return result;

  // Hs = 4 * sqrt(m0) — standard oceanographic definition
  result.Hs = 4.0f * sqrtf((float)m0);

  // Parabolic (quadratic) interpolation around peak bin for sub-bin Tp accuracy
  float peakFreq = (float)peakBin * df;
  if (peakBin > binMin && peakBin < binMax) {
    float alpha_pk = re[peakBin - 1] * re[peakBin - 1] + fftIm[peakBin - 1] * fftIm[peakBin - 1];
    float beta_pk  = re[peakBin]     * re[peakBin]     + fftIm[peakBin]     * fftIm[peakBin];
    float gamma_pk = re[peakBin + 1] * re[peakBin + 1] + fftIm[peakBin + 1] * fftIm[peakBin + 1];
    float denom = alpha_pk - 2.0f * beta_pk + gamma_pk;
    if (fabsf(denom) > 1e-12f) {
      float delta = 0.5f * (alpha_pk - gamma_pk) / denom;
      peakFreq = ((float)peakBin + delta) * df;
    }
  }
  result.Tp = (peakFreq > 0.0f) ? 1.0f / peakFreq : 0.0f;
  result.P = 0.49f * result.Hs * result.Hs * result.Tp; // deep-water power proxy

  // Sanity caps (configurable per deployment in config.h)
  if (result.Hs > WAVE_HS_MAX_M) result.Hs = 0.0f; // Exceeds deployment threshold (lake: 2m, ocean: higher)
  if (result.Tp < 0.5f || result.Tp > 25.0f) result.Tp = 0.0f;

  return result;
}

static void ensureIIRInitialized() {
  if (!iirInitialized) {
    computeIIRCoeffs(FS_HZ, HP_CUTOFF_HZ, LP_CUTOFF_HZ);
    iirInitialized = true;
  }
}

void recordWaveData() {
  SerialMon.println("=== Starting wave data collection (160s) ===");

  if (!imuInitialized) {
    SerialMon.println("Attempting IMU initialization...");
    if (!initMPU6500()) {
      SerialMon.println("ERROR: Failed to initialize IMU; wave data will be zeros");
      s_lastHs = 0.0f; s_lastTp = 0.0f; s_lastWaves = 0;
      return;
    } else {
      SerialMon.println("IMU initialized successfully!");
    }
  }

  // Reset all state
  sampleCount = 0;
  s_heaveAbsSum = 0.0; s_heaveSqSum = 0.0; s_heaveStatCount = 0;
  s_tiltSum = 0.0; s_tiltCount = 0; s_accelRms = 0.0f;
  hp_y_prev = hp_x_prev = lp_y_prev = 0.0f;
  g_lp_x = 0.0f; g_lp_y = 0.0f; g_lp_z = 9.80665f;
  iirInitialized = false;
  ensureIIRInitialized();

  // Collect heave acceleration samples for 160s (1024 FFT + ~57s gravity settling)
  const uint32_t sampleMs = 160000UL;
  uint32_t start = millis();
  uint32_t nextTick = start;
  uint32_t tick = 0;

  while ((millis() - start) < sampleMs && sampleCount < MAX_SAMPLES) {
    uint32_t now = millis();
    if (now < nextTick) { delay(1); continue; }
    nextTick += DT_MS;

    if ((tick % 50) == 0) {
      esp_task_wdt_reset();
      SerialMon.printf("Wave collection: %d samples, %d s elapsed\n",
                       sampleCount, (now - start) / 1000);
    }

    float ax = 0, ay = 0, az = 0;
    readMPU6500(ax, ay, az);

    // Sanity: discard if accel magnitude far from 1g
    float amag = sqrtf(ax * ax + ay * ay + az * az);
    if (fabsf(amag - 9.80665f) > 4.9f) { tick++; continue; }

    // Slow gravity tracker in body frame
    const float dt = 1.0f / FS_HZ;
    const float RC = 1.0f / (2.0f * PI * G_TRACK_FC_HZ);
    const float alpha = dt / (RC + dt);
    g_lp_x = (1.0f - alpha) * g_lp_x + alpha * ax;
    g_lp_y = (1.0f - alpha) * g_lp_y + alpha * ay;
    g_lp_z = (1.0f - alpha) * g_lp_z + alpha * az;

    // Specific force (acceleration minus gravity)
    const float ax_spec = ax - g_lp_x;
    const float ay_spec = ay - g_lp_y;
    const float az_spec = az - g_lp_z;
    float gnorm = sqrtf(g_lp_x * g_lp_x + g_lp_y * g_lp_y + g_lp_z * g_lp_z);
    if (gnorm < 1e-3f) gnorm = 9.80665f;

    // Tilt: angle between gravity vector and vertical (z-axis)
    float cosAngle = g_lp_z / gnorm;
    if (cosAngle > 1.0f) cosAngle = 1.0f;
    if (cosAngle < -1.0f) cosAngle = -1.0f;
    s_tiltSum += (double)(acosf(cosAngle) * 57.2958f);
    s_tiltCount++;

    // Project specific force onto gravity direction to get heave acceleration
    const float ux = g_lp_x / gnorm, uy = g_lp_y / gnorm, uz = g_lp_z / gnorm;
    float heaveAcc = -(ax_spec * ux + ay_spec * uy + az_spec * uz);
    if (fabsf(heaveAcc) < 0.001f) heaveAcc = 0.0f;
    if (heaveAcc > 5.0f) heaveAcc = 5.0f;
    else if (heaveAcc < -5.0f) heaveAcc = -5.0f;

    // Light band-limit before storing (anti-alias for FFT)
    float a_heave = bandLimit(heaveAcc);

    // Store heave acceleration for spectral analysis
    if (sampleCount < MAX_SAMPLES) {
      accelBuf[sampleCount] = a_heave;
      sampleCount++;
    }

    // Track heave acceleration stats incrementally
    s_heaveAbsSum += fabsf(a_heave);
    s_heaveSqSum += (double)a_heave * (double)a_heave;
    s_heaveStatCount++;

    tick++;
  }

  SerialMon.printf("Wave collection complete: %d samples in %d s\n",
                   sampleCount, (millis() - start) / 1000);

  // Compute acceleration RMS
  s_accelRms = (s_heaveStatCount > 0)
    ? sqrtf((float)(s_heaveSqSum / (double)s_heaveStatCount))
    : 0.0f;

  // Need at least FFT_N samples for spectral analysis
  if (sampleCount < FFT_N) {
    SerialMon.println("Insufficient samples for FFT spectral analysis");
    s_lastHs = 0.0f; s_lastTp = 0.0f; s_lastWaves = 0;
    return;
  }

  // Gate on acceleration RMS: skip analysis if essentially no motion
  float rms_a = s_accelRms;
  float meanAbsA = (s_heaveStatCount > 0)
    ? (float)(s_heaveAbsSum / (double)s_heaveStatCount) : 0.0f;
  if (rms_a < 0.01f && meanAbsA < 0.005f) {
    SerialMon.println("Motion below threshold, reporting calm");
    s_lastHs = 0.0f; s_lastTp = 0.0f; s_lastWaves = 0;
    return;
  }

  // Run FFT spectral analysis
  SerialMon.println("Running FFT spectral analysis...");
  SpectralWaveStats ws = spectralAnalysis(accelBuf, sampleCount, FS_HZ);
  s_lastHs = ws.Hs;
  s_lastTp = ws.Tp;
  s_lastWaves = ws.nBins; // Report spectral bins used (replaces wave count)

  SerialMon.printf("Spectral result: Hs=%.3f m, Tp=%.2f s, bins=%u\n",
                   s_lastHs, s_lastTp, s_lastWaves);
}

float computeWaveHeight() { return s_lastHs; }
float computeWavePeriod() { return s_lastTp; }
String computeWaveDirection() {
  return String("N/A"); // Magnetometer non-functional in sealed enclosure
}
float computeWavePower(float height, float period) {
  return 0.49f * height * height * period;
}
float computeMeanTilt() {
  return (s_tiltCount > 0) ? (float)(s_tiltSum / (double)s_tiltCount) : 0.0f;
}
float computeAccelRms() { return s_accelRms; }

void logWaveStats() {
  SerialMon.println("---- Wave Stats (FFT spectral) ----");
  SerialMon.printf("Samples: %u @ %.1f Hz, FFT bins: %u\n",
                   sampleCount, FS_HZ, s_lastWaves);
  SerialMon.printf("Hs (sig. height):    %.3f m\n", s_lastHs);
  SerialMon.printf("Tp (period):         %.2f s\n", s_lastTp);
  SerialMon.printf("Power proxy:         %.3f kW/m\n",
                   computeWavePower(s_lastHs, s_lastTp));

  const char* seaState = "Unknown";
  if (s_lastHs < 0.02f) seaState = "No waves (Perfect conditions)";
  else if (s_lastHs < 0.06f) seaState = "Ripples (Very easy to swim)";
  else if (s_lastHs < 0.12f) seaState = "Light waves (OK waves)";
  else if (s_lastHs < 0.30f) seaState = "Medium waves (can be annoying)";
  else if (s_lastHs < 0.60f) seaState = "Large waves (uncomfortable)";
  else seaState = "Too large waves (storm)";
  SerialMon.printf("Sea state:           %s\n", seaState);

  if (s_heaveStatCount > 0) {
    float meanAbsA = (float)(s_heaveAbsSum / (double)s_heaveStatCount);
    SerialMon.printf("Heave |a| mean:      %.4f m/s^2\n", meanAbsA);
  }
  SerialMon.printf("Accel RMS:           %.4f m/s^2\n", s_accelRms);
  SerialMon.printf("Mean tilt:           %.1f deg\n", computeMeanTilt());

  if (!imuInitialized) {
    SerialMon.println("WARNING: MPU6500 data not available - wave readings may be zero");
  }
  SerialMon.println("----------------------------------");
}
