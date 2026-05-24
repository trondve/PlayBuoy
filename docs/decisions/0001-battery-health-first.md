# Decision: Battery Health-First Power Strategy

## Date
2026-03-10

## Status
Adopted

## Context
PlayBuoy is a permanently sealed IoT buoy deployed in harsh environments (lakes, ocean). Once deployed, battery failure = device loss (cannot be recovered). The 2× 18650 Li-ion cells must survive 6+ months (winter) or 12+ months (optimal conditions).

## Problem
Traditional IoT devices prioritize:
1. Frequent updates
2. Responsive operation
3. Feature completeness

This approach fails for sealed devices in winter darkness with minimal solar harvest (0.1-0.5 Wh/day).

## Decision
Adopt a **battery health-first** strategy:

### Operating Principles
1. **Preserve optimal range (40-60% SoC)** when possible
2. **Never charge above 80%** (high SoC accelerates aging)
3. **Never discharge below 20%** (irreversible damage)
4. **Critical guard at 25%/3.70V** (safety margin for aged cells)
5. **Conservative sleep schedule** prioritizes survival over data frequency

### Sleep Schedule Implementation
Three-season model (see `src/battery.cpp`):
- **Summer (Jun–Aug)**: Aggressive data collection (2–9h cycles) when solar is abundant
- **Shoulder (Apr–May, Sep–Oct)**: Moderate cycles (6–18h) during transition seasons
- **Winter (Nov–Mar)**: Deep hibernation (12h–3 month cycles) when solar is minimal
- **Fallback to winter** if RTC time is invalid (safer than guessing summer)

### Charging Philosophy
- **≥75% SoC**: Charge dump (DumpMode) — intentionally short cycles to burn excess energy:
  - TIER1 (75–85%): 60 min summer / 6h shoulder+winter, normal full cycle
  - TIER2 (85–90%): 60 min, full cycle including GPS, overrides quiet hours
  - TIER3 (90–95%): 30 min, two back-to-back cycles, overrides quiet hours
  - TIER4 (≥95%): 15 min, two back-to-back cycles, overrides quiet hours
- **40–60% SoC**: Sustainable (6–24h cycles depending on season)
- **<40% SoC**: Conservation mode (24h+ to allow recharge)

### Brownout Protection
If battery sagged under modem load → brownout reset:
- Skip full cycle (modem + GPS would cause another brownout)
- Measure voltage immediately
- Sleep if <40% SoC (conservative recovery)
- Otherwise proceed (battery recovered)

## Rationale
- **Longevity**: Lithium cells at 40-60% SoC last 2-3× longer than at full charge
- **Survival**: Conservative winter schedule ensures 3+ months hibernation capability
- **Safety**: 3.70V critical guard provides margin for aged cells & voltage sag
- **Recovery**: Brownout fast-track prevents reset loops

## Trade-offs
✗ Lower data frequency in winter (1–4 readings/day vs. 24+ in summer)
✗ Delayed response to critical changes (temperature, wave height)
✗ DumpMode adds complexity (4 tiers, quiet-hour overrides, back-to-back cycles)
✓ Guaranteed survival through months of darkness
✓ Preserved cell health for multi-year operation

## Implementation
See `src/battery.cpp`:
- `determineSleepDuration(batteryPercent)` — season & SoC aware
- `handleUndervoltageProtection()` — critical guard at 3.70V
- `estimateBatteryPercent(voltage)` — OCV table for 18650

See `src/main.cpp`:
- `setup()` brownout detection → fast-track sleep if <40%

## Monitoring
- Log battery voltage trend (should stay 40-60% optimal)
- Monitor sleep duration (adapts to season/charge state)
- Alert on brownout recovery events
- Track boot count (reveals charge/discharge patterns)

## Related
- Decision 0002: Deep sleep and power cycle optimization
- CLAUDE.md: Battery health guidelines section
