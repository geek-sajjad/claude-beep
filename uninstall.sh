#!/usr/bin/env bash
# Remove everything install.sh put in place.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
prefix="${PREFIX:-$HOME/.local}"

systemctl --user disable --now claude-beep.service 2>/dev/null || true
rm -f "$HOME/.config/systemd/user/claude-beep.service"
systemctl --user daemon-reload

"$here/scripts/install-hooks.py" --remove

rm -f "$prefix/bin/claude-beep"
rm -rf "$prefix/share/claude-beep"
rm -rf "${XDG_RUNTIME_DIR:-/tmp}/claude-beep"

echo "Removed. ~/.config/claude-beep was left alone."
