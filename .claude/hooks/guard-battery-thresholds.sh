#!/bin/bash
# Block edits that weaken the critical battery guard (3.70V / 25% SoC)
# These thresholds protect a sealed buoy from bricking itself
INPUT=$(cat)
FILE_PATH=$(echo "$INPUT" | jq -r '.tool_input.file_path // empty')
NEW_STRING=$(echo "$INPUT" | jq -r '.tool_input.new_string // .tool_input.content // empty')

# Only check files that contain battery guard logic
case "$FILE_PATH" in
  *main.cpp|*battery.cpp|*battery.h|*power.cpp|*power.h) ;;
  *) exit 0 ;;
esac

# Check if the edit introduces a lower voltage threshold
if echo "$NEW_STRING" | grep -qP 'CRITICAL.*[0-2]\.[0-9]+|CRITICAL.*3\.[0-6][0-9]*[^0-9]'; then
  echo "BLOCKED: Edit appears to lower the critical voltage threshold below 3.70V. The sealed buoy needs this guard to survive. SIM7000G requires ≥3.55V under 2A peak load." >&2
  exit 2
fi

# Check if the edit introduces a lower SoC threshold
if echo "$NEW_STRING" | grep -qP 'CRITICAL.*(1[0-9]|[0-9])%|CRITICAL_SOC.*(1[0-9]|[0-9])[^0-9]'; then
  echo "BLOCKED: Edit appears to lower the critical SoC threshold below 25%. 18650 cells suffer irreversible damage below 20% SoC." >&2
  exit 2
fi

exit 0
