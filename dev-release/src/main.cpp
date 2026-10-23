// claudebackup linux v1.0
// Menu-driven multi-distro live ISO backup in C++. Per-family modules follow
// penguins-eggs (brain.d templates + oa engine), merged into one native binary.

#include <pwd.h>
#include "menu.h"

int main(int argc, char* argv[]) {
    // The menu needs root for mounts; re-launch through sudo once, keeping the real user's name
    if (geteuid() != 0) {
        std::vector<char*> args = {const_cast<char*>("sudo"), const_cast<char*>("-E")};
        for (int i = 0; i < argc; i++) args.push_back(argv[i]);
        args.push_back(nullptr);
        execvp("sudo", args.data());
        std::cerr << COLOR_RED << "Please run with sudo." << COLOR_RESET << std::endl;
        return 1;
    }

    const char* sudoUser = getenv("SUDO_USER");
    if (sudoUser && *sudoUser) USERNAME = sudoUser;
    else {
        struct passwd* pw = getpwuid(getuid());
        USERNAME = pw ? pw->pw_name : "root";
    }

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
