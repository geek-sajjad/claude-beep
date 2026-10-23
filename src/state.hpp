#pragma once

#include <string>
#include <vector>

namespace cb {

// What a single Claude session is doing right now.
enum class Status { Idle, Busy, Waiting };

const char* statusName(Status s);
Status statusFromName(const std::string& name);

struct Session {
    std::string id;       // Claude session id (uuid), or "manual"
    Status status = Status::Idle;
    long pid = 0;         // pid of the owning `claude` process, for stale pruning
    std::string cwd;      // project directory, shown in the menu
    long long ts = 0;     // unix seconds of the last update
};

// Runtime state lives under $XDG_RUNTIME_DIR so it is wiped on logout.
std::string stateDir();
std::string configDir();
std::string sessionPath(const std::string& id);

bool writeSession(const Session& s);
bool readSession(const std::string& path, Session& out);
void removeSession(const std::string& id);

// Loads every session file; with prune=true, files whose owning process is
// gone are deleted instead of returned.
std::vector<Session> loadSessions(bool prune);

// Waiting beats Busy beats Idle: anything asking for input wins the panel.
Status aggregate(const std::vector<Session>& sessions);

bool muted();
void setMuted(bool value);

// Walks up the process tree from `start` looking for the `claude` process that
// spawned this hook. Falls back to `start` when nothing matches.
long findClaudePid(long start);

long long nowSeconds();

} // namespace cb
