# Testing & Validation Skill

## Purpose
Support testing and validation for PlayBuoy firmware:
- Unit test design for critical functions
- Integration test planning
- Field deployment checklists
- Data validation
- Battery health monitoring

## Test Areas

### Unit Tests (Critical)
- **Power measurement** (`power.cpp`): ADC accuracy, calibration, burst logic
- **Battery state** (`battery.cpp`): OCV table lookup, percentage estimation, sleep duration
- **Temperature measurement** (`sensors.cpp`): Retry logic, validation, range checks
- **Wave data** (`wave.cpp`): FFT analysis, spectral moments, sanity caps
- **GPS/Time** (`gps.cpp`): NTP sync, XTRA download, GNSS fix timeout

### Integration Tests
- **Boot cycle**: Wake → measure battery → check guard → collect wave data → GPS fix
- **Deep sleep**: Pin state, power domain config, GPIO holds
- **Modem**: Power on/off sequences, network registration, OTA updates
- **Brownout recovery**: Battery measurement → sleep decision

### Field Validation
- [ ] Battery voltage measurements vs. calibration data
- [ ] Temperature readings in known conditions
- [ ] Wave data during different sea states
- [ ] GPS fix timing and HDOP quality
- [ ] OTA firmware update and rollback
- [ ] Brownout protection in low light

## Deployment Checklist

Before field deployment:
1. [ ] FirmwareVersion bumped in config.h
2. [ ] CLAUDE.md updated with any architectural changes
3. [ ] Power projections for deployment season (summer/winter)
4. [ ] Battery health parameters reviewed
5. [ ] Critical guard thresholds appropriate for cell chemistry
6. [ ] OTA server URLs configured
7. [ ] NTP/XTRA servers reachable
8. [ ] Cellular coverage at deployment location verified
9. [ ] Solar panel angle optimized for latitude
10. [ ] All safety features enabled (brownout, battery guard)

## Monitoring in Field
- Watch battery trending (should stay 40-60% optimal range)
- Monitor GPS fix quality (HDOP, TTF)
- Check for anomalies in temperature or wave data
- Verify upload frequency matches sleep schedule
- Log any brownout or battery guard triggers
