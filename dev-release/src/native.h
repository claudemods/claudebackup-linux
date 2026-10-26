#ifndef NATIVE_H
#define NATIVE_H

#include <algorithm>
#include <cerrno>
#include <ctime>
#include <set>
#include <thread>
#include <chrono>
#include <atomic>
#include <iomanip>
#include <fcntl.h>
#include <sys/ioctl.h>
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

// Downloaded packages and temp files that only make the image bigger: {folder, exclude rule relative
// to the root of the clone}. Left out of the image, the running system's own copies are not touched.
struct CacheDir { std::string dir, rule; };

inline std::vector<CacheDir> packageCaches(const Distro& d) {
    std::vector<CacheDir> v;
    if (d.family == "archlinux")      v.push_back({"/var/cache/pacman/pkg", "var/cache/pacman/pkg/*"});
    else if (d.family == "debian")    v.push_back({"/var/cache/apt/archives", "var/cache/apt/archives/*.deb"});
    else if (d.family == "fedora") {
        v.push_back({"/var/cache/dnf", "var/cache/dnf/*"});
        v.push_back({"/var/cache/libdnf5", "var/cache/libdnf5/*"});
    }
    else if (d.family == "opensuse")  v.push_back({"/var/cache/zypp/packages", "var/cache/zypp/packages/*"});
    else if (d.family == "alpine")    v.push_back({"/var/cache/apk", "var/cache/apk/*"});
    else if (d.family == "gentoo") {
        v.push_back({"/var/cache/distfiles", "var/cache/distfiles/*"});
        v.push_back({"/var/cache/binpkgs", "var/cache/binpkgs/*"});
    }
    v.push_back({"/var/tmp", "var/tmp/*"});
    return v;
}

// Says what is being left out of the image, with the size it saves (the step before mksquashfs / mkfs.erofs)
inline bool reportPackageCaches(const Context& c) {
    logNormal("Cleaning the package cache and temp files out of the image (your running system's own files are not touched):");
    bool any = false;
    for (const auto& p : packageCaches(c.distro)) {
        if (!isDirectory(p.dir)) continue;
        std::string size = captureCommand("du -sh " + shq(p.dir) + " 2>/dev/null | cut -f1");
        if (size.empty() || size == "0" || size == "4.0K") continue;
        logNormal("  " + p.dir + " (" + size + ")");
        any = true;
    }
    if (!any) logNormal("  nothing to clean.");
    return true;
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
    // the ISO structure (in the user's config folder) must not end up inside the image it contains
    if (startsWith(c.isoDir, "/") && c.isoDir.size() > 1) ex.push_back(c.isoDir.substr(1));

    for (const char* e : {
        "boot/cdb-initrd.img",
        "etc/fstab", "etc/crypttab", "etc/mtab",
        "etc/udev/rules.d/70-persistent-cd.rules", "etc/udev/rules.d/70-persistent-net.rules",
        "swapfile"
    }) ex.push_back(e);

    for (const auto& s : swapFilesFromFstab()) ex.push_back(s);

    if (c.cleanCache) for (const auto& p : packageCaches(c.distro)) ex.push_back(p.rule);

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

// Unmounts the bind of "/" and everything bound inside it, deepest first
inline bool unbindRoot(const std::string& dir) {
    if (dir.empty() || !isMounted(dir)) return true;
    std::vector<std::string> targets;
    for (const auto& m : mountPoints())
        if (m == dir || startsWith(m, dir + "/")) targets.push_back(m);
    std::sort(targets.begin(), targets.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    bool ok = true;
    for (const auto& t : targets) if (!umountPath(t)) ok = false;
    return ok;
}

// A bind of "/" only shows the root filesystem. Systems that keep /home, /var, /boot ... on their own
// partition or btrfs subvolume (openSUSE, Fedora, a btrfs Arch...) would lose them, so those mounts
// are listed here to be bound into the clone as well. Snapshots, removable media and the app's own
// work tree are left out.
inline std::vector<std::string> dataMounts(const std::vector<std::string>& skip) {
    static const std::set<std::string> diskFs = {"btrfs", "ext2", "ext3", "ext4", "xfs", "f2fs"};
    static const char* const never[] = {"/dev", "/proc", "/sys", "/run", "/tmp", "/mnt", "/media", "/snap"};

    std::vector<std::string> out;
    for (const auto& line : splitLines(readFile("/proc/mounts"))) {
        std::stringstream ss(line);
        std::string dev, path, type;
        if (!(ss >> dev >> path >> type)) continue;
        path = decodeMountPath(path);
        if (path == "/" || !diskFs.count(type) || contains(path, "/.snapshots")) continue;

        bool skipIt = false;
        for (const char* n : never) if (path == n || startsWith(path, std::string(n) + "/")) skipIt = true;
        for (const auto& s : skip) if (!s.empty() && (path == s || startsWith(path, s + "/"))) skipIt = true;
        if (!skipIt && std::find(out.begin(), out.end(), path) == out.end()) out.push_back(path);
    }
    // parents first, so /boot is bound before /boot/grub2/...
    std::stable_sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return a.size() < b.size(); });
    return out;
}

