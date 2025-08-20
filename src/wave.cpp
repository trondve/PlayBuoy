// New wave measurement implementation based on Working_Wave_Measurement_config.cpp
#include "wave.h"
#include "sensors.h"
#include "config.h"
#include "battery.h"
#include "esp_task_wdt.h"
#include <Wire.h>
#include <MahonyAHRS.h>
#include <math.h>
#include <algorithm>

#define SerialMon Serial

// Forward declarations from battery.cpp
float readBatteryVoltage();
int estimateBatteryPercent(float);

// Sampling configuration (aligned with working config)
static const float FS_HZ = 10.0f;           // IMU sample rate
static const uint32_t DT_MS = (uint32_t)(1000.0f / FS_HZ);

// Band limits for wave band (working config values)
static const float HP_CUTOFF_HZ = 0.28f;    // ~4–5 s band start
static const float LP_CUTOFF_HZ = 1.0f;     // ~1 Hz high cutoff
static const float G_TRACK_FC_HZ = 0.02f;   // very slow gravity tracker

// Displacement amplitude calibration (from working config notes)
static const float DISP_AMP_SCALE = 1.75f;

// IMU registers (MPU6500/9250)
#define MPU6500_ADDR           0x68
#define MPU6500_WHO_AM_I       0x75
#define MPU6500_PWR_MGMT_1     0x6B
#define MPU6500_CONFIG         0x1A
#define MPU6500_GYRO_CONFIG    0x1B
#define MPU6500_ACCEL_CONFIG   0x1C
#define MPU6500_ACCEL_CONFIG2  0x1D
#define MPU6500_SMPLRT_DIV     0x19
#define MPU6500_ACCEL_XOUT_H   0x3B

// Runtime state
static bool imuInitialized = false;
static bool filterInitialized = false;
static Mahony filter;

// Wave buffers/state
static const uint32_t MAX_SAMPLES = 3000; // up to 300 s @ 10 Hz (5 minutes)
static float dispBuf[MAX_SAMPLES];
static float aHeaveBuf[MAX_SAMPLES];
static uint32_t dispCount = 0;

// Last computed results for public getters
static float s_lastHs = 0.0f;
static float s_lastTp = 0.0f;
static uint16_t s_lastWaves = 0;
static float s_headingSum = 0.0f;
static uint32_t s_headingCount = 0;

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
  
  // Assume Wire.begin(...) was called in beginSensors()
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
  
  // Gyro ±250 dps
  if (!i2cWrite(MPU6500_GYRO_CONFIG, 0x00)) {
    SerialMon.println("Failed to configure gyroscope");
    return false;
  }
  
  // Accel ±2g
  if (!i2cWrite(MPU6500_ACCEL_CONFIG, 0x00)) {
    SerialMon.println("Failed to configure accelerometer");
    return false;
  }
  
  // Accel DLPF ~44 Hz
  if (!i2cWrite(MPU6500_ACCEL_CONFIG2, 0x03)) {
    SerialMon.println("Failed to configure accelerometer DLPF");
    return false;
  }
  
  // Sample rate divider for 10 Hz (assuming 1 kHz when DLPF enabled)
  if (!i2cWrite(MPU6500_SMPLRT_DIV, 99)) {
    SerialMon.println("Failed to configure sample rate divider");
    return false;
  }
  
  SerialMon.println("MPU6500 initialized successfully!");
  imuInitialized = true;
  return true;
}

