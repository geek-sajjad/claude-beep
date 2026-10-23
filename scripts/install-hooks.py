#!/usr/bin/env python3
"""Merge the claude-beep hooks into ~/.claude/settings.json, idempotently.

Existing settings and existing hooks are preserved; the claude-beep entries are
recognised by their command and replaced in place on re-runs. Pass --remove to
take them out again.
"""

import json
import os
import shutil
import sys

BIN = os.path.expanduser("~/.local/bin/claude-beep")
SETTINGS = os.path.expanduser("~/.claude/settings.json")

# Claude Code hook event -> claude-beep event
EVENTS = {
    "SessionStart": "session-start",
    "UserPromptSubmit": "prompt",
    "PostToolUse": "tool",
    "Notification": "notification",
    "Stop": "stop",
    "SessionEnd": "session-end",
}


def entry(event):
    return {
        "hooks": [
            {
                "type": "command",
                "command": f"{BIN} notify {event}",
                "timeout": 5,
            }
        ]
    }


def is_ours(group):
    return any(
        h.get("type") == "command" and "claude-beep notify" in h.get("command", "")
        for h in group.get("hooks", [])
    )


def main():
    remove = "--remove" in sys.argv

    settings = {}
    if os.path.exists(SETTINGS):
        with open(SETTINGS) as f:
            settings = json.load(f)
        shutil.copyfile(SETTINGS, SETTINGS + ".claude-beep.bak")

    hooks = settings.setdefault("hooks", {})
    for claude_event, our_event in EVENTS.items():
        groups = [g for g in hooks.get(claude_event, []) if not is_ours(g)]
        if not remove:
            groups.append(entry(our_event))
        if groups:
            hooks[claude_event] = groups
        else:
            hooks.pop(claude_event, None)
    if not hooks:
        settings.pop("hooks", None)

    os.makedirs(os.path.dirname(SETTINGS), exist_ok=True)
    with open(SETTINGS, "w") as f:
        json.dump(settings, f, indent=2)
        f.write("\n")

    print(("removed" if remove else "installed") + f" claude-beep hooks in {SETTINGS}")


if __name__ == "__main__":
    main()
