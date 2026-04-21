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
- Use last 1024 samples for 1024-point FFT
- Hanning window → FFT → acceleration PSD → displacement PSD via 1/(2πf)⁴
- Hs = 4·√m₀ (standard oceanographic definition)
- Tp = 1/f_peak (period of peak spectral density)
- Wave band: 0.05–1.0 Hz (periods 1–20s)

## Rationale
- FFT is drift-free — no accumulated error from double integration
- No fudge factor needed — Hs = 4·√m₀ is the accepted oceanographic standard
- Frequency-domain filtering cleanly separates wave energy from noise
- Same 160s sampling window, no additional power cost

## Trade-offs
- 1024-point FFT requires ~8KB RAM (acceptable on ESP32 with 520KB)
- Frequency resolution limited to ~0.01Hz (sufficient for wave periods 1–20s)
- Mahony AHRS filter is now redundant (gravity tracker does orientation independently)
- Sanity cap at Hs > 2.0m appropriate for lakes, needs raising for ocean deployment

## Implementation
- `src/wave.cpp`: FFT pipeline, spectral moment calculation
- Sanity cap in `wave.cpp`: Hs > 2.0m treated as noise
