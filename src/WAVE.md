# Wave Measurement — Local Context

## Purpose
Measure significant wave height (Hs) and peak period (Tp) using FFT spectral analysis of accelerometer data. Replaces time-domain double integration which suffered from drift.

## What can go wrong
- **Reducing sample duration below 2 min**: FFT frequency resolution = fs/N. At 10Hz with 1024 points, resolution is ~0.01Hz. Shorter windows reduce spectral resolution and miss longer wave periods (>10s).
- **Changing FFT_N from 1024**: Must be power of 2 for radix-2 Cooley-Tukey. 512 halves resolution. 2048 needs 16KB RAM and 204s of data (collection would need to increase from 160s).
- **Wrong scale factors**: IMU registers use fixed-point. ±2g range: exact scale = 9.80665/16384 m/s² per LSB. Wrong values = wrong wave heights.
- **Gravity tracker drift**: The 0.02Hz low-pass gravity tracker takes ~50s to converge. The first ~576 samples are transient — that's why we collect 1600 samples but FFT uses only the last 1024.
- **Sanity cap too low/high**: Hs > 2.0m → 0 (noise on lakes). For ocean deployment, this needs raising to 10-15m. Hardcoded in `spectralAnalysis()`.

## Signal processing pipeline
```
IMU (10Hz, 160s)
  → MPU6500: ax, ay, az in physical units (accel only, no gyro)
  → Gravity tracker: 0.02Hz LP on acceleration → slowly tracks g vector
  → Specific force: accel - gravity_estimate
  → Heave: project specific force onto gravity direction
  → IIR bandpass: 0.05-2.0Hz (anti-alias for FFT)
  → Store in accelBuf[1600]

FFT spectral analysis (last 1024 samples)
  → Remove DC mean
  → Hanning window (correction factor 8/3)
  → 1024-point radix-2 FFT (in-place, Cooley-Tukey)
  → Acceleration PSD = 2|X(f)|² / (N·fs) × window_correction
  → Displacement PSD = Accel PSD / (2πf)⁴
  → m₀ = ∫ displacement_PSD df  (over 0.05-1.0 Hz)
  → Hs = 4·√m₀  (standard oceanographic definition)
  → Tp = 1/f_peak  (parabolic interpolation for sub-bin accuracy)
  → Power = 0.49 · Hs² · Tp  (deep-water approximation)
```

## Memory layout
- `accelBuf[1600]`: 6.4KB — heave acceleration samples, reused as FFT input
- `fftIm[1024]`: 4.1KB — imaginary part of FFT (static allocation)
- Total: ~10.5KB static RAM for wave processing

## Additional outputs
- **Mean tilt**: Angle between gravity vector and vertical, averaged over 160s
- **Accel RMS**: Root-mean-square of heave acceleration (proxy for sea state energy)
- **Motion gate**: If RMS < 0.01 m/s² AND mean|a| < 0.005, skip FFT → report calm

## IMU configuration (MPU6500/9250)
- Address: 0x68, WHO_AM_I: 0x70 (MPU6500) or 0x71/0x73 (MPU9250)
- DLPF: config register 0x03 (~44Hz bandwidth)
- Accel: ±2g (register 0x00)
- Gyro: not configured (not needed — removed with Mahony AHRS)
- Sample rate divider: 99 (1kHz base / 100 = 10Hz)
- Magnetometer: not used (broken in sealed enclosure)

## Rules
- Never reduce collection time below 2 minutes (1200 samples minimum for FFT_N=1024)
- Never change FFT_N without recalculating memory and frequency resolution
- Never change IMU scale factors without checking the datasheet register values
- Wave direction is always "N/A" — magnetometer doesn't work through the sealed case
