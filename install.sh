#!/usr/bin/env bash
# Build claude-beep, install it for the current user, start the tray indicator
# and wire up the Claude Code hooks. Safe to re-run.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
prefix="${PREFIX:-$HOME/.local}"
icons="$prefix/share/claude-beep/icons"
unit_dir="$HOME/.config/systemd/user"

echo "==> building"
cmake -S "$here" -B "$here/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$here/build" -j"$(nproc)"

echo "==> installing to $prefix"
install -Dm755 "$here/build/claude-beep" "$prefix/bin/claude-beep"
install -d "$icons"
install -m644 "$here"/icons/*.svg "$icons/"

echo "==> installing the user service"
install -Dm644 "$here/systemd/claude-beep.service" "$unit_dir/claude-beep.service"
systemctl --user daemon-reload
systemctl --user enable --now claude-beep.service

echo "==> wiring up the Claude Code hooks"
"$here/scripts/install-hooks.py"

case ":$PATH:" in
    *":$prefix/bin:"*) ;;
    *) echo "note: $prefix/bin is not on your PATH" ;;
esac

echo
echo "Done. A green dot should now be in the top bar."
echo "Restart any running Claude Code session (or open /hooks once) to load the hooks."
