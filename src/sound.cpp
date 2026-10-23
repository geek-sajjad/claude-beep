#include "sound.hpp"

#include "state.hpp"

#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace cb {
namespace {

const char* kDefaultDone = "/usr/share/sounds/freedesktop/stereo/complete.oga";
const char* kDefaultAsk  = "/usr/share/sounds/freedesktop/stereo/message.oga";

std::string trim(const std::string& s) {
    auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// config file is `key = value` lines; unknown keys are ignored.
std::string configValue(const std::string& key) {
    std::ifstream in(configDir() + "/config");
    if (!in) return {};
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (trim(line.substr(0, eq)) == key) return trim(line.substr(eq + 1));
    }
    return {};
}

bool exists(const std::string& path) {
    struct stat st;
    return !path.empty() && stat(path.c_str(), &st) == 0;
}

std::string resolvePlayer() {
    const std::string configured = configValue("player");
    if (!configured.empty()) return configured;
    for (const char* candidate : {"/usr/bin/pw-play", "/usr/bin/paplay", "/usr/bin/canberra-gtk-play"}) {
        if (exists(candidate)) return candidate;
    }
    return {};
}

} // namespace

std::string soundFor(Cue cue) {
    const std::string configured = configValue(cue == Cue::Done ? "sound_done" : "sound_ask");
    if (!configured.empty()) return configured;
    return cue == Cue::Done ? kDefaultDone : kDefaultAsk;
}

void playCue(Cue cue) {
    if (muted()) return;

    const std::string player = resolvePlayer();
    const std::string sound = soundFor(cue);
    if (player.empty() || !exists(sound)) return;

    const pid_t pid = fork();
    if (pid != 0) return;  // parent (and fork failure): carry on immediately

    // Detach so the player outlives this short-lived hook process.
    setsid();
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO) close(devnull);
    }

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(player.c_str()));
    // canberra-gtk-play needs -f to take a file path; the others take it bare.
    const bool canberra = player.find("canberra") != std::string::npos;
    if (canberra) argv.push_back(const_cast<char*>("-f"));
    argv.push_back(const_cast<char*>(sound.c_str()));
    argv.push_back(nullptr);

    execv(player.c_str(), argv.data());
    _exit(127);
}

} // namespace cb
