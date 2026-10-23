#pragma once

#include <string>

namespace cb {

// Entry point for the Claude Code hooks. Reads the hook's JSON payload from
// stdin, updates this session's state file and plays a cue when appropriate.
// Always exits 0 so a hook can never break a Claude session.
int runNotify(const std::string& event);

} // namespace cb