// mount --bind / <dir>, then the separate data mounts (/home on its own subvolume, ...) inside it.
// All kept private (never propagate back) and read-only (mksquashfs / mkfs.erofs only read).
// skip: paths whose mounts must not be included (the work directory with its liveroot binds).
inline bool bindRoot(const std::string& dir, const std::vector<std::string>& skip = {}) {
    mkdirP(dir);
    if (isMounted(dir)) return true;
    logNormal("Mounting system to: " + dir);
    if (!doMount("/", dir, nullptr, MS_BIND)) return false;
    if (!doMount(nullptr, dir, nullptr, MS_PRIVATE) ||
        !doMount(nullptr, dir, nullptr, MS_REMOUNT | MS_BIND | MS_RDONLY)) {
        umountPath(dir);
        return false;
    }

    std::vector<std::string> skipAll = skip;
    skipAll.push_back(dir);
    for (const auto& m : dataMounts(skipAll)) {
        const std::string dst = dir + m;
        if (!isDirectory(dst)) { logWarning(m + " is a separate filesystem but has no mount point in the root filesystem - not included."); continue; }
        if (!doMount(m.c_str(), dst, nullptr, MS_BIND) ||
            !doMount(nullptr, dst, nullptr, MS_PRIVATE) ||
            !doMount(nullptr, dst, nullptr, MS_REMOUNT | MS_BIND | MS_RDONLY)) {
            logError("Could not include " + m + " in the clone.");
            unbindRoot(dir);
            return false;
        }
        logNormal("Including " + m + " (separate filesystem / subvolume)");
    }
    logSuccess("System mounted successfully to: " + dir);
    return true;
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

// ---------------------------------------------------------------------------
// Green progress bar for mkfs.erofs (as in the claudemods multi iso creator): mkfs.erofs runs
// in the background writing to a log; the bar shows a spinner, elapsed time and image size.
// The percentage is estimated from time (to 60%, then held) and jumps to 100% when it finishes.
// ---------------------------------------------------------------------------
inline std::string humanSize(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return "0 B";
    double s = st.st_size;
    std::ostringstream o;
    o << std::fixed << std::setprecision(2);
    if (s >= 1073741824.0) o << s / 1073741824.0 << " GB";
    else if (s >= 1048576.0) o << s / 1048576.0 << " MB";
    else if (s >= 1024.0) o << s / 1024.0 << " KB";
    else o << std::setprecision(0) << s << " B";
    return o.str();
}

inline void drawErofsBar(int percentage, int spinnerPos, int spinnerChar, int secs, const std::string& size) {
    struct winsize w;
    int width = (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) ? w.ws_col : 80;
    char timer[16];
    snprintf(timer, sizeof(timer), "%02d:%02d", secs / 60, secs % 60);
    std::string extra = std::string(" [") + timer + "] [" + size + "]";
    int barWidth = width - 9 - static_cast<int>(extra.size());
    if (barWidth < 10) barWidth = 10;
    if (percentage < 1) percentage = 1;
    int filled = std::max(1, percentage * barWidth / 100);
    const char spinner[] = {'|', '/', '-', '\\'};

    std::string bar;
    for (int i = 0; i < filled; i++) bar += (i == spinnerPos % filled && percentage < 100) ? spinner[spinnerChar % 4] : '=';
    bar += std::string(barWidth - filled, ' ');
    char pct[8];
    snprintf(pct, sizeof(pct), "%3d%%", percentage);
    std::cout << "\r" << COLOR_GREEN << "[" << bar << "] " << pct << extra << COLOR_RESET << std::flush;
}

inline int runWithErofsBar(const std::string& cmd, const std::string& output, const std::string& logFile) {
    std::atomic<bool> done(false);
    int rc = 0;
    std::thread worker([&] { rc = runCommand(cmd + " > " + shq(logFile) + " 2>&1"); done = true; });

    std::cout << "\033[?25l";   // hide the cursor while drawing
    auto start = std::chrono::steady_clock::now();
    int spinnerPos = 0, spinnerChar = 0, frame = 0;
    while (!done) {
        int secs = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count());
        int pct = std::min(60, std::max(1, secs / 10));
        if (++frame >= 3) { frame = 0; spinnerPos++; spinnerChar++; }
        drawErofsBar(pct, spinnerPos, spinnerChar, secs, humanSize(output));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    worker.join();
    int secs = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count());
    drawErofsBar(rc == 0 ? 100 : std::min(60, std::max(1, secs / 10)), 0, 0, secs, humanSize(output));
    std::cout << "\033[?25h" << std::endl;
    if (rc != 0) runCommand("tail -n 20 " + shq(logFile));
    return rc;
}

