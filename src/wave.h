#pragma once

#include <Arduino.h>

//
// Wave spectral analysis via FFT (1024-point, 10Hz IMU sampling).
// Pipeline: acquire 100s accel samples → IIR bandpass filter → FFT → spectral integration.
// Computes significant wave height (Hs) and peak period (Tp) from displacement spectrum.
// Replaces legacy time-domain double-integration approach (eliminates drift).
//
// ALGORITHM:
// 1. Raw acceleration measured at 10Hz on MPU6500 Z-axis (vertical)
// 2. Low-pass filter (highpass + lowpass) to isolate wave motion (0.05-2 Hz passband)
// 3. Gravity tracking via slow low-pass filter (0.02 Hz) to remove DC offset
// 4. 1024-point FFT on 100-second window (yielding ~1024 bins, df ≈ 0.01 Hz)
// 5. Convert acceleration spectrum to displacement via ω⁴ division (frequency domain)
// 6. Zero bins below WAVE_FREQ_MIN (0.05 Hz) to prevent low-freq blowup (H-08 fix)
// 7. Integrate spectrum: m₀ = ∫ PSD df → Hs = 4√m₀ (oceanographic standard)
// 8. Find peak frequency f_peak via binary search, refine via parabolic interpolation
// 9. Tp = 1 / f_peak (peak period in seconds)
//
// REFERENCE DOCUMENTS:
// - IEC 61025:2017 (Wave height statistical definitions)
// - USGS Wave Spectra Analysis Guide
// - Welch's method for spectral estimation (overlapped, windowed segments)
//

//
// Acquires 100 seconds of heave acceleration samples at 10Hz from IMU.
// Performs gravity removal, IIR filtering, and prepares for FFT analysis.
// Updates global s_lastHs, s_lastTp, s_tiltSum, s_accelRms for getter functions.
// Must be called while 3.3V rail is powered (sensors depend on GPIO 25).
// Duration: ~100 seconds of active I2C sampling + FFT (~2 seconds).
//
void recordWaveData();

//
// Returns most recently computed significant wave height (meters).
// Based on Hs = 4 * sqrt(m0) where m0 is zeroth moment (variance) of displacement spectrum.
// Valid range: 0.1-10 m (capped at WAVE_HS_MAX_M, default 2.0m for lakes).
// Recomputes from FFT results on each call (not cached).
// Returns: 0.0 if no valid data or NaN occurs.
//
float computeWaveHeight();

//
// Returns most recently computed peak wave period (seconds).
// Based on Tp = 1 / f_peak where f_peak is frequency of maximum energy.
// Valid range: 1-30 seconds (typical oceanographic range).
// Recomputes from FFT results on each call (not cached).
// Returns: 0.0 if no valid data.
//
float computeWavePeriod();

//
// Returns wave direction as cardinal string (N, NE, E, SE, etc.).
// NOTE: Currently hardcoded to "N/A" because magnetometer is dead
// (ferrite seals and electronic noise in sealed enclosure prevent accurate compass).
// Future: could infer from multiaxial IMU and time-series correlation if needed.
// Returns: "N/A" (always).
//
String computeWaveDirection();

//
// Computes wave power from height and period.
// Power = (rho * g² / 64 * pi) * Hs² * Tp
// where rho = 1025 kg/m³ (seawater density), g = 9.81 m/s².
// Units: kW per meter of wave front width.
// This is a diagnostic metric (not used in sleep scheduling).
// Returns: power in kW/m, 0.0 if inputs invalid.
//
float computeWavePower(float height, float period);

//
// Returns mean buoy tilt angle (degrees from vertical) during sampling window.
// Computed as arctan(sqrt(ax²+ay²) / az) averaged over 100s window.
// Indicates buoy platform orientation (0° = vertical, 90° = horizontal).
// Used in diagnostics to detect tilt sensor failure or physical damage.
// Returns: 0-90 degrees, or 0.0 if not computed.
//
float computeMeanTilt();

//
// Returns RMS acceleration (m/s²) during sampling window.
// Proxy for overall wave energy magnitude independent of frequency content.
// Sqrt of mean of (ax² + ay² + az²) over entire window.
// Used for diagnostics and buoy health assessment.
// Returns: 0-50 m/s² typical for ocean waves, ~0.5 m/s² for lakes.
//
float computeAccelRms();

//
// Logs FFT results and wave statistics to Serial for debugging.
// Prints: wave height, peak period, peak frequency, spectral moments, etc.
// Called after recordWaveData() to show what the FFT pipeline computed.
//
void logWaveStats();
