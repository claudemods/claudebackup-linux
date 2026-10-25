// claudebackup linux v1.0
// Menu-driven multi-distro live ISO backup in C++. Per-family modules follow
// penguins-eggs (brain.d templates + oa engine), merged into one native binary.

#include <pwd.h>
#include <climits>
#include "menu.h"

// Passed to the re-launched root process so it knows who started it, whichever tool was used
const std::string USER_ARG = "--cdb-user=";

// The menu needs root for mounts. Distros differ in how you get it (sudo, doas on Alpine,
// run0 on newer systemd, pkexec, or plain su), so try each that is installed, in that order.
void relaunchAsRoot(int argc, char* argv[]) {
    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    std::string exe = n > 0 ? std::string(self, n) : argv[0];

    struct passwd* pw = getpwuid(getuid());
    std::string user = pw ? pw->pw_name : "";

    std::vector<std::string> rest;
    for (int i = 1; i < argc; i++) rest.push_back(argv[i]);
    if (!user.empty()) rest.push_back(USER_ARG + user);

    const std::vector<std::vector<std::string>> tools = {
        {"sudo", "-E"}, {"doas"}, {"run0"}, {"pkexec"}
    };
    for (const auto& t : tools) {
        if (!commandExists(t[0])) continue;
        std::vector<std::string> a = t;
        a.push_back(exe);
        a.insert(a.end(), rest.begin(), rest.end());
        std::vector<char*> args;
        for (auto& s : a) args.push_back(const_cast<char*>(s.c_str()));
        args.push_back(nullptr);
        std::cout << COLOR_CYAN << "Root is needed - asking through " << t[0] << "..." << COLOR_RESET << std::endl;
        execvp(t[0].c_str(), args.data());
    }

    if (commandExists("su")) {
        std::string cmd = shq(exe);
        for (const auto& r : rest) cmd += " " + shq(r);
        std::cout << COLOR_CYAN << "Root is needed - enter the root password for su." << COLOR_RESET << std::endl;
        execlp("su", "su", "root", "-c", cmd.c_str(), (char*)nullptr);
    }

    std::cerr << COLOR_RED << "Please run " << APP_BIN << " as root (no sudo, doas, run0, pkexec or su found)." << COLOR_RESET << std::endl;
    exit(1);
}

// Who started us: our own --cdb-user=, else what sudo / doas / pkexec leave behind, else the login name
std::string originalUser(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (startsWith(a, USER_ARG)) return a.substr(USER_ARG.size());
    }
    for (const char* var : {"SUDO_USER", "DOAS_USER"}) {
        const char* v = getenv(var);
        if (v && *v) return v;
    }
    if (const char* uid = getenv("PKEXEC_UID")) {
        if (struct passwd* pw = getpwuid((uid_t)atoi(uid))) return pw->pw_name;
    }
    if (const char* login = getlogin()) return login;
    struct passwd* pw = getpwuid(getuid());
    return pw ? pw->pw_name : "root";
}

int main(int argc, char* argv[]) {
    if (geteuid() != 0) relaunchAsRoot(argc, argv);

    USERNAME = originalUser(argc, argv);

    mkdirP(configDirPath());
    loadConfig();

    std::thread time_thread(update_time_thread);

    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ICANON;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    showMainMenu();

    time_thread_running = false;
    time_thread.join();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::cout << "\033[?25h" << std::endl;
    return 0;
}