static void readMPU6500(float &ax, float &ay, float &az, float &gx, float &gy, float &gz) {
  uint8_t buf[14];
  if (!i2cReadBytes(MPU6500_ACCEL_XOUT_H, 14, buf)) {
    ax = ay = az = gx = gy = gz = 0.0f;
    return;
  }
  int16_t accelX = (int16_t)((buf[0] << 8) | buf[1]);
  int16_t accelY = (int16_t)((buf[2] << 8) | buf[3]);
  int16_t accelZ = (int16_t)((buf[4] << 8) | buf[5]);
  int16_t gyroX  = (int16_t)((buf[8] << 8) | buf[9]);
  int16_t gyroY  = (int16_t)((buf[10] << 8) | buf[11]);
  int16_t gyroZ  = (int16_t)((buf[12] << 8) | buf[13]);

  // Convert to SI and deg/s (matches working config conventions)
  ax = accelX * 0.000598f; // m/s^2 per LSB (approx for ±2g)
  ay = accelY * 0.000598f;
  az = accelZ * 0.000598f;
  gx = gyroX  * 0.00763f;  // deg/s per LSB (±250 dps)
  gy = gyroY  * 0.00763f;
  gz = gyroZ  * 0.00763f;
}

// IIR helpers (single-pole forms)
static float a_hp, b_hp, a_lp, b_lp;
static float hp_y_prev = 0.0f, hp_x_prev = 0.0f;
static float lp_y_prev = 0.0f;

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

// Displacement band-pass uses separate memory to avoid bleed
static float hp_y_prev_d = 0.0f, hp_x_prev_d = 0.0f;
static float lp_y_prev_d = 0.0f;
static inline float bandPassDisplacement(float xin) {
  float y_hp = a_hp * (hp_y_prev_d + xin - hp_x_prev_d);
  hp_x_prev_d = xin;
  hp_y_prev_d = y_hp;
  float y_lp = a_lp * y_hp + b_lp * lp_y_prev_d;
  lp_y_prev_d = y_lp;
  return y_lp;
}

// Analyze zero-upcrossing waves (port from working config)
struct WaveStats { float Hs_m; float Tp_s; float P_kWpm; uint16_t wavesCount; };

static WaveStats analyzeWaves(const float* xbuf, uint32_t n, float fs) {
  const float MIN_PERIOD_S = 1.0f;
  const uint32_t MIN_SAMPLES = (uint32_t)(MIN_PERIOD_S * fs);
  const float MAX_PERIOD_S = 30.0f;
  const uint32_t MAX_SAMPLES_WAVE = (uint32_t)(MAX_PERIOD_S * fs);
  const float MIN_WAVE_HEIGHT = 0.015f;

  struct Wave { float H; float T; };
  static const uint16_t MAX_WAVES = 256;
  Wave waves[MAX_WAVES];
  uint16_t wc = 0;

  const float ZERO_LOW = -0.0005f;
  const float ZERO_HIGH = 0.0005f;
  enum State { BELOW, IN_BAND, ABOVE };
  auto stateOf = [&](float v){ if (v < ZERO_LOW) return BELOW; if (v > ZERO_HIGH) return ABOVE; return IN_BAND; };
  bool isPos = (xbuf[0] > ZERO_HIGH);
  uint32_t lastUp = UINT32_MAX;
  float localMax = -1e9f, localMin = 1e9f;

  for (uint32_t i = 1; i < n; ++i) {
    State st = stateOf(xbuf[i]);
    if (!isPos && st == ABOVE) {
      if (lastUp != UINT32_MAX) {
        uint32_t dt = i - lastUp;
        if (dt >= MIN_SAMPLES && dt <= MAX_SAMPLES_WAVE) {
          float H = localMax - localMin;
          if (H > 0.8f) H = 0.0f;
          float T = dt / fs;
          if (wc < MAX_WAVES && H > MIN_WAVE_HEIGHT) waves[wc++] = {H, T};
        }
      }
      lastUp = i; localMax = -1e9f; localMin = 1e9f; isPos = true;
    } else if (isPos && st == BELOW) {
      isPos = false;
    }
    if (xbuf[i] > localMax) localMax = xbuf[i];
    if (xbuf[i] < localMin) localMin = xbuf[i];
  }

  if (wc == 0) return {0.0f, 0.0f, 0.0f, 0};
  for (uint16_t i = 0; i < wc; ++i) if (waves[i].H > 5.0f) return {0.0f, 0.0f, 0.0f, 0};

  uint16_t K = (uint16_t)max<uint16_t>(1, wc / 3);
  float Hlist[MAX_WAVES], Tlist[MAX_WAVES];
  for (uint16_t i = 0; i < wc; ++i) { Hlist[i] = waves[i].H; Tlist[i] = waves[i].T; }
  for (uint16_t i = 0; i < K; ++i) {
    uint16_t mi = i;
    for (uint16_t j = i + 1; j < wc; ++j) if (Hlist[j] > Hlist[mi]) mi = j;
    float th = Hlist[i]; Hlist[i] = Hlist[mi]; Hlist[mi] = th;
    float tt = Tlist[i]; Tlist[i] = Tlist[mi]; Tlist[mi] = tt;
  }
  float sumH = 0.0f, sumT = 0.0f;
  for (uint16_t i = 0; i < K; ++i) { sumH += Hlist[i]; sumT += Tlist[i]; }
  float Hs = sumH / K;
  float Tp = sumT / K;
  float P = 0.49f * Hs * Hs * Tp; // deep-water power proxy
  return {Hs, Tp, P, wc};
}

