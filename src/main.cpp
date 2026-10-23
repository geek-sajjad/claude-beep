#include "indicator.hpp"
#include "notify.hpp"
#include "sound.hpp"
#include "state.hpp"

#include <cstdio>
#include <iostream>
#include <string>

namespace {

int usage() {
    std::cout <<
        "claude-beep - GNOME top-bar status light + beep for Claude Code\n"
        "\n"
        "  claude-beep indicator            run the tray indicator (systemd user service)\n"
        "  claude-beep notify <event>       hook entry point; reads hook JSON on stdin\n"
        "  claude-beep status               print the current state\n"
        "  claude-beep prune                drop state left by dead sessions\n"
        "  claude-beep mute | unmute        silence or restore the beeps\n"
        "  claude-beep test [done|ask]      play a cue\n"
        "\n"
        "events: session-start prompt tool notification stop session-end\n";
    return 0;
}

int printStatus() {
    const std::vector<cb::Session> sessions = cb::loadSessions(/*prune=*/false);
    std::cout << "overall: " << cb::statusName(cb::aggregate(sessions))
              << "  (muted: " << (cb::muted() ? "yes" : "no") << ")\n"
              << "state dir: " << cb::stateDir() << "\n";
    for (const cb::Session& s : sessions) {
        std::printf("  %-8s pid=%-8ld %s\n", cb::statusName(s.status), s.pid, s.cwd.c_str());
    }
    if (sessions.empty()) std::cout << "  (no sessions)\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";

    if (command == "indicator") return cb::runIndicator();
    if (command == "notify") {
        if (argc < 3) {
            std::cerr << "claude-beep notify: missing event\n";
            return 0;  // never fail a hook
        }
        return cb::runNotify(argv[2]);
    }
    if (command == "status") return printStatus();
    if (command == "prune") { cb::loadSessions(/*prune=*/true); return printStatus(); }
    if (command == "mute") { cb::setMuted(true); return 0; }
    if (command == "unmute") { cb::setMuted(false); return 0; }
    if (command == "test") {
        const std::string cue = argc > 2 ? argv[2] : "done";
        cb::playCue(cue == "ask" ? cb::Cue::Ask : cb::Cue::Done);
        return 0;
    }
    return usage();
}
