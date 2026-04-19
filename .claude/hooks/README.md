# PlayBuoy Hooks — Automated Guardrails

Models forget. Hooks don't.
These run automatically on every tool call — no prompt needed.

## Active Hooks

| Hook | Event | Action | Why |
|------|-------|--------|-----|
| `block-config-secrets.sh` | PreToolUse (Edit/Write) | **BLOCK** edits to `config.h` | Contains API keys, must stay gitignored |
| `guard-battery-thresholds.sh` | PreToolUse (Edit/Write) | **BLOCK** lowering critical guard | ≤3.70V / ≤25% protects sealed buoy from death |
| `guard-datasheet-timings.sh` | PreToolUse (Edit/Write) | **BLOCK** reducing modem timings | SIM7000G PWRKEY minimums are non-negotiable |
| `warn-simultaneous-subsystems.sh` | PreToolUse (Edit/Write) | **WARN** if modem+GPS both referenced | 2A modem + GPS = brownout on 18650s |
| `remind-decision-log.sh` | Stop | **REMIND** to log decisions | Architecture decisions get lost if not recorded |

## How hooks work

- **Exit 0** = allow the action
- **Exit 2** = block the action (reason sent to Claude via stderr)
- Hooks receive tool input as JSON on stdin
- Configured in `.claude/settings.json` under `hooks`
