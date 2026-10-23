#include "notify.hpp"

#include "sound.hpp"
#include "state.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace cb {
namespace {

std::string readStdin() {
    if (isatty(STDIN_FILENO)) return {};
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    return ss.str();
}

// Pulls one top-level string field out of the hook payload. The payloads are
// small and flat, so this beats pulling in a JSON dependency.
std::string jsonString(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t at = json.find(needle);
    if (at == std::string::npos) return {};
    at = json.find(':', at + needle.size());
    if (at == std::string::npos) return {};
    at = json.find('"', at);
    if (at == std::string::npos) return {};
    ++at;

    std::string out;
    while (at < json.size()) {
        const char c = json[at];
        if (c == '\\' && at + 1 < json.size()) {
            const char esc = json[at + 1];
            switch (esc) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                default: out += esc; break;
            }
            at += 2;
            continue;
        }
        if (c == '"') break;
        out += c;
        ++at;
    }
    return out;
}

std::string lowered(std::string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

int runNotify(const std::string& event) {
    const std::string payload = readStdin();

    Session session;
    session.id = jsonString(payload, "session_id");
    if (session.id.empty()) session.id = "manual";
    session.cwd = jsonString(payload, "cwd");
    session.pid = findClaudePid(getppid());
    session.ts = nowSeconds();

    if (event == "session-end") {
        removeSession(session.id);
        return 0;
    }

    Session prior;
    const bool hadPrior = readSession(sessionPath(session.id), prior);
    if (session.cwd.empty() && hadPrior) session.cwd = prior.cwd;

    if (event == "session-start") {
        session.status = Status::Idle;
    } else if (event == "prompt" || event == "tool") {
        session.status = Status::Busy;
    } else if (event == "notification") {
        // Claude Code also fires Notification when it has been idle for a while
        // waiting on input. That is not "the agent needs you", so ignore it
        // unless the session was actually working.
        const std::string message = lowered(jsonString(payload, "message"));
        const bool idleNudge = message.find("waiting for your input") != std::string::npos;
        const bool working = hadPrior ? prior.status != Status::Idle : !idleNudge;
        if (!working) return 0;
        if (hadPrior && prior.status == Status::Waiting) return 0;  // already flagged
        session.status = Status::Waiting;
        writeSession(session);
        playCue(Cue::Ask);
        return 0;
    } else if (event == "stop") {
        session.status = Status::Idle;
        const bool wasWorking = !hadPrior || prior.status != Status::Idle;
        writeSession(session);
        if (wasWorking) playCue(Cue::Done);
        return 0;
    } else {
        std::cerr << "claude-beep: unknown event '" << event << "'\n";
        return 0;
    }

    writeSession(session);
    return 0;
}

} // namespace cb
