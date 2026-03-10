# Code Review Skill

## Purpose
Perform comprehensive code reviews for PlayBuoy firmware, checking for:
- Power consumption issues
- Safety violations (brownout, battery)
- Timing violations per datasheets
- Memory leaks and inefficiencies
- Test coverage
- Documentation gaps

## Execution Steps

1. **Read the CLAUDE.md** for project context
2. **Check against critical constraints:**
   - Never run out of power (permanently sealed)
   - Stability over features
   - Correct datasheets/timings
   - Water temperature accuracy
   - Accurate timestamps (NTP)
   - Minimize firmware size (OTA cellular)

3. **Review modified files:**
   - Check power consumption patterns
   - Verify timing against SIM7000G datasheet
   - Ensure brownout protection
   - Validate battery thresholds
   - Check for timing violations

4. **Generate findings:**
   - Safety issues (high priority)
   - Performance improvements
   - Code quality issues
   - Documentation gaps

## Usage Example
```
/code-review <file-or-directory>
```

## Key Areas
- `src/main.cpp` - Boot cycle, power management
- `src/battery.cpp` - Battery voltage, sleep logic
- `src/power.cpp` - ADC measurement, accuracy
- `src/gps.cpp` - Timing, NTP sync, XTRA
- `src/wave.cpp` - FFT analysis, memory efficiency
