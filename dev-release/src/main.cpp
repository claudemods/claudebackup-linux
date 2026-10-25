// claudebackup linux v1.0
// Menu-driven multi-distro live ISO backup in C++. Per-family modules follow
// penguins-eggs (brain.d templates + oa engine), merged into one native binary.

#include <pwd.h>
#include <climits>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
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

// The terminal ignored the resize request: open the app again in a new window of the same
// terminal, sized to fit (as the user, before asking for root). Returns true if it opened.
bool reopenInBiggerTerminal(int argc, char* argv[], int rows, int cols) {
    if (getenv("CDB_REOPENED")) return false;                        // never loop
    if (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) return false;

    const std::string R = std::to_string(rows), C = std::to_string(cols);
    std::vector<std::string> cmd;
    if (getenv("KONSOLE_VERSION") && commandExists("konsole"))
        cmd = {"konsole", "-p", "TerminalColumns=" + C, "-p", "TerminalRows=" + R, "-e"};
    else if (getenv("KITTY_WINDOW_ID") && commandExists("kitty"))
        cmd = {"kitty", "-o", "initial_window_width=" + C + "c", "-o", "initial_window_height=" + R + "c"};
    else if ((getenv("ALACRITTY_WINDOW_ID") || getenv("ALACRITTY_SOCKET")) && commandExists("alacritty"))
        cmd = {"alacritty", "-o", "window.dimensions.columns=" + C, "-o", "window.dimensions.lines=" + R, "-e"};
    else if (getenv("GNOME_TERMINAL_SCREEN") && commandExists("gnome-terminal"))
        cmd = {"gnome-terminal", "--geometry=" + C + "x" + R, "--"};
    else if (getenv("VTE_VERSION") && commandExists("xfce4-terminal"))
        cmd = {"xfce4-terminal", "--geometry=" + C + "x" + R, "-x"};
    else if (getenv("XTERM_VERSION") && commandExists("xterm"))
        cmd = {"xterm", "-geometry", C + "x" + R, "-e"};
    else return false;

    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    cmd.push_back(n > 0 ? std::string(self, n) : argv[0]);
    for (int i = 1; i < argc; i++) cmd.push_back(argv[i]);

    setenv("CDB_REOPENED", "1", 1);
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); }
        std::vector<char*> args;
        for (auto& s : cmd) args.push_back(const_cast<char*>(s.c_str()));
        args.push_back(nullptr);
        execvp(args[0], args.data());
        _exit(127);
    }
    usleep(800000);   // a failed exec exits at once
    int status = 0;
    if (waitpid(pid, &status, WNOHANG) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 127) return false;
    std::cout << COLOR_GREEN << APP_NAME << " opened in a bigger " << cmd[0] << " window (" << C << "x" << R << ")." << COLOR_RESET << std::endl;
    return true;
}

// Every screen must fit without scrolling: resize the terminal, or reopen in a bigger window
void fitTerminal(int argc, char* argv[], bool mayReopen) {
    const int rows = requiredRows(), cols = UI_COLUMNS;
    if (resizeTerminal(rows, cols)) return;
    if (mayReopen && reopenInBiggerTerminal(argc, argv, rows, cols)) exit(0);
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    std::cout << COLOR_YELLOW << "This window is " << w.ws_col << "x" << w.ws_row << "; make it at least "
              << cols << "x" << rows << " so every screen fits without scrolling." << COLOR_RESET << std::endl;
    sleep(2);
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
    if (geteuid() != 0) {
        // As the user: size the window first (a new terminal window can only be opened from here)
        USERNAME = originalUser(argc, argv);
        loadConfig();
        fitTerminal(argc, argv, true);
        relaunchAsRoot(argc, argv);
    }

    USERNAME = originalUser(argc, argv);

    mkdirP(configDirPath());
    loadConfig();
    fitTerminal(argc, argv, false);

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
