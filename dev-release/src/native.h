#ifndef NATIVE_H
#define NATIVE_H

#include <algorithm>
#include <cerrno>
#include <ctime>
#include <set>
#include <thread>
#include <fcntl.h>
#include <sys/mount.h>
#include "context.h"
#include "resources.h"

// Native replacements for the penguins-eggs "oa" C engine (mount bootstrap, eternit umount) and the Go worker modules (shell, mksquashfs, xorriso). Everything that
// used to be forked as a bash script or a second binary runs in-process here.
namespace native {

// ---------------------------------------------------------------------------
// Shell step runner (worker/shell.go): script file + optional chroot
// ---------------------------------------------------------------------------
// bash where the distro has it, otherwise the system /bin/sh (BusyBox ash on Alpine)
inline std::string hostShell() { return commandExists("bash") ? "bash" : "/bin/sh"; }

inline int runShell(const std::string& script, const std::string& chrootPath, const std::vector<std::string>& args = {}) {
    std::string header = "set -e\nexport PATH=\"/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:$PATH\"\n\n";
    std::string content = header + script;

    // Host scripts live in our staging dir; chroot scripts go on the liveroot tmpfs (/tmp is
    // never cloned), so nothing is left behind in /root or anywhere in the clone.
    std::string hostDir = chrootPath.empty() ? STAGING_DIR + "/run" : chrootPath + "/tmp";
    mkdirP(hostDir, chrootPath.empty() ? 0700 : 01777);

    std::string tmpl = hostDir + "/cdb-exec-XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    int fd = mkstemp(buf.data());
    if (fd < 0) { logError("cannot create script in " + hostDir); return -1; }
    close(fd);
    std::string path = buf.data();
    writeFile(path, content, 0755);

    std::string argStr;
    for (const auto& a : args) argStr += " " + shq(a);

    std::string cmd;
    if (!chrootPath.empty()) {
        std::string sh = pathExists(chrootPath + "/bin/bash") ? "/bin/bash" : "/bin/sh";
        cmd = "chroot " + shq(chrootPath) + " " + sh + " /tmp/" + baseName(path) + argStr;
    } else {
        cmd = hostShell() + " " + shq(path) + argStr;
    }
    int rc = runCommand(cmd);
    unlink(path.c_str());
    return rc;
}

// ---------------------------------------------------------------------------
// Mount helpers
// ---------------------------------------------------------------------------
inline std::string decodeMountPath(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 3 < s.size() + 0 && isdigit((unsigned char)s[i + 1])) {
            out += (char)strtol(s.substr(i + 1, 3).c_str(), nullptr, 8);
            i += 3;
        } else out += s[i];
    }
    return out;
}

inline std::vector<std::string> mountPoints() {
    std::vector<std::string> out;
    for (const auto& line : splitLines(readFile("/proc/mounts"))) {
        std::stringstream ss(line);
        std::string dev, path;
        if (ss >> dev >> path) out.push_back(decodeMountPath(path));
    }
    return out;
}

inline bool isMounted(const std::string& path) {
    std::string clean = path;
    while (clean.size() > 1 && clean.back() == '/') clean.pop_back();
    for (const auto& m : mountPoints()) if (m == clean) return true;
    return false;
}

inline bool doMount(const char* src, const std::string& dst, const char* type, unsigned long flags, const char* data = nullptr) {
    if (mount(src, dst.c_str(), type, flags, data) != 0) {
        logError("mount " + dst + " failed: " + strerror(errno));
        return false;
    }
    return true;
}

inline bool bindOnce(const std::string& src, const std::string& dst) {
    mkdirP(dst);
    if (isMounted(dst)) return true;
    return doMount(src.c_str(), dst, nullptr, MS_BIND);
}

