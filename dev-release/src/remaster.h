#ifndef REMASTER_H
#define REMASTER_H

#include <csignal>
#include <chrono>
#include "plan.h"

// Options chosen in the setup menu
struct RemasterOptions {
    std::string mode = "standard";    // standard | clone
    std::string workDir;              // empty = default
    std::string outputDir;            // empty = work dir
    std::string isoName;              // empty = auto-generated
    std::string algo;                 // zstd | xz | gzip | lz4
    std::string user;
    std::string password;
    std::string stopAfter;
    int level = 0;
    int ramMode = -1;                 // -1 = default, 0/1 = override
};

namespace remaster {

inline std::string g_workDir;

inline void onSignal(int sig) {
    // Never leave bind mounts of / behind
    native::umountTree(g_workDir);
    _exit(128 + sig);
}

inline Context makeContext(const RemasterOptions& o) {
    Context c;
    c.distro = detectDistro();
    c.settings = Settings();
    c.mode = o.mode;
    if (!o.algo.empty()) c.settings.compressionAlgo = o.algo;
    if (o.level > 0) c.settings.compressionLevel = o.level;
    if (!o.user.empty()) c.settings.user = o.user;
    if (!o.password.empty()) c.settings.password = o.password;
    if (o.ramMode >= 0) c.settings.ramMode = (o.ramMode == 1);
    c.ramModeEnabled = c.settings.ramMode;
    c.hasCalamares = commandExists("calamares");
    c.isGitHubAction = isDirectory("/home/runner/work");
    c.stopAfter = o.stopAfter;

    c.workDir = !o.workDir.empty() ? o.workDir : c.settings.workDir;
    while (c.workDir.size() > 1 && c.workDir.back() == '/') c.workDir.pop_back();

    std::string snapshotDir = !o.outputDir.empty() ? o.outputDir : c.workDir;
    while (snapshotDir.size() > 1 && snapshotDir.back() == '/') snapshotDir.pop_back();

    std::string name = o.isoName;
    if (name.empty()) name = makeIsoName(c.distro, o.mode, "");
    else if (!endsWith(name, ".iso")) name += ".iso";

    c.isoDir = snapshotDir + "/isodir";
    c.liveRoot = c.workDir + "/liveroot";
    c.isoPath = snapshotDir + "/" + name;
    if (snapshotDir != c.workDir) c.symlinkIsoPath = c.workDir + "/" + baseName(c.isoPath);
    return c;
}

// Host tools the pipeline shells out to
inline std::vector<std::string> missingTools() {
    std::vector<std::string> missing;
    for (const char* t : {"mksquashfs", "xorriso", "mcopy", "mkfs.vfat", "curl", "tar"})
        if (!commandExists(t)) missing.push_back(t);
    return missing;
}

inline std::string installToolsCommand(const Distro& d) {
    if (d.family == "debian")   return "apt-get install -y squashfs-tools xorriso mtools dosfstools curl";
    if (d.family == "archlinux" || d.family == "manjaro") return "pacman -S --needed --noconfirm squashfs-tools libisoburn mtools dosfstools curl";
    if (d.family == "fedora")   return "dnf install -y squashfs-tools xorriso mtools dosfstools curl";
    if (d.family == "opensuse") return "zypper install -y squashfs xorriso mtools dosfstools curl";
    if (d.family == "alpine")   return "apk add squashfs-tools xorriso mtools dosfstools curl";
    return "";
}

// Runs the whole plan. Returns true when the ISO was built (or the breakpoint was reached).
inline bool run(const RemasterOptions& opts) {
    if (geteuid() != 0) { logError("Creating an ISO requires root."); return false; }

    Context c = makeContext(opts);
    g_workDir = c.workDir;

    logNormal("Distro: " + c.distro.prettyName + " (family: " + c.distro.family + ", mode: " + c.mode + ")");
    if (!c.distro.supported()) { logError("Distro '" + c.distro.distroID + "' is not mapped to a supported family."); return false; }

    auto missing = missingTools();
    if (!missing.empty()) {
        std::string list;
        for (const auto& m : missing) list += m + " ";
        logError("Missing tools: " + list + "(use Setup > Install Needed Tools)");
        return false;
    }

    if (c.distro.family == "fedora" && captureCommand("getenforce 2>/dev/null") == "Enforcing")
        logWarning("SELinux is Enforcing; set it to permissive (setenforce 0) to avoid unexplained blocks.");

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    mkdirP(STAGING_DIR);
    mkdirP(c.workDir);
    mkdirP(c.isoDir);

    if (!native::ensureBootloaders()) { logError("Failed to download bootloaders."); return false; }
    logNormal("Generating exclude list (" + c.mode + " mode)...");
    c.excludeList = native::generateExcludeList(c);

    auto steps = plan::build(c);
    logNormal("ISO will be generated at: " + c.isoPath);
    auto start = std::chrono::steady_clock::now();

    bool ok = true;
    for (const auto& s : steps) {
        logNormal("[" + s.name + "] " + s.description);
        if (!s.run()) {
            logError("Step '" + s.name + "' failed. Aborting.");
            ok = false;
            break;
        }
        if (!c.stopAfter.empty() && s.name == c.stopAfter) {
            logWarning("Breakpoint '" + s.name + "' reached.");
            break;
        }
    }

    if (!native::umountTree(c.workDir)) ok = false;
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    if (!ok) return false;

    if (c.stopAfter.empty()) {
        long secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
        char t[32];
        snprintf(t, sizeof(t), "%02ld:%02ld:%02ld", secs / 3600, (secs / 60) % 60, secs % 60);
        struct stat st;
        double mib = stat(c.isoPath.c_str(), &st) == 0 ? st.st_size / 1048576.0 : 0;
        char sz[64];
        snprintf(sz, sizeof(sz), "%.1f MiB", mib);
        logSuccess(std::string("ISO: ") + sz + " in " + t + " - " + c.isoPath);
    }
    return true;
}

}  // namespace remaster

#endif
