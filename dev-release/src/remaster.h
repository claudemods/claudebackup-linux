#ifndef REMASTER_H
#define REMASTER_H

#include <csignal>
#include <chrono>
#include "plan.h"

// Options chosen in the setup menu
struct RemasterOptions {
    std::string workDir;              // empty = default
    std::string outputDir;            // empty = work dir
    std::string isoName;              // empty = auto-generated
    std::string algo;                 // zstd | xz | gzip | lz4
    std::string stopAfter;
    std::string isoTag;               // Arch volume label, empty = 2026
    std::string cloneDir;             // bind point for "/", empty = workDir/clone_system_temp
    std::string imageFormat;          // Arch: squashfs | erofs, empty = squashfs
    int level = 0;
    int ramMode = -1;                 // -1 = default, 0/1 = override
};

namespace remaster {

inline std::string g_workDir;
inline std::string g_cloneDir;

inline void onSignal(int sig) {
    // Never leave bind mounts of / behind
    native::unbindRoot(g_cloneDir);
    native::umountTree(g_workDir);
    _exit(128 + sig);
}

inline Context makeContext(const RemasterOptions& o) {
    Context c;
    c.distro = detectDistro();
    c.settings = Settings();
    if (!o.algo.empty()) c.settings.compressionAlgo = o.algo;
    if (o.level > 0) c.settings.compressionLevel = o.level;
    if (o.ramMode >= 0) c.settings.ramMode = (o.ramMode == 1);
    if (o.imageFormat == "erofs" && c.distro.family == "archlinux") c.settings.imageFormat = "erofs";
    c.ramModeEnabled = c.settings.ramMode;
    c.isGitHubAction = isDirectory("/home/runner/work");
    c.stopAfter = o.stopAfter;
    if (!o.isoTag.empty()) c.isoTag = o.isoTag;

    c.workDir = !o.workDir.empty() ? o.workDir : c.settings.workDir;
    while (c.workDir.size() > 1 && c.workDir.back() == '/') c.workDir.pop_back();

    c.cloneDir = !o.cloneDir.empty() ? o.cloneDir : c.workDir + "/clone_system_temp";
    while (c.cloneDir.size() > 1 && c.cloneDir.back() == '/') c.cloneDir.pop_back();

    std::string snapshotDir = !o.outputDir.empty() ? o.outputDir : c.workDir;
    while (snapshotDir.size() > 1 && snapshotDir.back() == '/') snapshotDir.pop_back();

    std::string name = o.isoName;
    if (name.empty()) name = makeIsoName(c.distro, "");
    else if (!endsWith(name, ".iso")) name += ".iso";

    c.isoDir = snapshotDir + "/isodir";
    c.liveRoot = c.workDir + "/liveroot";
    c.isoPath = snapshotDir + "/" + name;
    if (snapshotDir != c.workDir) c.symlinkIsoPath = c.workDir + "/" + baseName(c.isoPath);
    return c;
}

// Host tools the pipeline shells out to (Arch unzips its embedded boot files instead of
// downloading bootloaders with curl). Build steps run as bash scripts, which a minimal
// Alpine does not have.
inline std::vector<std::string> missingTools(const Distro& d) {
    std::vector<std::string> missing;
    for (const char* t : {"bash", "mksquashfs", "xorriso", "mcopy", "mkfs.vfat", "tar"})
        if (!commandExists(t)) missing.push_back(t);
    const char* extra = d.family == "archlinux" ? "unzip" : "curl";
    if (!commandExists(extra)) missing.push_back(extra);
    return missing;
}

inline std::string installToolsCommand(const Distro& d) {
    if (d.family == "debian")   return "apt-get install -y squashfs-tools xorriso mtools dosfstools curl live-boot live-boot-initramfs-tools";
    if (d.family == "archlinux") return "pacman -S --needed --noconfirm squashfs-tools erofs-utils libisoburn mtools dosfstools unzip mkinitcpio device-mapper gnupg";
    if (d.family == "fedora")   return "dnf install -y squashfs-tools xorriso mtools dosfstools curl dracut-live";
    if (d.family == "opensuse") return "zypper install -y squashfs xorriso mtools dosfstools curl";
    if (d.family == "alpine")   return "apk add bash squashfs-tools xorriso mtools dosfstools curl";
    if (d.family == "gentoo")   return "emerge --noreplace --ask=n sys-fs/squashfs-tools dev-libs/libisoburn sys-fs/mtools sys-fs/dosfstools net-misc/curl sys-kernel/dracut";
    return "";
}

// Boot files for the build: the embedded claudemods zip on Arch, downloaded bootloaders otherwise
inline bool bootFilesReady(const Distro& d) {
    return d.family == "archlinux" || pathExists(STAGING_DIR + "/bootloaders/.download-complete");
}

inline bool prepareBootFiles(const Distro& d) {
    if (d.family == "archlinux") return native::ensureArchFiles();
    return native::ensureBootloaders();
}

// Where each family's live initramfs leaves the squashfs during the live session (Calamares unpackfs)
inline std::string calamaresUnpackSource(const Distro& d) {
    if (d.family == "debian") return "/run/live/medium/live/filesystem.squashfs";                // live-boot
    if (d.family == "fedora" || d.family == "opensuse" || d.family == "gentoo")
        return "/run/initramfs/live/LiveOS/squashfs.img";                                       // dracut dmsquash-live
    if (d.family == "alpine") return "/lib/live/mount/medium/live/filesystem.squashfs";         // CDB-SIDECAR
    return "";
}

inline std::string unzipInstallCommand(const Distro& d) {
    if (d.family == "debian")   return "apt-get install -y unzip";
    if (d.family == "fedora")   return "dnf install -y unzip";
    if (d.family == "opensuse") return "zypper --non-interactive install unzip";
    if (d.family == "alpine")   return "apk add unzip";
    if (d.family == "gentoo")   return "emerge --noreplace --ask=n app-arch/unzip";
    return "pacman -S --needed --noconfirm unzip";
}

// Installs Calamares on the host so the next ISO contains it.
//   Arch: the embedded calamares-files.zip + claudemods.zip (cmiadvanced extrainstalls.sh)
//   others: the distro's calamares package + claudemods branding and a full-clone config
inline bool installCalamares(const Distro& d, bool btrfs, bool erofs) {
    if (!d.supported()) { logError("Calamares setup needs a supported distro."); return false; }
    if (!commandExists("unzip")) {
        logNormal("Installing unzip...");
        runCommand(unzipInstallCommand(d));
        if (!commandExists("unzip")) { logError("unzip is not installed."); return false; }
    }
    if (!commandExists("bash")) { logError("bash is not installed (use Setup > Install Needed Tools)"); return false; }

    const std::string dir = STAGING_DIR + "/calamares";
    runCommand("rm -rf " + shq(dir));
    bool ok = resources::extract(resources::Zip::CalamaresFiles, dir) &&
              resources::extract(resources::Zip::Claudemods, dir + "/calamares-files");
    if (ok && d.family == "archlinux")
        ok = native::runShell(scripts::render(scripts::CALAMARES_SETUP), "", {dir, btrfs ? "1" : "0", erofs ? "1" : "0"}) == 0;
    else if (ok)
        ok = native::runShell(scripts::render(scripts::CALAMARES_SETUP_GENERIC), "",
                              {d.family, dir, btrfs ? "1" : "0", calamaresUnpackSource(d), toLower(d.distroID)}) == 0;
    runCommand("rm -rf " + shq(dir));
    return ok;
}

// Runs the whole plan. Returns true when the ISO was built (or the breakpoint was reached).
inline bool run(const RemasterOptions& opts) {
    if (geteuid() != 0) { logError("Creating an ISO requires root."); return false; }

    Context c = makeContext(opts);
    g_workDir = c.workDir;
    g_cloneDir = c.cloneDir;
    if (c.cloneDir == "/" || startsWith(c.workDir + "/", c.cloneDir + "/")) {
        logError("Clone directory '" + c.cloneDir + "' cannot be / or contain the work directory.");
        return false;
    }

    logNormal("Distro: " + c.distro.prettyName + " (family: " + c.distro.family + ")");
    if (!c.distro.supported()) { logError("Distro '" + c.distro.distroID + "' is not mapped to a supported family."); return false; }

    auto missing = missingTools(c.distro);
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

    if (!prepareBootFiles(c.distro)) { logError("Failed to prepare the boot files."); return false; }
    logNormal("Generating exclude list...");
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

    if (!native::unbindRoot(c.cloneDir)) ok = false;
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