// ---------------------------------------------------------------------------
// mount_logic: native port of bootstrap-liveroot.sh
// ---------------------------------------------------------------------------
inline bool bootstrapLiveroot(const Context& c) {
    const std::string& base = c.workDir;
    const std::string& live = c.liveRoot;
    const std::string overlay = base + "/.overlay";

    mkdirP(live);
    mkdirP(overlay + "/upperdir");
    mkdirP(overlay + "/workdir");
    mkdirP(overlay + "/lowerdir");

    // Self-bind so package managers see "/" as a mountpoint inside the chroot; private so
    // it never propagates and breaks the later umount.
    if (!isMounted(live) && !doMount(live.c_str(), live, nullptr, MS_BIND)) return false;
    if (!doMount(nullptr, live, nullptr, MS_PRIVATE)) return false;

    // Physical copies
    if (runCommand("cp -a /etc /boot " + shq(live + "/")) != 0) return false;
    for (const char* link : {"vmlinuz", "initrd.img", "vmlinuz.old", "initrd.img.old"}) {
        std::string src = std::string("/") + link;
        if (pathExists(src)) runCommand("cp -a " + shq(src) + " " + shq(live + "/" + link));
    }

    // Bind mounts and usr-merge symlinks
    for (const char* e : {"bin", "sbin", "lib", "lib64", "opt", "root", "srv"}) {
        std::string src = std::string("/") + e;
        std::string dst = live + "/" + e;
        struct stat st;
        if (lstat(src.c_str(), &st) != 0) continue;
        if (S_ISLNK(st.st_mode)) {
            char target[4096];
            ssize_t n = readlink(src.c_str(), target, sizeof(target) - 1);
            if (n > 0) {
                target[n] = '\0';
                unlink(dst.c_str());
                if (symlink(target, dst.c_str()) != 0) logWarning("symlink " + dst + ": " + strerror(errno));
            }
        } else if (S_ISDIR(st.st_mode)) {
            if (!bindOnce(src, dst)) return false;
        }
    }

    // Bind the real /home read-only so users and their files are cloned as they are
    // (slave so the bind never propagates back to the host)
    {
        std::string home = live + "/home";
        mkdirP(home);
        if (!isMounted(home)) {
            if (!doMount("/home", home, nullptr, MS_BIND)) return false;
            if (!doMount(nullptr, home, nullptr, MS_SLAVE)) return false;
            if (!doMount(nullptr, home, nullptr, MS_REMOUNT | MS_BIND | MS_RDONLY)) return false;
        }
    }

    // Overlays for usr and var
    for (const char* ovl : {"usr", "var"}) {
        std::string lower  = overlay + "/lowerdir/" + ovl;
        std::string upper  = overlay + "/upperdir/" + ovl;
        std::string work   = overlay + "/workdir/" + ovl;
        std::string merged = live + "/" + ovl;
        mkdirP(lower); mkdirP(upper); mkdirP(work); mkdirP(merged);
        if (!isMounted(lower) && !doMount((std::string("/") + ovl).c_str(), lower, nullptr, MS_BIND)) return false;
        if (!isMounted(merged)) {
            std::string opts = "lowerdir=" + lower + ",upperdir=" + upper + ",workdir=" + work;
            if (!doMount("overlay", merged, "overlay", 0, opts.c_str())) return false;
        }
    }

    // API filesystems
    for (const char* d : {"proc", "sys", "dev", "run"}) mkdirP(live + "/" + d);
    if (!isMounted(live + "/proc") && !doMount("proc", live + "/proc", "proc", 0)) return false;
    if (!isMounted(live + "/sys") && !doMount("sys", live + "/sys", "sysfs", 0)) return false;
    if (!isMounted(live + "/dev") && !doMount("/dev", live + "/dev", nullptr, MS_BIND)) return false;
    if (!isMounted(live + "/run") && !doMount("/run", live + "/run", nullptr, MS_BIND)) return false;

    // tmpfs
    mkdirP(live + "/tmp");
    chmod((live + "/tmp").c_str(), 01777);
    if (!c.isGitHubAction && !isMounted(live + "/tmp"))
        if (!doMount("tmpfs", live + "/tmp", "tmpfs", 0, "mode=1777")) return false;

    logSuccess("liveroot bootstrap completed.");
    return true;
}

