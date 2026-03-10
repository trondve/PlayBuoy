#!/bin/bash
# Warn if edit to main.cpp might power modem and GPS simultaneously
# SIM7000G draws up to 2A peak — two subsystems will brownout the 18650s
INPUT=$(cat)
FILE_PATH=$(echo "$INPUT" | jq -r '.tool_input.file_path // empty')
NEW_STRING=$(echo "$INPUT" | jq -r '.tool_input.new_string // .tool_input.content // empty')

# Only check main boot cycle
case "$FILE_PATH" in
  *main.cpp) ;;
  *) exit 0 ;;
esac

# Check if edit contains both modem power-on and GPS power-on without shutdown between
if echo "$NEW_STRING" | grep -q 'MODEM_POWER' && echo "$NEW_STRING" | grep -q 'GPS_POWER\|GNSS'; then
  echo "WARNING: This edit references both modem and GPS power. Verify they are NEVER powered simultaneously — SIM7000G 2A peak + GPS will brownout the battery." >&2
  # Don't block, just warn (exit 0, not exit 2)
  exit 0
fi

exit 0
