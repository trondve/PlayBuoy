#!/bin/bash
# Block edits to config.h (contains API keys, gitignored)
INPUT=$(cat)
FILE_PATH=$(echo "$INPUT" | jq -r '.tool_input.file_path // empty')

if [[ "$FILE_PATH" == *"config.h" && "$FILE_PATH" != *"config.h.example" ]]; then
  echo "BLOCKED: config.h contains API keys and secrets. Edit config.h.example instead." >&2
  exit 2
fi

exit 0