// ---------------------------------------------------------------------------
// umount (eternit): deepest mounts first, short EBUSY retry, lazy detach as last resort
// ---------------------------------------------------------------------------
inline bool umountPath(const std::string& path) {
    if (umount(path.c_str()) == 0) return true;
    if (errno != EBUSY) {
        logWarning("umount('" + path + "') failed: " + strerror(errno));
        return false;
    }
    for (int i = 0; i < 10; i++) {
        usleep(100000);
        if (umount(path.c_str()) == 0) return true;
        if (errno != EBUSY) { logWarning("umount('" + path + "') failed: " + strerror(errno)); return false; }
    }
    logWarning("'" + path + "' still busy after retries, forcing lazy detach");
    if (umount2(path.c_str(), MNT_DETACH) != 0) {
        logError("lazy detach of '" + path + "' failed: " + strerror(errno));
        return false;
    }
    return true;
}

inline bool umountTree(const std::string& workDir) {
    std::vector<std::string> targets;
    for (const auto& m : mountPoints())
        if (m == workDir || startsWith(m, workDir + "/")) targets.push_back(m);

    std::sort(targets.begin(), targets.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    logNormal("Decontamination: found " + std::to_string(targets.size()) + " mount points in " + workDir);

    bool ok = true;
    for (const auto& t : targets) if (!umountPath(t)) ok = false;
    sync();
    if (!ok) logError("Decontamination incomplete for: " + workDir);
    return ok;
}

// ---------------------------------------------------------------------------
// Exclude list for mksquashfs (planner/create-exclude-list.go)
// ---------------------------------------------------------------------------
inline std::vector<std::string> swapFilesFromFstab() {
    std::vector<std::string> out;
    for (const auto& l : splitLines(readFile("/etc/fstab"))) {
        std::string t = trim(l);
        if (t.empty() || t[0] == '#') continue;
        std::stringstream ss(t);
        std::string dev, mp, type;
        if (!(ss >> dev >> mp >> type)) continue;
        if (type == "swap" && dev[0] == '/' && !startsWith(dev, "/dev/")) out.push_back(dev.substr(1));
    }
    return out;
}

inline std::string generateExcludeList(const Context& c) {
    // Only what cannot be cloned: virtual/runtime filesystems and mount points, our own work
    // directory, and the host-specific files that stop a live boot (fstab, crypttab, swap).
    // Everything else, including users, passwords, /root and /home, is cloned untouched.
    std::vector<std::string> ex = {"dev/*", "mnt", "media", "proc/*", "sys/*", "run/*", "tmp/*", "lost+found"};

    std::string rel = c.workDir.substr(c.workDir[0] == '/' ? 1 : 0);
    ex.push_back(rel + "/.overlay/*");
    ex.push_back(rel + "/.overlay/.??*");
    ex.push_back(rel + "/isodir/*");
    ex.push_back(rel + "/*.iso");
    // The squashfs source is "/" bind-mounted at the clone directory, so the work directory's own
    // liveroot copy and the (empty) bind point show up inside it
    ex.push_back(c.liveRoot.substr(1));
    ex.push_back(c.cloneDir.substr(1));

    for (const char* e : {
        "boot/cdb-initrd.img",
        "etc/fstab", "etc/crypttab", "etc/mtab",
        "etc/udev/rules.d/70-persistent-cd.rules", "etc/udev/rules.d/70-persistent-net.rules",
        "swapfile"
    }) ex.push_back(e);

    for (const auto& s : swapFilesFromFstab()) ex.push_back(s);

    if (c.isGitHubAction) { ex.push_back("usr"); ex.push_back("var"); ex.push_back("opt"); }
    for (const auto& l : splitLines(readFile(CONFIG_DIR + "/custom.exclude.list"))) {
        std::string t = trim(l);
        if (t.empty() || t[0] == '#') continue;
        ex.push_back(t[0] == '/' ? t.substr(1) : t);
    }

    mkdirP(STAGING_DIR);
    std::string out;
    for (const auto& e : ex) out += e + "\n";
    std::string path = STAGING_DIR + "/excludes.list";
    writeFile(path, out);
    return path;
}

// ---------------------------------------------------------------------------
// mksquashfs (worker/mksquashfs.go + squashfs-args.go)
// ---------------------------------------------------------------------------
inline std::string squashfsCompressionArgs(const std::string& algo, const std::string& level, const std::string& arch) {
    if (algo == "zstd") return "-comp zstd -Xcompression-level " + level;
    if (algo == "xz") {
        std::string a = "-comp xz";
        if (arch == "x86_64" || arch == "i686" || arch == "i386") a += " -Xbcj x86";
        return a + " -Xdict-size 1M";
    }
    if (algo == "gzip") return "-comp gzip";
    return "-comp " + algo;
}

// Squashfs source for every family: the whole system bind-mounted at the user's clone directory
// (cmiadvanced mountSystemToCloneDir: mount --bind / <cloneDir>)
inline std::string cloneDir(const Context& c) { return c.cloneDir; }

// mount --bind / <dir>, kept private (never propagates back) and read-only (mksquashfs only reads)
inline bool bindRoot(const std::string& dir) {
    mkdirP(dir);
    if (isMounted(dir)) return true;
    logNormal("Mounting system to: " + dir);
    if (!doMount("/", dir, nullptr, MS_BIND)) return false;
    if (!doMount(nullptr, dir, nullptr, MS_PRIVATE) ||
        !doMount(nullptr, dir, nullptr, MS_REMOUNT | MS_BIND | MS_RDONLY)) {
        umountPath(dir);
        return false;
    }
    logSuccess("System mounted successfully to: " + dir);
    return true;
}

inline bool unbindRoot(const std::string& dir) {
    if (dir.empty() || !isMounted(dir)) return true;
    return umountPath(dir);
}

// ---------------------------------------------------------------------------
// mkfs.erofs (Arch, image format erofs): KDE Linux build.sh rootfs command, fed from the bound
// clone directory. The mksquashfs exclude list is turned into erofs exclude rules.
// ---------------------------------------------------------------------------
inline std::string globToRegex(const std::string& g) {
    std::string r = "^";
    for (char ch : g) {
        switch (ch) {
            case '*': r += "[^/]*"; break;
            case '?': r += "[^/]"; break;
            case '.': case '^': case '$': case '+': case '(': case ')':
            case '{': case '}': case '|': case '\\': r += '\\'; r += ch; break;
            default: r += ch;
        }
    }
    return r + "$";
}

// Rules match paths relative to the source tree without a leading slash (e.g. "etc/fstab")
inline std::string erofsExcludeArgs(const Context& c) {
    std::string out;
    for (const auto& l : splitLines(readFile(c.excludeList))) {
        std::string e = trim(l);
        if (e.empty()) continue;
        if (e == "mnt" || e == "media")   // keep the empty mount points (mksquashfs recreates them with -p)
            out += " --exclude-regex=" + shq("^" + e + "/.+");
        else if (e.find_first_of("*?[") != std::string::npos)
            out += " --exclude-regex=" + shq(globToRegex(e));
        else
            out += " --exclude-path=" + shq(e);
    }
    return out;
}

inline bool runMkerofs(const Context& c, const std::string& dest) {
    if (!commandExists("mkfs.erofs")) { logError("mkfs.erofs is not installed (use Setup > Install Needed Tools)"); return false; }

    mkdirP(dirName(dest));
    unlink(dest.c_str());
    const std::string src = cloneDir(c);
    if (!bindRoot(src)) return false;

    std::string cmd = "mkfs.erofs -zzstd -C 65536 --chunksize 65536 -T" + std::to_string((long long)time(nullptr)) +
        " --ignore-mtime" + erofsExcludeArgs(c) + " " + shq(dest) + " " + shq(src);

    logNormal("Running: " + cmd);
    bool ok = runCommand(cmd) == 0;
    logNormal("Unmounting bind mount...");
    if (!unbindRoot(src)) ok = false;
    return ok;
}

// mksquashfs is sometimes built with only some compressors (Gentoo: gzip unless USE=zstd etc.)
inline bool squashfsHasCompressor(const std::string& algo) {
    std::string help = captureCommand("mksquashfs -help 2>&1");
    if (!contains(help, "ompressors available")) return true;   // no list in this version: don't guess
    for (const auto& l : splitLines(help)) {
        std::string t = trim(l);
        if (t == algo || startsWith(t, algo + " ")) return true;
    }
    return false;
}

inline bool runMksquashfs(const Context& c, const std::string& dest) {
    if (c.distro.family == "archlinux" && c.settings.imageFormat == "erofs") return runMkerofs(c, dest);

    std::string algo = c.settings.compressionAlgo.empty() ? "zstd" : c.settings.compressionAlgo;
    if (algo != "gzip" && !squashfsHasCompressor(algo)) {
        logWarning("This mksquashfs was built without " + algo + " - using gzip instead" +
                   (c.distro.family == "gentoo" ? " (Setup > Install Needed Tools rebuilds it with zstd/xz)." : "."));
        algo = "gzip";
    }
    int lvl = c.settings.compressionLevel > 0 ? c.settings.compressionLevel : 3;
    unsigned procs = std::thread::hardware_concurrency();
    if (procs == 0) procs = 1;

    mkdirP(dirName(dest));
    const std::string src = cloneDir(c);
    if (!bindRoot(src)) return false;

    std::string cmd;
    if (c.distro.family == "archlinux") {
        // Original cmiadvanced createSquashFS / createSquashFS_xz flags (256K blocks), plus the
        // exclude list so the work directory and host-only files stay out
        std::string comp = algo == "xz" ? "-comp xz -b 256K -Xbcj x86"
                                        : "-comp " + algo + (algo == "zstd" ? " -Xcompression-level " + std::to_string(lvl) : "") + " -b 256K";
        cmd = "mksquashfs " + shq(src) + " " + shq(dest) + " -noappend " + comp +
            " -wildcards -p 'mnt d 0755 root root' -p 'media d 0755 root root' -ef " + shq(c.excludeList);
    } else {
        cmd = "mksquashfs " + shq(src) + " " + shq(dest) +
            " -b 1M -processors " + std::to_string(procs) + " -noappend -progress -wildcards" +
            " -p 'mnt d 0755 root root' -p 'media d 0755 root root'" +
            " -ef " + shq(c.excludeList) + " " + squashfsCompressionArgs(algo, std::to_string(lvl), c.distro.arch);
    }

    logNormal("Running: " + cmd);
    bool ok = runCommand(cmd) == 0;
    logNormal("Unmounting bind mount...");
    if (!unbindRoot(src)) ok = false;
    return ok;
}

// ---------------------------------------------------------------------------
// .disk metadata + xorriso (planner/create-dot-disk-script.go, worker/xorriso.go)
// ---------------------------------------------------------------------------
inline void writeDotDisk(const Context& c) {
    std::string uuid = captureCommand("cat /proc/sys/kernel/random/uuid");
    time_t now = time(nullptr);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
    std::string iso = baseName(c.isoPath);

    std::string dd = c.isoDir + "/.disk";
    mkdirP(dd + "/id");
    writeFile(dd + "/info", iso + "\n");
    writeFile(dd + "/id/" + uuid, "");
    writeFile(dd + "/README.md",
        "# claudebackup linux " + APP_VERSION + "\n\nVolinfo: " + iso + "\nImage created at: " + ts + "\n");
}

// Arch: the original cmiadvanced createISO() xorriso command, unchanged except that it reads the
// ISO tree from isodir (BUILD_DIR there) and runs without sudo (we are already root)
inline bool runXorrisoArch(const Context& c) {
    const std::string BUILD_DIR = c.isoDir;
    if (!isRegularFile(BUILD_DIR + "/boot/grub/i386-pc/boot_hybrid.img") || !isRegularFile(BUILD_DIR + "/boot/efi.img")) {
        logError("xorriso: GRUB boot images missing from " + BUILD_DIR);
        return false;
    }

    std::string cmd = "xorriso -as mkisofs "
    "--modification-date=\"$(date +%Y%m%d%H%M%S00)\" "
    "--protective-msdos-label "
    "-volid \"" + c.isoTag + "\" "
    "-appid \"claudemods Linux Live/Rescue CD\" "
    "-publisher \"claudemods claudemods101@gmail.com >\" "
    "-preparer \"Prepared by user\" "
    "-r -graft-points -no-pad "
    "--sort-weight 0 / "
    "--sort-weight 1 /boot "
    "--grub2-mbr " + BUILD_DIR + "/boot/grub/i386-pc/boot_hybrid.img "
    "-partition_offset 16 "
    "-b boot/grub/i386-pc/eltorito.img "
    "-c boot.catalog "
    "-no-emul-boot -boot-load-size 4 -boot-info-table --grub2-boot-info "
    "-eltorito-alt-boot "
    "-append_partition 2 0xef " + BUILD_DIR + "/boot/efi.img "
    "-e --interval:appended_partition_2:all:: "
    "-no-emul-boot "
    "-iso-level 3 "
    "-o \"" + c.isoPath + "\" " +
    BUILD_DIR;

    logNormal("Generating ISO (Hybrid BIOS/UEFI GRUB, claudemods tree): " + c.isoPath);
    return runCommand(cmd) == 0;
}

inline bool runXorriso(const Context& c) {
    if (!isDirectory(c.isoDir)) { logError("xorriso: source directory '" + c.isoDir + "' does not exist"); return false; }
    if (c.distro.family == "archlinux") return runXorrisoArch(c);

    const std::string isolinuxBin = "isolinux/isolinux.bin";
    const std::string isolinuxCat = "isolinux/boot.cat";
    const std::string efiImg = "EFI/BOOT/efi.img";
    const std::string isohdpfx = STAGING_DIR + "/bootloaders/ISOLINUX/isohdpfx.bin";

    bool hasIsolinux = isRegularFile(c.isoDir + "/" + isolinuxBin) && isRegularFile(isohdpfx);
    bool hasEfi = isRegularFile(c.isoDir + "/" + efiImg);
    if (!hasIsolinux && !hasEfi) logWarning("neither ISOLINUX nor EFI boot loader was found. The ISO may not boot.");

    std::string cmd = "xorriso -as mkisofs -r -l -iso-level 3 -J -joliet-long -volid " + shq(LIVE_LABEL);
    if (hasIsolinux) {
        cmd += " -eltorito-boot " + isolinuxBin + " -eltorito-catalog " + isolinuxCat +
               " -no-emul-boot -boot-load-size 4 -boot-info-table -isohybrid-mbr " + shq(isohdpfx);
        if (hasEfi) cmd += " -eltorito-alt-boot -e " + efiImg + " -no-emul-boot -isohybrid-gpt-basdat";
    } else if (hasEfi) {
        cmd += " -e " + efiImg + " -no-emul-boot -isohybrid-gpt-basdat";
    }
    cmd += " -o " + shq(c.isoPath) + " " + shq(c.isoDir);

    logNormal(std::string("Generating ISO (") + (hasIsolinux && hasEfi ? "Hybrid BIOS/UEFI" : hasEfi ? "UEFI-only" : "ISO9660") + "): " + c.isoPath);
    return runCommand(cmd) == 0;
}

// ---------------------------------------------------------------------------
// Bootloader assets (utils/ensure-bootloaders.go)
// ---------------------------------------------------------------------------
inline bool ensureBootloaders() {
    const std::string dir = STAGING_DIR + "/bootloaders";
    if (pathExists(dir + "/.download-complete")) return true;

    logNormal("Downloading bootloaders...");
    runCommand("rm -rf " + shq(dir));
    mkdirP(dir);
    std::string cmd = "curl -fsSL " + shq(BOOTLOADER_URL) + " | tar -xz --strip-components=1 -C " + shq(dir);
    if (runCommand(hostShell() + " -c " + shq("set -o pipefail; " + cmd)) != 0) {
        runCommand("rm -rf " + shq(dir));
        return false;
    }
    return writeFile(dir + "/.download-complete", "ok\n");
}

// Arch: unpacks the embedded build-image-arch-img.zip (ISO tree + hooks) to STAGING_DIR/arch.
// Extracted fresh every time so a rebuilt binary never uses stale files.
inline bool ensureArchFiles() {
    const std::string dir = STAGING_DIR + "/arch";
    if (!commandExists("unzip")) { logError("unzip is not installed (use Setup > Install Needed Tools)"); return false; }
    logNormal("Extracting the embedded Arch boot files...");
    runCommand("rm -rf " + shq(dir));
    if (!resources::extract(resources::Zip::BuildImageArch, dir)) {
        runCommand("rm -rf " + shq(dir));
        return false;
    }
    return true;
}

}  // namespace native

#endif
