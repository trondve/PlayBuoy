#!/bin/bash
# Block edits that reduce SIM7000G datasheet timing minimums
# These timings are verified safe — reducing them risks modem failure or brownout
INPUT=$(cat)
FILE_PATH=$(echo "$INPUT" | jq -r '.tool_input.file_path // empty')
NEW_STRING=$(echo "$INPUT" | jq -r '.tool_input.new_string // .tool_input.content // empty')

# Only check modem/GPS related files
case "$FILE_PATH" in
  *modem.cpp|*modem.h|*gps.cpp|*gps.h|*main.cpp) ;;
  *) exit 0 ;;
esac

# PWRKEY power-on pulse: must be ≥1000ms (we use 2000ms)
# Check for PWRKEY-adjacent delays reduced below 1000ms
if echo "$NEW_STRING" | grep -qP 'PWRKEY.*delay\s*\(\s*[1-9][0-9]{0,2}\s*\)'; then
  echo "BLOCKED: PWRKEY pulse appears to be <1000ms. SIM7000G datasheet minimum is 1000ms for power-on. Current safe value: 2000ms." >&2
  exit 2
fi

# PWRKEY power-off pulse: must be ≥1200ms (we use 1300ms)
if echo "$NEW_STRING" | grep -qP '(power.?off|PWRKEY.*off).*delay\s*\(\s*([1-9][0-9]{0,2}|1[01][0-9]{2})\s*\)'; then
  echo "BLOCKED: Power-off PWRKEY pulse appears to be <1200ms. SIM7000G datasheet minimum is 1200ms. Current safe value: 1300ms." >&2
  exit 2
fi

exit 0