// Determine sampling duration dynamically using battery percent (existing policy)
static int getSampleDurationMs() {
#if DEBUG_NO_DEEP_SLEEP
  return 1000;
#else
  float voltage = getStableBatteryVoltage();
  int percent = estimateBatteryPercent(voltage);
  if (percent > 60) return 120000;
  if (percent > 40) return  90000;
  return 60000;
#endif
}

static void ensureFilterInitialized() {
  if (!filterInitialized) {
    filter.begin(FS_HZ);
    computeIIRCoeffs(FS_HZ, HP_CUTOFF_HZ, LP_CUTOFF_HZ);
    filterInitialized = true;
  }
}

void recordWaveData() {
  SerialMon.println("=== Starting wave data collection ===");
  ensureFilterInitialized();
  
  if (!imuInitialized) {
    SerialMon.println("Attempting IMU initialization...");
    if (!initMPU6500()) {
      SerialMon.println("ERROR: Failed to initialize IMU; wave data will be zeros");
      SerialMon.println("Please check:");
      SerialMon.println("1. I2C connections (SDA=21, SCL=22)");
      SerialMon.println("2. Power supply (3.3V)");
      SerialMon.println("3. GY-91 module is properly connected");
      SerialMon.println("4. No short circuits or loose connections");
      SerialMon.println("5. Try power cycling the ESP32 and GY-91");
      s_lastHs = 0.0f; s_lastTp = 0.0f; s_lastWaves = 0; s_headingSum = 0.0f; s_headingCount = 0;
      return;
    } else {
      SerialMon.println("IMU initialized successfully!");
    }
  }

  // Reset buffers/state
  dispCount = 0; hp_y_prev = hp_x_prev = lp_y_prev = 0.0f; hp_y_prev_d = hp_x_prev_d = lp_y_prev_d = 0.0f;
  float v = 0.0f, x = 0.0f;
  float prev_a = 0.0f, prev_v = 0.0f;
  s_headingSum = 0.0f; s_headingCount = 0;

  // Run for a fixed 5-minute window as requested
  const uint32_t sampleMs = (uint32_t)(300000UL);
  uint32_t start = millis();
  uint32_t nextTick = start;
  uint32_t tick = 0;
  while ((millis() - start) < sampleMs && dispCount < MAX_SAMPLES) {
    uint32_t now = millis();
    if (now < nextTick) { delay(1); continue; }
    nextTick += DT_MS;

    if ((tick % 50) == 0) {
      esp_task_wdt_reset(); // bump WDT ~ every 5s at 10 Hz
      SerialMon.printf("Wave collection progress: %d samples, %d seconds elapsed\n", dispCount, (now - start) / 1000);
    }

    float ax=0, ay=0, az=0, gx=0, gy=0, gz=0;
    readMPU6500(ax, ay, az, gx, gy, gz);

    // Basic sanity: discard if accel magnitude far from 1g
    float amag = sqrtf(ax*ax + ay*ay + az*az);
    if (fabsf(amag - 9.80665f) > 4.9f) { tick++; continue; }

    filter.updateIMU(gx, gy, gz, ax, ay, az);

    // Very-slow gravity tracker in body frame
    const float dt = 1.0f / FS_HZ;
    const float RC = 1.0f / (2.0f * PI * G_TRACK_FC_HZ);
    const float alpha = dt / (RC + dt);
    static float g_lp_x = 0.0f, g_lp_y = 0.0f, g_lp_z = 9.80665f;
    g_lp_x = (1.0f - alpha) * g_lp_x + alpha * ax;
    g_lp_y = (1.0f - alpha) * g_lp_y + alpha * ay;
    g_lp_z = (1.0f - alpha) * g_lp_z + alpha * az;

    // Specific force and projection along -g
    const float ax_spec = ax - g_lp_x;
    const float ay_spec = ay - g_lp_y;
    const float az_spec = az - g_lp_z;
    float gnorm = sqrtf(g_lp_x*g_lp_x + g_lp_y*g_lp_y + g_lp_z*g_lp_z);
    if (gnorm < 1e-3f) gnorm = 9.80665f;
    const float ux = g_lp_x / gnorm, uy = g_lp_y / gnorm, uz = g_lp_z / gnorm;
    float heaveAcc = -(ax_spec*ux + ay_spec*uy + az_spec*uz);
    if (fabsf(heaveAcc) < 0.001f) heaveAcc = 0.0f;
    if (heaveAcc > 5.0f) heaveAcc = 5.0f; else if (heaveAcc < -5.0f) heaveAcc = -5.0f;

    // Band-limit acceleration
    float a_heave = bandLimit(heaveAcc);

    // Integrate to velocity & displacement (trapezoidal)
    float v_new = v + 0.5f * (prev_a + a_heave) * dt;
    float x_new = x + 0.5f * (prev_v + v_new) * dt;
    prev_a = a_heave; prev_v = v_new; v = v_new; x = x_new;

    if (dispCount < MAX_SAMPLES) {
      dispBuf[dispCount] = x;
      aHeaveBuf[dispCount] = a_heave;
      dispCount++;
    }

    // Sample heading sparsely for direction label
    if ((tick % 10) == 0) {
      float hdg = getHeadingDegrees();
      if (!isnan(hdg)) { s_headingSum += hdg; s_headingCount++; }
    }

    tick++;
  }

  SerialMon.printf("Wave data collection complete: %d samples collected in %d seconds\n", dispCount, (millis() - start) / 1000);

  // If not enough samples, produce zeros
  if (dispCount < (uint32_t)(FS_HZ * 5.0f)) {
    s_lastHs = 0.0f; s_lastTp = 0.0f; s_lastWaves = 0; return;
  }

  // Detrend displacement over window (remove linear drift)
  double sumX = 0.0, sumI = 0.0, sumIX = 0.0, sumII = 0.0;
  for (uint32_t i = 0; i < dispCount; ++i) {
    sumX += dispBuf[i]; sumI += i; sumIX += (double)i * dispBuf[i]; sumII += (double)i * (double)i;
  }
  double n = (double)dispCount;
  double denom = (n * sumII - sumI * sumI);
  double slope = (denom != 0.0) ? (n * sumIX - sumI * sumX) / denom : 0.0;
  double intercept = (sumX - slope * sumI) / n;
  for (uint32_t i = 0; i < dispCount; ++i) dispBuf[i] = (float)(dispBuf[i] - (intercept + slope * i));

  // Ignore initial 5 s to avoid transients
  uint32_t settle = (uint32_t)(FS_HZ * 5.0f);
  if (settle >= dispCount) settle = dispCount / 5;
  const float* xbuf = dispBuf + settle;
  const float* abuf = aHeaveBuf + settle;
  uint32_t nuse = dispCount - settle;

  // Gating on acceleration RMS (calm sea)
  float rms_a = 0.0f;
  if (nuse > 1) { double ss = 0.0; for (uint32_t i = 0; i < nuse; ++i) ss += (double)abuf[i] * (double)abuf[i]; rms_a = sqrtf((float)(ss / (double)nuse)); }
  float maxAbsA = 0.0f; for (uint32_t i = 0; i < nuse; ++i) { float av = fabsf(abuf[i]); if (av > maxAbsA) maxAbsA = av; }
  if (rms_a < 0.01f && maxAbsA < 0.04f) { s_lastHs = 0.0f; s_lastTp = 0.0f; s_lastWaves = 0; return; }

  // DC removal + gentle band-pass on displacement
  double meanX = 0.0; for (uint32_t i = 0; i < nuse; ++i) meanX += xbuf[i]; meanX /= (double)nuse;
  static float workBuf[MAX_SAMPLES];
  for (uint32_t i = 0; i < nuse; ++i) {
    float xd = (float)(xbuf[i] - meanX);
    workBuf[i] = bandPassDisplacement(xd) * DISP_AMP_SCALE;
  }

  WaveStats ws = analyzeWaves(workBuf, nuse, FS_HZ);
  s_lastHs = ws.Hs_m; s_lastTp = ws.Tp_s; s_lastWaves = ws.wavesCount;
}

