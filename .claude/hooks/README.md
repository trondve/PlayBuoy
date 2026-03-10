# Claude Code Hooks

Hooks are shell commands that execute in response to Claude Code events (tool calls, session start, etc.).

## Available Hook Points

- `PreToolUse` — runs before a tool is executed
- `PostToolUse` — runs after a tool is executed
- `SessionStart` — runs when a new session begins

## Configuration

Hooks are configured in `.claude/settings.json` under the `hooks` key. Example:

```json
{
  "hooks": {
    "PreToolUse": [
      {
        "matcher": "Bash",
        "command": "./tools/scripts/pre-bash-check.sh"
      }
    ]
  }
}
```

## PlayBuoy-Specific Hooks

Add hooks here for:
- Pre-commit firmware version validation
- Build script automation
- Config.h secret detection (prevent committing API keys)
