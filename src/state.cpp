#include "state.hpp"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <signal.h>
#include <sstream>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace cb {
namespace {

std::string envOr(const char* name, const std::string& fallback) {
    const char* v = getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

void mkdirp(const std::string& path) {
    std::string acc;
    std::istringstream in(path);
    std::string part;
    while (std::getline(in, part, '/')) {
        if (part.empty()) continue;
        acc += "/" + part;
        mkdir(acc.c_str(), 0700);
    }
}

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Strips a trailing newline and anything that would corrupt the key=value file.
std::string sanitize(const std::string& v) {
    std::string out;
    out.reserve(v.size());
    for (char c : v) {
        if (c == '\n' || c == '\r') continue;
        out += c;
    }
    return out;
}

// /proc/<pid>/cmdline is a NUL-separated argv.
std::vector<std::string> readArgv(long pid) {
    const std::string raw = readFile("/proc/" + std::to_string(pid) + "/cmdline");
    std::vector<std::string> argv;
    std::string current;
    for (char c : raw) {
        if (c == '\0') {
            if (!current.empty()) argv.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) argv.push_back(current);
    return argv;
}

// True for the Claude Code CLI itself, and deliberately false for the shell
// that runs our own `claude-beep ...` hook command: that shell's argv contains
// the string "claude" too, and it exits the moment the hook returns.
bool looksLikeClaude(const std::vector<std::string>& argv) {
    for (const std::string& arg : argv) {
        if (arg.find("claude-beep") != std::string::npos) continue;
        const auto slash = arg.find_last_of('/');
        const std::string base = slash == std::string::npos ? arg : arg.substr(slash + 1);
        if (base == "claude") return true;
        if (arg.find("claude-code") != std::string::npos) return true;  // npm install layout
    }
    return false;
}

bool processAlive(long pid) {
    if (pid <= 0) return true;  // no pid recorded: keep the entry
    if (kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) return false;
    // Guard against the pid being reused by an unrelated program.
    return looksLikeClaude(readArgv(pid));
}

} // namespace

const char* statusName(Status s) {
    switch (s) {
        case Status::Busy: return "busy";
        case Status::Waiting: return "waiting";
        default: return "idle";
    }
}

Status statusFromName(const std::string& name) {
    if (name == "busy") return Status::Busy;
    if (name == "waiting") return Status::Waiting;
    return Status::Idle;
}

long long nowSeconds() { return static_cast<long long>(time(nullptr)); }

std::string stateDir() {
    std::string base = envOr("XDG_RUNTIME_DIR", "/tmp/claude-beep-" + std::to_string(getuid()));
    std::string dir = base + "/claude-beep";
    mkdirp(dir);
    return dir;
}

std::string configDir() {
    std::string base = envOr("XDG_CONFIG_HOME", envOr("HOME", ".") + "/.config");
    std::string dir = base + "/claude-beep";
    mkdirp(dir);
    return dir;
}

std::string sessionPath(const std::string& id) {
    std::string safe;
    for (char c : id) safe += (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    if (safe.empty()) safe = "unknown";
    return stateDir() + "/" + safe + ".session";
}

bool writeSession(const Session& s) {
    const std::string path = sessionPath(s.id);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << "id=" << sanitize(s.id) << "\n"
            << "status=" << statusName(s.status) << "\n"
            << "pid=" << s.pid << "\n"
            << "cwd=" << sanitize(s.cwd) << "\n"
            << "ts=" << s.ts << "\n";
    }
    return rename(tmp.c_str(), path.c_str()) == 0;
}

bool readSession(const std::string& path, Session& out) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    bool any = false;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "id") out.id = value;
        else if (key == "status") out.status = statusFromName(value);
        else if (key == "pid") out.pid = strtol(value.c_str(), nullptr, 10);
        else if (key == "cwd") out.cwd = value;
        else if (key == "ts") out.ts = strtoll(value.c_str(), nullptr, 10);
        any = true;
    }
    return any;
}

void removeSession(const std::string& id) { unlink(sessionPath(id).c_str()); }

std::vector<Session> loadSessions(bool prune) {
    std::vector<Session> sessions;
    const std::string dir = stateDir();
    DIR* d = opendir(dir.c_str());
    if (!d) return sessions;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name.size() < 9 || name.compare(name.size() - 8, 8, ".session") != 0) continue;
        Session s;
        const std::string path = dir + "/" + name;
        if (!readSession(path, s)) continue;
        if (prune && !processAlive(s.pid)) {
            unlink(path.c_str());
            continue;
        }
        sessions.push_back(s);
    }
    closedir(d);
    return sessions;
}

Status aggregate(const std::vector<Session>& sessions) {
    bool busy = false;
    for (const Session& s : sessions) {
        if (s.status == Status::Waiting) return Status::Waiting;
        if (s.status == Status::Busy) busy = true;
    }
    return busy ? Status::Busy : Status::Idle;
}

bool muted() {
    struct stat st;
    return stat((configDir() + "/muted").c_str(), &st) == 0;
}

void setMuted(bool value) {
    const std::string path = configDir() + "/muted";
    if (value) {
        std::ofstream out(path, std::ios::trunc);
        out << "1\n";
    } else {
        unlink(path.c_str());
    }
}

long findClaudePid(long start) {
    long pid = start;
    for (int hop = 0; hop < 12 && pid > 1; ++hop) {
        if (looksLikeClaude(readArgv(pid))) return pid;

        // Read PPid out of /proc/<pid>/status to climb one level.
        const std::string status = readFile("/proc/" + std::to_string(pid) + "/status");
        auto at = status.find("PPid:");
        if (at == std::string::npos) break;
        pid = strtol(status.c_str() + at + 5, nullptr, 10);
    }
    return 0;  // unknown owner: never prune on its behalf
}

} // namespace cb
