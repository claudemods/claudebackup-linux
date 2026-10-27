#ifndef DISTRO_H
#define DISTRO_H

#include <map>
#include <ctime>
#include <unistd.h>
#include <sys/utsname.h>
#include "common.h"

// Same mapping as penguins-eggs coa/brain.d/index.yaml: every distro id (or ID_LIKE entry)
// resolves to one of the six supported families.
struct FamilyEntry {
    std::string id;                    // family key
    std::vector<std::string> like;     // derivative ids that belong to it
};

inline const std::vector<FamilyEntry>& familyTable() {
    static const std::vector<FamilyEntry> table = {
        {"debian",   {"abellha", "devuan", "kali", "linuxmint", "openkylin", "pop", "quirinux", "ubuntu"}},
        {"arch",     {"archlinux", "archcraft", "cachyos", "endeavouros", "garuda", "rebornos"}},
        {"fedora",   {"nobara", "rhel"}},
        {"opensuse", {"opensuse-slowroll", "opensuse-tumbleweed", "opensuse-leap", "suse"}},
        {"alpine",   {}},
        {"gentoo",   {"calculate", "funtoo", "pentoo"}},
    };
    return table;
}

struct Distro {
    std::string distroID;    // os-release ID
    std::string prettyName;
    std::string codename;
    std::string release;
    std::string family = "generic";   // debian | archlinux | fedora | opensuse | alpine | gentoo | generic
    std::string familyLabel = "Generic";
    std::string arch;

    // Module directory name used upstream
    std::string moduleName() const {
        if (family == "archlinux") return "arch-family";
        return family;
    }
    bool supported() const { return family != "generic"; }
    // Alpine and Gentoo only build squashfs images
    bool erofsSupported() const { return supported() && family != "alpine" && family != "gentoo"; }
};

inline std::map<std::string, std::string> parseOsRelease() {
    std::map<std::string, std::string> info;
    for (const auto& line : splitLines(readFile("/etc/os-release"))) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string val = trim(line.substr(eq + 1));
        if (val.size() >= 2 && (val.front() == '"' || val.front() == '\'')) val = val.substr(1, val.size() - 2);
        info[line.substr(0, eq)] = val;
    }
    return info;
}

inline void setFamily(Distro& d, const std::string& entryId) {
    if (entryId == "debian")        { d.family = "debian";    d.familyLabel = "Debian"; }
    else if (entryId == "arch")     { d.family = "archlinux"; d.familyLabel = "Arch"; }
    else if (entryId == "fedora")   { d.family = "fedora";    d.familyLabel = "Fedora"; }
    else if (entryId == "opensuse") { d.family = "opensuse";  d.familyLabel = "Opensuse"; }
    else if (entryId == "alpine")   { d.family = "alpine";    d.familyLabel = "Alpine"; }
    else if (entryId == "gentoo")   { d.family = "gentoo";    d.familyLabel = "Gentoo"; }
}

inline Distro detectDistro() {
    auto info = parseOsRelease();
    Distro d;
    d.distroID   = info["ID"];
    d.prettyName = info.count("PRETTY_NAME") ? info["PRETTY_NAME"] : info["ID"];
    d.codename   = info["VERSION_CODENAME"];
    d.release    = info["VERSION_ID"];

    struct utsname u;
    d.arch = (uname(&u) == 0) ? u.machine : "x86_64";

    // Candidates: own id first, then every ID_LIKE entry
    std::vector<std::string> candidates = {toLower(d.distroID)};
    std::stringstream ss(toLower(info.count("ID_LIKE") ? info["ID_LIKE"] : ""));
    std::string tok;
    while (ss >> tok) candidates.push_back(tok);

    for (const auto& c : candidates) {
        for (const auto& entry : familyTable()) {
            bool hit = (c == entry.id);
            for (const auto& l : entry.like) if (c == l) hit = true;
            if (hit) { setFamily(d, entry.id); return d; }
        }
    }
    return d;
}

// Default ISO name: "claudebackup-<distro>-<YYYY-MM-DD_HHMM>.iso", e.g. claudebackup-gentoo-2026-10-26_1430.iso
inline std::string makeIsoName(const Distro& d) {
    char ts[64];
    time_t now = time(nullptr);
    strftime(ts, sizeof(ts), "%Y-%m-%d_%H%M", localtime(&now));
    std::string distro = d.distroID.empty() ? "linux" : toLower(d.distroID);
    return replaceAll("claudebackup-" + distro + "-" + ts + ".iso", " ", "-");
}

#endif
