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
    std::string imageFormat;          // squashfs | erofs, empty = squashfs
    std::string customDir;            // edited copies from Setup > Edit Configs, empty = none
    std::string isoTreeDir;           // where the ISO structure is built, empty = <output dir>/isodir
    bool cleanCache = true;           // leave package caches out of the image
    int level = 0;
    int ramMode = -1;                 // -1 = default, 0/1 = override
};

namespace remaster {

inline std::string g_workDir;
inline std::string g_cloneDir;

inline void onSignal(int sig) {
    if (write(STDOUT_FILENO, "\033[0m", 4) < 0) {}   // the green command output must not stick
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
    if (o.imageFormat == "erofs") c.settings.imageFormat = "erofs";
    c.customDir = o.customDir;
    c.cleanCache = o.cleanCache;
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
    if (name.empty()) name = makeIsoName(c.distro);
    else if (!endsWith(name, ".iso")) name += ".iso";

    // The ISO structure lives in the user's config folder like in the claudemods multi iso creator;
    // the output directory only receives the finished ISO
    c.isoDir = !o.isoTreeDir.empty() ? o.isoTreeDir : snapshotDir + "/isodir";
    c.liveRoot = c.workDir + "/liveroot";
    c.isoPath = snapshotDir + "/" + name;
    if (snapshotDir != c.workDir) c.symlinkIsoPath = c.workDir + "/" + baseName(c.isoPath);
    return c;
}

// Host tools the pipeline shells out to (Arch unzips its embedded boot files instead of
// downloading bootloaders with curl)
inline std::vector<std::string> missingTools(const Distro& d) {
    std::vector<std::string> missing;
    for (const char* t : {"mksquashfs", "xorriso", "mcopy", "mkfs.vfat", "tar"})
        if (!commandExists(t)) missing.push_back(t);
    const char* extra = d.family == "archlinux" ? "unzip" : "curl";
    if (!commandExists(extra)) missing.push_back(extra);
    return missing;
}

// Everything the app needs is installed by dev-release/build.sh (tools, Calamares, Alpine
// installer tools); the app itself installs nothing
const std::string INSTALL_HINT = "run dev-release/build.sh";

// erofs-utils (mkfs.erofs) per family, for messages
inline std::string erofsPackage(const Distro& d) {
    return d.family == "gentoo" ? "sys-fs/erofs-utils" : "erofs-utils";
}

// Squashfs compression per distro: Gentoo's squashfs-tools is built with gzip by default, every
// other distro uses zstd (its level is set in Setup)
inline std::string compressionFor(const Distro& d) { return d.family == "gentoo" ? "gzip" : "zstd"; }

// Boot files for the build: the embedded claudemods zip on Arch, downloaded bootloaders otherwise
inline bool bootFilesReady(const Distro& d) {
    return d.family == "archlinux" || pathExists(STAGING_DIR + "/bootloaders/.download-complete");
}

inline bool prepareBootFiles(const Distro& d) {
    if (d.family == "archlinux") return native::ensureArchFiles();
    return native::ensureBootloaders();
}

// Arch: where mkinitcpio looks for the claudemods 11-dm-initramfs.rules
inline const std::string MKINITCPIO_RULES = "/usr/lib/initcpio/udev/11-dm-initramfs.rules";

inline bool mkinitcpioConfigReady() { return isRegularFile(MKINITCPIO_RULES); }

// Setup > mkinitcpio Config: makes sure the rules file is in place, copying it from the embedded
// claudemods zip when it is not
inline bool ensureMkinitcpioConfig() {
    logNormal("Checking: " + MKINITCPIO_RULES);
    if (mkinitcpioConfigReady()) { logSuccess("TRUE - file already exists!"); return true; }
    logWarning("FALSE - file does not exist, copying it...");
    if (!native::ensureArchFiles()) return false;
    const std::string src = families::archZipDir() + "/build-image-arch-img/11-dm-initramfs.rules";
    if (!isRegularFile(src)) { logError("11-dm-initramfs.rules is missing from the embedded Arch files."); return false; }
    mkdirP(dirName(MKINITCPIO_RULES));
    if (!writeFile(MKINITCPIO_RULES, readFile(src))) { logError("cannot write " + MKINITCPIO_RULES); return false; }
    logSuccess("mkinitcpio config copied to: " + MKINITCPIO_RULES);
    return true;
}

// Calamares does not run on Alpine, so it is never offered or installed there
inline bool calamaresAvailable(const Distro& d) { return d.supported() && d.family != "alpine"; }

// Alpine instead gets our own installer that puts the cloned system back on a disk. Its tools
// (installed by build.sh) must be on the host before building, so they are in the clone.
inline bool alpineInstallerReady() {
    for (const char* t : {"sfdisk", "mkfs.vfat", "grub-install", "grub-mkconfig", "mkinitfs"})
        if (!commandExists(t)) return false;
    return true;
}

// Run from the live ISO: installs the cloned system onto disk (fs = ext4 | btrfs)
inline bool installClonedSystemAlpine(const std::string& disk, const std::string& fs, bool compress) {
    if (geteuid() != 0) { logError("Installing needs root."); return false; }
    return native::runShell(scripts::render(scripts::ALPINE_INSTALLER), "", {disk, fs, compress ? "1" : "0"}) == 0;
}

// ---------------------------------------------------------------------------
// Setup > Edit Configs: the files the user can change before building. Each is kept as an own copy
// in the custom folder (seeded from what the build would generate) and used by the next build.
// What applies depends on the distro: Arch has kernels.cfg + mkinitcpio, the others isolinux.cfg
// + their initramfs tool (initramfs-tools modules, dracut); Alpine has no extra initramfs file.
// ---------------------------------------------------------------------------
struct CustomFile { std::string name, label; };

inline std::vector<CustomFile> customFilesFor(const Distro& d) {
    std::vector<CustomFile> v = {{"grub.cfg", "Edit GRUB Config"}};
    if (d.family == "archlinux") {
        v.push_back({"kernels.cfg", "Edit Boot Text (kernels.cfg)"});
        v.push_back({"mkinitcpio.conf", "Edit mkinitcpio.conf"});
    } else {
        v.push_back({"isolinux.cfg", "Edit Boot Text (isolinux.cfg)"});
        if (d.family == "debian") v.push_back({"initramfs-modules", "Edit Initramfs Modules"});
        if (d.family == "fedora" || d.family == "opensuse" || d.family == "gentoo") v.push_back({"dracut.conf", "Edit Dracut Config"});
    }
    return v;
}

// Creates the user's copy of a file from what the build would produce. False if it cannot be made.
inline bool seedCustomFile(const Context& c, const std::string& name, const std::string& path) {
    const std::string archTree = families::archZipDir() + "/build-image-arch-img";
    std::string content;

    if (name == "kernels.cfg") {
        content = families::archKernelsCfg(c);
    } else if (name == "grub.cfg" && families::isArch(c)) {
        if (!native::ensureArchFiles()) return false;
        content = readFile(archTree + "/boot/grub/grub.cfg");
    } else if (name == "grub.cfg" || name == "isolinux.cfg") {
        // Generate the menus into a scratch tree exactly as the build does
        const std::string tmp = STAGING_DIR + "/seed";
        runCommand("rm -rf " + shq(tmp));
        for (const char* d : {"/boot/grub", "/isolinux", "/EFI/BOOT"}) mkdirP(tmp + d);
        bool ok = native::runShell(plan::bootAssetsCmd(tmp), "") == 0 && plan::generateMenus(c, tmp);
        if (ok) content = readFile(tmp + (name == "grub.cfg" ? "/boot/grub/grub.cfg" : "/isolinux/isolinux.cfg"));
        runCommand("rm -rf " + shq(tmp));
    } else if (name == "mkinitcpio.conf") {
        if (!native::ensureArchFiles()) return false;
        content = readFile(archTree + "/mkinitcpio.conf");
    } else if (name == "dracut.conf") {
        content = "# Used as dracut's main config (dracut --conf) for the live initramfs.\n" + readFile("/etc/dracut.conf");
    } else if (name == "initramfs-modules") {
        content = "# Kernel modules to add to the live initramfs, one per line (like /etc/initramfs-tools/modules).\n";
    }
    if (content.empty()) { logError("Could not create " + name + " to edit."); return false; }

    mkdirP(dirName(path));
    return writeFile(path, content);
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
        logError("Missing tools: " + list + "(" + INSTALL_HINT + ")");
        return false;
    }

    if (c.settings.imageFormat == "erofs") {
        if (!commandExists("mkfs.erofs")) { logError("mkfs.erofs is missing (" + INSTALL_HINT + ", or install " + erofsPackage(c.distro) + ")"); return false; }
        if (!native::kernelHasErofs()) {
            logError("The running kernel has no erofs support (CONFIG_EROFS_FS), so the live ISO could not mount the image. Set Setup > Image Format to squashfs.");
            return false;
        }
        logNormal("Image: erofs (" + native::erofsCompressionName() + ")");
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
