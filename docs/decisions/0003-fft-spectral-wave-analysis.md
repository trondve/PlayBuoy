# Decision: FFT Spectral Analysis for Wave Measurement

## Date
2026-03-09

## Status
Adopted

## Context
The original wave measurement used time-domain double integration of accelerometer data (acceleration → velocity → displacement). This approach suffered from integration drift, required a fudge factor (`DISP_AMP_SCALE`), and produced unreliable wave height estimates.

## Decision
Replace time-domain double integration with FFT spectral analysis:
- Collect 160s of heave acceleration at 10Hz (1600 samples)
- Use last 1024 samples for 1024-point FFT (first 576 samples discarded — gravity tracker settling)
- IIR bandpass pre-filter at 0.03–2.0Hz (HP below WAVE_FREQ_MIN to avoid attenuation at band edge)
- Hanning window (8/3 power correction) → FFT → acceleration PSD → displacement PSD via 1/(2πf)⁴
- Hs = 4·√m₀ (standard oceanographic definition)
- Tp = 1/f_peak (parabolic interpolation on displacement PSD for sub-bin accuracy)
- Wave band: 0.05–1.0 Hz (periods 1–20s)

## Rationale
- FFT is drift-free — no accumulated error from double integration
- No fudge factor needed — Hs = 4·√m₀ is the accepted oceanographic standard
- Frequency-domain filtering cleanly separates wave energy from noise
- ~160s (2:40) sampling window, no additional power cost vs prior implementation

## Trade-offs
- 1024-point FFT requires ~8KB RAM (acceptable on ESP32 with 520KB)
- Frequency resolution limited to ~0.01Hz (sufficient for wave periods 1–20s)
- Mahony AHRS filter is redundant and has been removed (gravity tracker handles orientation)
- Sanity caps (`WAVE_HS_MAX_M`, `WAVE_TP_MAX_S`) configurable per deployment — defaults for lakes

## Implementation
- `src/wave.cpp`: FFT pipeline, spectral moment calculation
- Sanity cap in `wave.cpp`: Hs > 2.0m treated as noise
