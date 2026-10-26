#ifndef UPDATE_H
#define UPDATE_H

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include "common.h"

// Update check and self-update from GitHub (REPO_URL). The running version is APP_VERSION
// (dev-release/version.txt at build time); the latest is the same file on the main branch.
namespace update {

// "v1.2.3" -> {1, 2, 3}
inline std::vector<int> versionParts(std::string v) {
    size_t start = v.find_first_of("0123456789");
    std::vector<int> parts;
    if (start == std::string::npos) return parts;
    std::stringstream ss(v.substr(start));
    std::string p;
    while (std::getline(ss, p, '.')) parts.push_back(atoi(p.c_str()));
    return parts;
}

// True when `latest` is a higher version than `current`
inline bool isNewer(const std::string& latest, const std::string& current) {
    std::vector<int> a = versionParts(latest), b = versionParts(current);
    size_t n = std::max(a.size(), b.size());
    a.resize(n, 0);
    b.resize(n, 0);
    return a > b;
}

// curl, or wget (BusyBox on Alpine); empty on any error
inline std::string fetchText(const std::string& url) {
    return captureCommand("(curl -fsSL --max-time 8 " + shq(url) + " || wget -q -T 8 -O- " + shq(url) + ") 2>/dev/null");
}

// The version on GitHub, empty if it could not be read
inline std::string latestVersion() {
    std::string v = trim(fetchText(REMOTE_VERSION_URL));
    if (v.empty() || v.size() > 20 || versionParts(v).empty()) return "";
    return v;
}

// Background check at start, so the menu never waits for the network
enum class State { Checking, UpToDate, Available, Offline };
inline std::atomic<State> g_state(State::Checking);
inline std::mutex g_mutex;
inline std::string g_latest;

inline void checkInBackground() {
    std::thread([] {
        std::string v = latestVersion();
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_latest = v;
        }
        g_state = v.empty() ? State::Offline : isNewer(v, APP_VERSION) ? State::Available : State::UpToDate;
    }).detach();
}

inline std::string latestKnown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_latest;
}

// Shown after the version in the banner
inline std::string statusText() {
    switch (g_state.load()) {
        case State::Checking:  return COLOR_CYAN + "  (checking for updates...)" + COLOR_RESET;
        case State::UpToDate:  return COLOR_GREEN + "  (up to date)" + COLOR_RESET;
        case State::Available: return COLOR_YELLOW + "  -> " + latestKnown() + " update available (main menu > Update)" + COLOR_RESET;
        case State::Offline:   return COLOR_RED + "  (update check failed - offline?)" + COLOR_RESET;
    }
    return "";
}

// Downloads the main branch source, builds and installs it. True when /usr/bin has the new build.
inline bool downloadBuildInstall() {
    const std::string dir = STAGING_DIR + "/update";
    runCommand("rm -rf " + shq(dir));
    mkdirP(dir);

    logNormal("Downloading " + SOURCE_TARBALL_URL);
    std::string get = "(curl -fsSL " + shq(SOURCE_TARBALL_URL) + " || wget -q -O- " + shq(SOURCE_TARBALL_URL) + ")";
    if (runCommand("sh -c " + shq("set -e; " + get + " | tar -xz -C " + shq(dir))) != 0) {
        logError("Download failed - check the internet connection.");
        return false;
    }
    const std::string src = dir + "/" + SOURCE_TARBALL_DIR + "/dev-release";
    if (!isRegularFile(src + "/Makefile")) { logError("The download has no dev-release/Makefile."); return false; }

    logNormal("Building the new version");
    if (runCommand("make -C " + shq(src) + " -j\"$(nproc 2>/dev/null || echo 1)\"") != 0) {
        logError("Building the update failed (new build tools needed? run the new dev-release/build.sh).");
        return false;
    }
    logNormal("Installing the new version");
    if (runCommand("make -C " + shq(src) + " install") != 0) { logError("Installing the update failed."); return false; }
    runCommand("rm -rf " + shq(dir));
    return true;
}

}  // namespace update

#endif