// Direction label based on average heading during sampling
static String directionFromAverage(float avgDeg) {
  const char* dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  int idx = (int)((avgDeg + 22.5f) / 45.0f) % 8;
  return String(dirs[idx]);
}

float computeWaveHeight() { return s_lastHs; }
float computeWavePeriod() { return s_lastTp; }
String computeWaveDirection() {
  if (s_headingCount == 0) return String("N/A");
  float avg = s_headingSum / (float)s_headingCount;
  if (avg < 0.0f) avg += 360.0f; if (avg >= 360.0f) avg -= 360.0f;
  return directionFromAverage(avg);
}
float computeWavePower(float height, float period) {
  return 0.49f * height * height * period; // deep-water proxy
}
void logWaveStats() {
  SerialMon.println("---- Wave Stats (last window) ----");
  SerialMon.printf("Samples: %u @ %.1f Hz, Waves detected: %u\n", dispCount, FS_HZ, s_lastWaves);
  SerialMon.printf("Hs (sig. height):    %.3f m\n", s_lastHs);
  SerialMon.printf("Tp (period):         %.2f s\n", s_lastTp);
  SerialMon.printf("Power proxy:         %.3f kW/m\n", computeWavePower(s_lastHs, s_lastTp));
  
  // Sea state categorization
  const char* seaState = "Unknown";
  if (s_lastHs < 0.02f) seaState = "No waves (Perfect conditions)";
  else if (s_lastHs < 0.06f) seaState = "Ripples (Very easy to swim)";
  else if (s_lastHs < 0.12f) seaState = "Light waves (OK waves)";
  else if (s_lastHs < 0.30f) seaState = "Medium waves (can be annoying)";
  else if (s_lastHs < 0.60f) seaState = "Large waves (uncomfortable)";
  else seaState = "Too large waves (storm)";
  SerialMon.printf("Sea state:           %s\n", seaState);
  
  // Diagnostic: average |a_heave| over the window segment used
  if (dispCount > 0) {
    uint32_t settle = (uint32_t)(FS_HZ * 5.0f);
    if (settle >= dispCount) settle = dispCount / 5;
    uint32_t nuse = dispCount - settle;
    if (nuse > 0) {
      double sumAbs = 0.0;
      for (uint32_t i = settle; i < dispCount; ++i) {
        sumAbs += fabsf(aHeaveBuf[i]);
      }
      float meanAbsA = (float)(sumAbs / (double)nuse);
      SerialMon.printf("Heave |a| mean:      %.4f m/s²\n", meanAbsA);
    }
  }
  
  if (!imuInitialized) {
    SerialMon.println("WARNING: MPU6500 data not available - wave readings may be zero");
  }
  SerialMon.println("----------------------------------");
}
