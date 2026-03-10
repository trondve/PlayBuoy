# Power & Performance Optimization Skill

## Purpose
Conduct thorough audits of power consumption, timing, and performance in PlayBuoy:
- Deep sleep optimization
- Wake cycle efficiency
- Power measurement accuracy and speed
- Modem timing and power cycles
- Sensor power management
- Battery health strategies

## Audit Checklist

### Deep Sleep Phase
- [ ] All unused power domains OFF (RTC periph, XTAL, fast mem)
- [ ] GPIO 25 (3V3 rail) held LOW via `gpio_hold_en`
- [ ] All modem/I2C/sensor pins set to INPUT (high-Z)
- [ ] Brownout recovery handling
- [ ] Wake time scheduling

### Wake Cycle
- [ ] Boot delays (startup, BT release, etc.)
- [ ] Battery measurement timing and accuracy
- [ ] 3V3 rail power and settle times
- [ ] Sensor initialization and sampling
- [ ] Wave data collection (3 min baseline)
- [ ] Modem pre-cycle efficiency
- [ ] GPS fix timeout strategy
- [ ] Cellular registration and upload timing

### Power Measurement
- [ ] ADC burst count and inter-burst delays
- [ ] Sample averaging (noise reduction)
- [ ] Calibration method (`esp_adc_cal`)
- [ ] Spread logging for diagnostics
- [ ] Divider ratio accuracy
- [ ] Critical guard thresholds

## Output Format
- Produce a detailed audit report with:
  - Current state vs. best practices
  - Estimated power savings (mAh/cycle)
  - Safety margin analysis
  - Recommendations (conservative approach)
  - Implementation steps with minimal risk

## Notes
- Conservative timing: never risk brownout
- Battery health: 40-60% optimal range
- Winter survival: 3+ months minimum
- Preserve critical features (GPS accuracy, wave data quality)
