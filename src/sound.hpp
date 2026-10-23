#pragma once

#include <string>

namespace cb {

enum class Cue { Done, Ask };

// Plays the cue in a detached child process and returns immediately, so a hook
// never blocks Claude while audio plays. No-op when muted.
void playCue(Cue cue);

// Resolved from ~/.config/claude-beep/config, with freedesktop defaults.
std::string soundFor(Cue cue);

} // namespace cb
