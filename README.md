# claude-beep

A coloured dot in the GNOME top bar that tells you what Claude Code is doing,
plus a short beep when it finishes or needs you. No desktop notifications.

| Dot | Meaning |
|-----|---------|
| 🟢 green | every session is idle — Claude is free |
| 🔴 red | at least one session is working |
| 🟡 yellow | a session is waiting on you (permission prompt, question) |

Sounds: `complete.oga` when a turn finishes, `message.oga` when Claude asks for
something. Both fire once per transition, and never when the tray menu's
**Mute beeps** is ticked.

## How it works

Claude Code hooks run `claude-beep notify <event>`, which writes a tiny state
file per session into `$XDG_RUNTIME_DIR/claude-beep/` and plays the cue. The
indicator watches that directory and shows the aggregate: yellow beats red beats
green, so anything asking for input wins the panel.

```
claude session ──hook──> claude-beep notify ──> $XDG_RUNTIME_DIR/claude-beep/<id>.session
                                │                          │
                                └── pw-play (beep)          └── inotify ──> claude-beep indicator ──> top bar
```

Each state file records the pid of the owning `claude` process. The indicator
drops files whose process is gone, so a session killed with `kill -9` cannot
leave the dot stuck on red.

The menu is built once at startup with a fixed set of rows and is never
replaced: updates edit labels and row visibility in place. Replacing a
StatusNotifier menu makes the shell tear down whatever it is showing, so
rebuilding it on every update made the menu flicker while it was open. Editing
items instead travels as a dbusmenu `ItemsPropertiesUpdated`, which the shell
applies to an open menu without closing it, and an update that changes nothing
sends no D-Bus traffic at all.

### Event mapping

| Claude Code hook | claude-beep event | Effect |
|---|---|---|
| `SessionStart` | `session-start` | idle |
| `UserPromptSubmit` | `prompt` | busy |
| `PostToolUse` | `tool` | busy (clears yellow once you approve a tool) |
| `Notification` | `notification` | waiting + beep |
| `Stop` | `stop` | idle + beep |
| `SessionEnd` | `session-end` | forget the session |

`Notification` also fires when Claude has simply been idle for a while waiting
for input. That one is ignored: a notification only turns the dot yellow if the
session was actually working.

## Install

```sh
./install.sh
```

That builds the binary, installs it to `~/.local/bin`, installs the icons, and
enables a systemd user service so the indicator starts with your desktop every
day. It also merges the hooks into `~/.claude/settings.json`, keeping whatever
is already there (a backup is written next to it).

Restart any Claude Code session that was already running so it picks up the
hooks.

```sh
./uninstall.sh   # removes the binary, service and hooks
```

## Requirements

Ubuntu with GNOME and the `ubuntu-appindicators` extension enabled (default on
Ubuntu). Build-time: `cmake`, `g++`, `libgtk-3-dev`,
`libayatana-appindicator3-dev`. Runtime: `pw-play`, `paplay` or
`canberra-gtk-play`.

## Commands

```sh
claude-beep status          # what every live session is doing
claude-beep prune           # drop state left behind by dead sessions
claude-beep mute | unmute   # same as the tray menu toggle
claude-beep test ask        # play a cue
claude-beep indicator       # run the tray indicator in the foreground
```

## Configuration

`~/.config/claude-beep/config`, all keys optional:

```ini
sound_done = /usr/share/sounds/freedesktop/stereo/complete.oga
sound_ask  = /usr/share/sounds/freedesktop/stereo/message.oga
player     = /usr/bin/pw-play
```

Muting is a separate flag file (`~/.config/claude-beep/muted`) so the tray
toggle never rewrites your config.

## Troubleshooting

```sh
systemctl --user status claude-beep      # is the indicator running?
claude-beep status                       # is the hook writing state?
echo '{"session_id":"x"}' | claude-beep notify stop   # does the beep work?
```

If the dot never appears, check that the appindicator extension is on:
`gnome-extensions list | grep appindicator`.
