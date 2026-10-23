#ifndef NATIVE_H
#define NATIVE_H

#include <algorithm>
#include <cerrno>
#include <ctime>
#include <set>
#include <thread>
#include <crypt.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <sys/mount.h>
#include "context.h"

// Native replacements for the penguins-eggs "oa" C engine (mount bootstrap, eternit umount,
// users) and the Go worker modules (shell, autologin, mksquashfs, xorriso). Everything that
// used to be forked as a bash script or a second binary runs in-process here.
namespace native {

// ---------------------------------------------------------------------------
// Shell step runner (worker/shell.go): script file + optional chroot
// ---------------------------------------------------------------------------
inline int runShell(const std::string& script, const std::string& chrootPath, const std::vector<std::string>& args = {}) {
    std::string header = "set -e\nexport PATH=\"/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:$PATH\"\n\n";
    std::string content = header + script;

    std::string hostDir = chrootPath.empty() ? "/root/.cdb" : chrootPath + "/root/.cdb";
    mkdirP(hostDir, 0700);

    std::string tmpl = hostDir + "/exec-XXXXXX";
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
        cmd = "chroot " + shq(chrootPath) + " " + sh + " /root/.cdb/" + baseName(path) + argStr;
    } else {
        cmd = "bash " + shq(path) + argStr;
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

    // Persistent marker of the remaster mode
    mkdirP(live + "/etc/claudebackup-linux");
    writeFile(live + "/etc/claudebackup-linux/mode", c.mode + "\n");

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

    // Clone: bind the real /home read-only (slave so it never propagates back to the host)
    if (c.mode == "clone") {
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
// users (oa native.c + oa-yocto.c)
// ---------------------------------------------------------------------------
const int UID_HUMAN_MIN = 1000;
const int UID_HUMAN_MAX = 59999;

inline std::string fieldOf(const std::string& line, int idx) {
    int cur = 0;
    size_t start = 0;
    for (size_t i = 0; i <= line.size(); i++) {
        if (i == line.size() || line[i] == ':') {
            if (cur == idx) return line.substr(start, i - start);
            cur++;
            start = i + 1;
        }
    }
    return "";
}

inline bool nameInFile(const std::string& file, const std::string& name) {
    for (const auto& l : splitLines(readFile(file))) if (fieldOf(l, 0) == name) return true;
    return false;
}

// Drop entries whose numeric id (field 2) is in the human range
inline void sanitizeIdFile(const std::string& path) {
    std::string out;
    for (const auto& l : splitLines(readFile(path))) {
        std::string id = fieldOf(l, 2);
        if (id.empty()) continue;
        int n = atoi(id.c_str());
        if (n < UID_HUMAN_MIN || n > UID_HUMAN_MAX) out += l + "\n";
    }
    writeFile(path, out);
}

// Keep only entries whose first field still exists in reference file
inline void sanitizeByName(const std::string& path, const std::string& reference, mode_t mode) {
    if (!pathExists(path)) return;
    std::string out;
    for (const auto& l : splitLines(readFile(path)))
        if (nameInFile(reference, fieldOf(l, 0))) out += l + "\n";
    writeFile(path, out, mode);
}

inline std::string makeSalt() {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";
    unsigned char rnd[16] = {0};
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) { if (read(fd, rnd, sizeof(rnd)) < 0) {} close(fd); }
    std::string salt = "$6$";
    for (int i = 0; i < 16; i++) salt += chars[rnd[i] % (sizeof(chars) - 1)];
    return salt + "$";
}

inline std::string hashPassword(const std::string& pw) {
    if (pw.empty()) return "$6$oa-tools$uTKAYeAVn.Y.Dy2To6HXsHt1Gt4HpMghmOV93a46jFY7hkAQ3tk7eRTKjcvSYDf5sOf3qnKzyyPYXurKp9ST3.";
    if (pw[0] == '$') return pw;
    const char* h = crypt(pw.c_str(), makeSalt().c_str());
    return h ? std::string(h) : hashPassword("");
}

// Groups of the invoking user (SUDO_USER) so the live user keeps hardware access
inline std::vector<std::string> mirrorGroups() {
    std::vector<std::string> groups;
    std::set<std::string> seen;
    auto add = [&](const std::string& g) { if (!g.empty() && seen.insert(g).second) groups.push_back(g); };

    const char* sudoUser = getenv("SUDO_USER");
    struct passwd* pw = (sudoUser && *sudoUser) ? getpwnam(sudoUser) : nullptr;
    if (pw) {
        int n = 64;
        std::vector<gid_t> gids(n);
        if (getgrouplist(pw->pw_name, pw->pw_gid, gids.data(), &n) == -1) { gids.resize(n); getgrouplist(pw->pw_name, pw->pw_gid, gids.data(), &n); }
        for (int i = 0; i < n; i++) {
            struct group* g = getgrgid(gids[i]);
            if (g && std::string(g->gr_name) != pw->pw_name && std::string(g->gr_name) != "users") add(g->gr_name);
        }
        add("wheel");
    } else {
        for (const char* g : {"wheel", "audio", "video", "storage", "network"}) add(g);
    }
    for (const char* g : {"netdev", "bluetooth", "plugdev", "lpadmin", "scanner", "cdrom", "dialout"}) add(g);
    return groups;
}

inline void addUserToGroups(const std::string& groupFile, const std::string& user, const std::vector<std::string>& groups) {
    std::string out;
    for (const auto& l : splitLines(readFile(groupFile))) {
        std::string name = fieldOf(l, 0);
        bool match = std::find(groups.begin(), groups.end(), name) != groups.end();
        if (match) out += l + ((!l.empty() && l.back() == ':') ? "" : ",") + user + "\n";
        else out += l + "\n";
    }
    writeFile(groupFile, out);
}

inline bool injectLiveUser(const Context& c) {
    const std::string& root = c.liveRoot;
    std::string login = c.liveUser();
    std::string home = "/home/" + login;
    std::string hashed = hashPassword(c.settings.password);
    int uid = UID_HUMAN_MIN, gid = UID_HUMAN_MIN;

    std::string passwd = root + "/etc/passwd", shadow = root + "/etc/shadow", group = root + "/etc/group";
    std::string gshadow = root + "/etc/gshadow", subuid = root + "/etc/subuid", subgid = root + "/etc/subgid";

    logNormal("Cleaning up host users (sanitize)...");
    sanitizeIdFile(passwd);
    sanitizeByName(shadow, passwd, 0640);
    sanitizeIdFile(group);
    sanitizeByName(gshadow, group, 0640);
    sanitizeByName(subuid, passwd, 0644);
    sanitizeByName(subgid, passwd, 0644);

    logNormal("Injecting live user '" + login + "' uid=" + std::to_string(uid));
    appendFile(passwd, login + ":x:" + std::to_string(uid) + ":" + std::to_string(gid) + ":" + login + ",,,:" + home + ":/bin/bash\n");
    appendFile(shadow, login + ":" + hashed + ":19750:0:99999:7:::\n");
    appendFile(group, login + ":x:" + std::to_string(gid) + ":\n");
    appendFile(gshadow, login + ":!::\n");
    appendFile(subuid, login + ":100000:65536\n");
    appendFile(subgid, login + ":100000:65536\n");
    addUserToGroups(group, login, mirrorGroups());

    mkdirP(root + "/etc/skel");
    mkdirP(root + home);
    runCommand("cp -a " + shq(root + "/etc/skel/.") + " " + shq(root + home + "/"));
    if (runCommand("chown -R " + std::to_string(uid) + ":" + std::to_string(gid) + " " + shq(root + home)) != 0) {
        logError("Home setup error for '" + login + "'");
        return false;
    }

    // If root already has a real password on this distro, match it to the live password.
    // Locked/disabled root (sudo-only distros) is deliberately left locked.
    std::string out;
    bool changed = false;
    for (const auto& l : splitLines(readFile(shadow))) {
        if (fieldOf(l, 0) == "root" && startsWith(fieldOf(l, 1), "$")) {
            size_t p1 = l.find(':');
            size_t p2 = l.find(':', p1 + 1);
            out += "root:" + hashed + (p2 == std::string::npos ? "" : l.substr(p2)) + "\n";
            changed = true;
        } else out += l + "\n";
    }
    if (changed) writeFile(shadow, out, 0640);
    return true;
}

// ---------------------------------------------------------------------------
// autologin-gui (worker/autologin-gui.go)
// ---------------------------------------------------------------------------
inline std::vector<std::string> listDesktopFiles(const std::string& dir) {
    std::vector<std::string> out;
    std::string raw = captureCommand("ls -1 " + shq(dir) + " 2>/dev/null");
    for (const auto& n : splitLines(raw)) if (endsWith(n, ".desktop")) out.push_back(n.substr(0, n.size() - 8));
    return out;
}

inline std::string findPreferredSession(const std::string& root) {
    auto files = listDesktopFiles(root + "/usr/share/xsessions");
    if (files.empty()) return "xfce";
    for (const char* pref : {"plasma", "lxqt", "lubuntu", "xfce"})
        for (const auto& f : files) if (contains(toLower(f), pref)) return f;
    return files[0];
}

// Insert AutomaticLogin keys right after [daemon] (creating it when missing)
inline void setDaemonAutologin(const std::string& path, const std::string& user) {
    std::vector<std::string> out;
    bool found = false;
    for (const auto& l : splitLines(readFile(path))) {
        if (contains(l, "AutomaticLoginEnable") || contains(l, "AutomaticLogin=")) continue;
        out.push_back(l);
        if (trim(l) == "[daemon]") {
            found = true;
            out.push_back("AutomaticLoginEnable=true");
            out.push_back("AutomaticLogin=" + user);
        }
    }
    if (!found) {
        out.insert(out.begin(), {"[daemon]", "AutomaticLoginEnable=true", "AutomaticLogin=" + user});
    }
    std::string text;
    for (const auto& l : out) text += l + "\n";
    writeFile(path, text);
}

inline void configureAutologin(const Context& c, const std::string& user) {
    const std::string& root = c.liveRoot;
    std::string session = findPreferredSession(root);
    logNormal("Autologin for '" + user + "', desktop session: " + session);

    // SDDM
    if (isDirectory(root + "/usr/share/sddm")) {
        mkdirP(root + "/etc/sddm.conf.d");
        writeFile(root + "/etc/sddm.conf.d/autologin.conf", "[Autologin]\nUser=" + user + "\nSession=" + session + "\nRelogin=false\n");
    }
    // LightDM
    if (isDirectory(root + "/etc/lightdm")) {
        std::string pam = root + "/etc/pam.d/lightdm-autologin";
        std::string data = readFile(pam);
        const std::string bypass = "auth\tsufficient\tpam_permit.so";
        if (!data.empty() && !contains(data, bypass)) writeFile(pam, bypass + "\n" + data);
        appendFile(root + "/etc/lightdm/lightdm.conf",
                   "\n[Seat:*]\nautologin-user=" + user + "\nautologin-user-timeout=0\nautologin-session=" + session + "\n");
    }
    // GDM / MDM
    for (const char* rel : {"etc/gdm3/daemon.conf", "etc/gdm3/custom.conf", "etc/gdm/custom.conf", "etc/mdm/mdm.conf"})
        if (isRegularFile(root + "/" + rel)) setDaemonAutologin(root + "/" + rel, user);

    // LXDM
    std::string lxdm = root + "/etc/lxdm/lxdm.conf";
    if (isRegularFile(lxdm)) {
        auto lines = splitLines(readFile(lxdm));
        bool found = false;
        for (auto& l : lines) {
            std::string t = trim(l);
            if (startsWith(t, "autologin=") || startsWith(t, "# autologin=") || startsWith(t, "#autologin=")) { l = "autologin=" + user; found = true; break; }
        }
        if (!found) {
            std::vector<std::string> n; bool ins = false;
            for (const auto& l : lines) { n.push_back(l); if (trim(l) == "[base]") { n.push_back("autologin=" + user); ins = true; } }
            if (!ins) n.insert(n.begin(), {"[base]", "autologin=" + user});
            lines = n;
        }
        std::string text; for (const auto& l : lines) text += l + "\n";
        writeFile(lxdm, text);
    }
    // SLiM
    std::string slim = root + "/etc/slim.conf";
    if (isRegularFile(slim)) {
        auto lines = splitLines(readFile(slim));
        bool u = false, a = false;
        for (auto& l : lines) {
            std::string t = trim(l);
            if (startsWith(t, "default_user") || startsWith(t, "#default_user") || startsWith(t, "# default_user")) { l = "default_user " + user; u = true; }
            else if (startsWith(t, "auto_login") || startsWith(t, "#auto_login") || startsWith(t, "# auto_login")) { l = "auto_login yes"; a = true; }
        }
        if (!u) lines.push_back("default_user " + user);
        if (!a) lines.push_back("auto_login yes");
        std::string text; for (const auto& l : lines) text += l + "\n";
        writeFile(slim, text);
    }
    // greetd
    std::string greetd = root + "/etc/greetd/config.toml";
    if (isRegularFile(greetd)) {
        std::vector<std::string> out;
        bool inInitial = false, userSet = false;
        const std::string userLine = "user = \"" + user + "\"";
        for (const auto& l : splitLines(readFile(greetd))) {
            std::string t = trim(l);
            if (startsWith(t, "[") && endsWith(t, "]")) {
                if (inInitial && !userSet) { out.push_back(userLine); userSet = true; }
                inInitial = (t == "[initial_session]");
            } else if (inInitial && startsWith(t, "user")) {
                out.push_back(userLine); userSet = true; continue;
            }
            out.push_back(l);
        }
        if (inInitial && !userSet) { out.push_back(userLine); userSet = true; }
        if (!userSet) { out.push_back(""); out.push_back("[initial_session]"); out.push_back(userLine); }
        std::string text; for (const auto& l : out) text += l + "\n";
        writeFile(greetd, text);
    }
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
    std::vector<std::string> ex = {"dev/*", "mnt", "media", "proc/*", "sys/*", "run/*", "tmp/*", "var/tmp/*", "var/tmp/.??*", "lost+found"};

    std::string rel = c.workDir.substr(c.workDir[0] == '/' ? 1 : 0);
    ex.push_back(rel + "/.overlay/*");
    ex.push_back(rel + "/.overlay/.??*");
    ex.push_back(rel + "/isodir/*");
    ex.push_back(rel + "/*.iso");
    // In clone mode /home is bind-mounted into liveroot, so liveroot would see itself
    ex.push_back(c.liveRoot.substr(1));

    for (const char* e : {
        "boot/efi/EFI", "boot/loader/entries/", "boot/grub/!(themes|unicode.pf2)",
        "boot/cdb-initrd.img", "boot/initramfs-0-rescue-*.img", "boot/vmlinuz-0-rescue-*",
        "etc/fstab", "etc/mtab", "swapfile",
        "etc/udev/rules.d/70-persistent-cd.rules", "etc/udev/rules.d/70-persistent-net.rules",
        "etc/NetworkManager/system-connections/*", "etc/ssh/ssh_host_*", "var/lib/NetworkManager/secret_key",
        "etc/machine-id", "var/lib/dbus/machine-id", "var/lib/systemd/random-seed",
        "etc/resolv.conf",
        "etc/adjtime", "etc/crypttab", "etc/X11/xorg.conf",
        "etc/X11/xorg.conf.d/20-nvidia.conf", "etc/X11/xorg.conf.d/20-intel.conf",
        "etc/X11/xorg.conf.d/20-radeon.conf", "etc/X11/xorg.conf.d/20-amd.conf",
        // apt
        "var/cache/apt/archives/*", "var/cache/apt/*.bin", "var/cache/apt/apt-file/*",
        "var/cache/apt-xapian-index/index.*", "var/cache/apt-show-versions/*", "var/cache/debconf/*-old",
        "var/lib/apt/lists/*", "var/lib/apt/periodic/*", "var/lib/dpkg/*-old",
        // pacman
        "var/cache/pacman/pkg/*", "var/lib/pacman/sync/*",
        // dnf / packagekit
        "var/cache/dnf/*", "var/cache/libdnf5/*", "var/cache/dnf5daemon-server/*", "var/cache/PackageKit/*",
        // logs and runtime state
        "var/log/!(apache2|clamav|libvirt|journal|samba)", "var/log/clamav/*", "var/log/journal/*", "var/log/samba/*",
        "var/lib/sudo/*", "var/lib/dhcp/*", "var/lib/urandom/*", "var/lib/udisks/*",
        "var/mail/*", "var/spool/mail/*", "var/spool/anacron/*",
        "var/cache/lightdm", "var/lib/lightdm/.cache", "var/lib/lightdm/.Xauthority",
        // debian derivatives
        "etc/rc*.d/*cryptdisks*", "etc/grub.d/42_openkylin_multiboot", "etc/openkylin-installer", "etc/openkylin-installer/**"
    }) ex.push_back(e);

    for (const auto& s : swapFilesFromFstab()) ex.push_back(s);

    if (c.isGitHubAction) { ex.push_back("usr"); ex.push_back("var"); ex.push_back("opt"); }

    if (c.mode != "clone") {
        ex.push_back("root/*");
        ex.push_back("root/.??*");
    } else {
        for (const char* e : {
            "root/.bash_history", "root/.zsh_history", "root/.cache", "root/.local/share/recently-used.xbel",
            "root/.local/share/Trash/*", "root/.xsession-errors*",
            "home/*/.bash_history", "home/*/.lesshst", "home/*/.local/share/Trash/*",
            "home/*/.local/share/recently-used.xbel", "home/*/.local/share/mc/history", "home/*/.cache/*",
            "home/*/.thumbnails/*", "home/*/.dbus", "home/*/.gvfs", "home/*/.Trash*", "home/*/.xsession-errors*",
            "home/*/.adobe", "home/*/.macromedia", "home/*/.recently-used", "home/*/.recently-used.xbel",
            "home/*/.sudo_as_admin_successful"
        }) ex.push_back(e);
    }

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

inline bool runMksquashfs(const Context& c) {
    std::string algo = c.settings.compressionAlgo.empty() ? "zstd" : c.settings.compressionAlgo;
    int lvl = c.settings.compressionLevel > 0 ? c.settings.compressionLevel : 3;
    unsigned procs = std::thread::hardware_concurrency();
    if (procs == 0) procs = 1;

    std::string dest = c.isoDir + "/live/filesystem.squashfs";
    mkdirP(c.isoDir + "/live");

    std::string cmd = "mksquashfs " + shq(c.liveRoot) + " " + shq(dest) +
        " -no-xattrs -b 1M -processors " + std::to_string(procs) + " -noappend -progress -wildcards" +
        " -p 'mnt d 0755 root root' -p 'media d 0755 root root'" +
        " -ef " + shq(c.excludeList) + " " + squashfsCompressionArgs(algo, std::to_string(lvl), c.distro.arch);

    logNormal("Running: " + cmd);
    return runCommand(cmd) == 0;
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

inline bool runXorriso(const Context& c) {
    if (!isDirectory(c.isoDir)) { logError("xorriso: source directory '" + c.isoDir + "' does not exist"); return false; }

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
    if (runCommand("bash -c " + shq("set -o pipefail; " + cmd)) != 0) {
        runCommand("rm -rf " + shq(dir));
        return false;
    }
    return writeFile(dir + "/.download-complete", "ok\n");
}

}  // namespace native

#endif
