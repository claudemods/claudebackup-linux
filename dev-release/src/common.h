#ifndef COMMON_H
#define COMMON_H

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

// Product identity
const std::string APP_NAME    = "claudebackup linux";
// Set by the Makefile from dev-release/version.txt
#ifndef CBL_VERSION
#define CBL_VERSION "v1.0"
#endif
const std::string APP_VERSION = std::string(CBL_VERSION).empty() ? "v1.0" : CBL_VERSION;
const std::string APP_BIN     = "claudebackup-linux";

// Filesystem label of the live medium (referenced by boot params, menus and xorriso)
const std::string LIVE_LABEL = "CBL_LIVE";

// Paths
const std::string CONFIG_DIR  = "/etc/claudebackup-linux";
const std::string STAGING_DIR = "/tmp/cbl";
const std::string DEFAULT_WORK_DIR = "/home/claudebackup";
const std::string LOG_FILE    = "/var/log/claudebackup-linux.log";
const std::string BOOTLOADER_URL =
    "https://github.com/pieroproietti/penguins-bootloaders/releases/download/v26.1.16/bootloaders.tar.gz";

// Updates: the version on GitHub's main branch, and the source to build the update from
const std::string REPO_URL           = "https://github.com/claudemods/claudebackup-linux";
const std::string REMOTE_VERSION_URL = "https://raw.githubusercontent.com/claudemods/claudebackup-linux/main/dev-release/version.txt";
const std::string SOURCE_TARBALL_URL = "https://github.com/claudemods/claudebackup-linux/archive/refs/heads/main.tar.gz";
const std::string SOURCE_TARBALL_DIR = "claudebackup-linux-main";   // top folder inside the tarball

// ANSI color codes
const std::string COLOR_RED    = "\033[31m";
const std::string COLOR_GREEN  = "\033[32m";
const std::string COLOR_BLUE   = "\033[34m";
const std::string COLOR_CYAN   = "\033[36m";
const std::string COLOR_YELLOW = "\033[33m";
const std::string COLOR_RESET  = "\033[0m";

// ---------------------------------------------------------------------------
// Logging (every message is also appended to LOG_FILE)
// ---------------------------------------------------------------------------
inline void logToFile(const std::string& tag, const std::string& msg) {
    std::ofstream f(LOG_FILE, std::ios::app);
    if (f.is_open()) f << "[" << tag << "] " << msg << "\n";
}

inline void logNormal(const std::string& msg)  { std::cout << COLOR_CYAN   << "[cbl] " << COLOR_RESET << msg << std::endl; logToFile("INFO", msg); }
inline void logSuccess(const std::string& msg) { std::cout << COLOR_GREEN  << "[cbl] " << COLOR_RESET << msg << std::endl; logToFile("OK", msg); }
inline void logWarning(const std::string& msg) { std::cout << COLOR_YELLOW << "[cbl] " << COLOR_RESET << msg << std::endl; logToFile("WARN", msg); }
inline void logError(const std::string& msg)   { std::cerr << COLOR_RED    << "[cbl] " << COLOR_RESET << msg << std::endl; logToFile("ERROR", msg); }
[[noreturn]] inline void fatal(const std::string& msg) { logError(msg); std::exit(1); }

// ---------------------------------------------------------------------------
// Small filesystem / string helpers
// ---------------------------------------------------------------------------
inline bool pathExists(const std::string& p) { struct stat st; return lstat(p.c_str(), &st) == 0; }
inline bool isDirectory(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
inline bool isRegularFile(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }

inline std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline bool writeFile(const std::string& p, const std::string& content, mode_t mode = 0644) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    f << content;
    f.close();
    chmod(p.c_str(), mode);
    return true;
}

inline bool appendFile(const std::string& p, const std::string& content) {
    std::ofstream f(p, std::ios::app);
    if (!f.is_open()) return false;
    f << content;
    return true;
}

inline std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) out.push_back(line);
    return out;
}

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

inline bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }
inline bool endsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
inline bool contains(const std::string& s, const std::string& p) { return s.find(p) != std::string::npos; }

inline std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

inline std::string toLower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// Single-quote a string for safe use inside a shell command line
inline std::string shq(const std::string& s) {
    return "'" + replaceAll(s, "'", "'\\''") + "'";
}

inline void mkdirP(const std::string& p, mode_t mode = 0755) {
    std::string cur;
    std::stringstream ss(p);
    std::string part;
    if (!p.empty() && p[0] == '/') cur = "/";
    while (std::getline(ss, part, '/')) {
        if (part.empty()) continue;
        cur += part + "/";
        mkdir(cur.c_str(), mode);
    }
}

inline std::string dirName(const std::string& p) {
    size_t pos = p.find_last_of('/');
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return p.substr(0, pos);
}

inline std::string baseName(const std::string& p) {
    size_t pos = p.find_last_of('/');
    return pos == std::string::npos ? p : p.substr(pos + 1);
}

// Runs a command through the shell with no colour change (probes whose output is discarded)
inline int runPlain(const std::string& cmd) {
    fflush(stdout);
    int status = system(cmd.c_str());
    if (status == -1) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Runs a command through the shell, its output flows to the terminal in green. Returns exit code.
inline int runCommand(const std::string& cmd) {
    std::cout << COLOR_GREEN << std::flush;
    int rc = runPlain(cmd);
    std::cout << COLOR_RESET << std::flush;
    return rc;
}

// Runs a command and returns its stdout (trailing newline stripped)
inline std::string captureCommand(const std::string& cmd) {
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

inline bool commandExists(const std::string& name) {
    return runPlain("command -v " + shq(name) + " >/dev/null 2>&1") == 0;
}

#endif