// The live system boots the kernel it is built on, so erofs is only possible when that kernel has it
// (built in, a module, or already loaded)
inline bool kernelHasErofs() {
    return runPlain("grep -qw erofs /proc/filesystems 2>/dev/null || modinfo erofs >/dev/null 2>&1 || "
                    "grep -qs '/erofs.ko' \"/lib/modules/$(uname -r)/modules.builtin\"") == 0;
}

// zstd needs kernel 6.10+ (erofs zstd support); older kernels get lz4hc
inline bool kernelHasErofsZstd() {
    int major = 0, minor = 0;
    return sscanf(captureCommand("uname -r").c_str(), "%d.%d", &major, &minor) == 2 && (major > 6 || (major == 6 && minor >= 10));
}

inline std::string erofsCompressionArgs() {
    return kernelHasErofsZstd() ? "-zzstd -C 65536 --chunksize 65536" : "-zlz4hc -C 65536";
}

inline std::string erofsCompressionName() { return kernelHasErofsZstd() ? "zstd" : "lz4hc"; }

inline bool runMkerofs(const Context& c, const std::string& dest) {
    if (!commandExists("mkfs.erofs")) { logError("mkfs.erofs is not installed (use Setup > Install Needed Tools)"); return false; }
    if (!kernelHasErofs()) { logError("The running kernel has no erofs support (CONFIG_EROFS_FS), so a live ISO built from it could not boot an erofs image."); return false; }

    mkdirP(dirName(dest));
    unlink(dest.c_str());
    const std::string src = cloneDir(c);
    if (!bindRoot(src, {c.workDir})) return false;

    std::string cmd = "mkfs.erofs " + erofsCompressionArgs() + " -T" + std::to_string((long long)time(nullptr)) +
        " --ignore-mtime" + erofsExcludeArgs(c) + " " + shq(dest) + " " + shq(src);

    logNormal("Running: " + cmd);
    logNormal("Creating EROFS image... (mkfs.erofs output: " + STAGING_DIR + "/erofs.log)");
    bool ok = runWithErofsBar(cmd, dest, STAGING_DIR + "/erofs.log") == 0;
    if (ok) logSuccess("EROFS image created: " + dest + " (" + humanSize(dest) + ")");
    logNormal("Unmounting bind mount...");
    if (!unbindRoot(src)) ok = false;
    return ok;
}

// mksquashfs is sometimes built with only some compressors (Gentoo: gzip unless USE=zstd etc.).
// Its help output differs between versions, so test for real: squash a tiny folder with it.
inline bool squashfsHasCompressor(const std::string& algo) {
    const std::string dir = STAGING_DIR + "/comp-test";
    runCommand("rm -rf " + shq(dir));
    mkdirP(dir + "/in");
    writeFile(dir + "/in/test", "claudebackup\n");
    bool ok = runCommand("mksquashfs " + shq(dir + "/in") + " " + shq(dir + "/test.sqfs") +
                         " -noappend -no-progress -comp " + shq(algo) + " >/dev/null 2>&1") == 0;
    runCommand("rm -rf " + shq(dir));
    return ok;
}

inline bool runMksquashfs(const Context& c, const std::string& dest) {
    if (c.settings.imageFormat == "erofs") return runMkerofs(c, dest);

    std::string algo = c.settings.compressionAlgo.empty() ? "zstd" : c.settings.compressionAlgo;
    if (algo != "gzip" && !squashfsHasCompressor(algo)) {
        logWarning("This mksquashfs was built without " + algo + " - using gzip instead.");
        algo = "gzip";
    }
    int lvl = c.settings.compressionLevel > 0 ? c.settings.compressionLevel : 3;
    unsigned procs = std::thread::hardware_concurrency();
    if (procs == 0) procs = 1;

    mkdirP(dirName(dest));
    const std::string src = cloneDir(c);
    if (!bindRoot(src, {c.workDir})) return false;

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
    // mksquashfs draws its progress bar on our terminal: print it in green
    std::cout << COLOR_GREEN << std::flush;
    bool ok = runCommand(cmd) == 0;
    std::cout << COLOR_RESET << std::flush;
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
