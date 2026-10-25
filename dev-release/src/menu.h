#ifndef MENU_H
#define MENU_H

#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <termios.h>
#include <fcntl.h>
#include <pwd.h>
#include <functional>
#include <sys/ioctl.h>
#include "remaster.h"

// Interactive menu, same look and feel as the claudemods multi iso creator:
// red ASCII banner, live clock, df line, checkbox status list, arrow-key navigation.

std::string USERNAME;

// Time-related globals
std::atomic<bool> time_thread_running(true);
std::mutex time_mutex;
std::string current_time_str;
std::atomic<bool> menu_needs_refresh(false);

const std::string COLOR_HIGHLIGHT = "\033[32m";

// Saved from the setup menu (~/.config/claudebackup-linux/configuration.txt)
struct MenuConfig {
    std::string isoName;       // empty = auto-generated
    std::string workDir;       // empty = default
    std::string outputDir;     // empty = work dir
    int level = 3;             // zstd level (Gentoo uses gzip, no level)
    bool ramMode = true;
    std::string isoTag;        // Arch volume label, empty = 2026
    std::string cloneDir;      // <parent>/clone_system_temp, "/" is bound here; empty = default
    std::string imageFormat = "squashfs";   // Arch only: squashfs | erofs
};
MenuConfig mcfg;

// The running distro, detected once: menus, status and guide only show what applies to it
const Distro& sysDistro() { static Distro d = detectDistro(); return d; }
bool onFamily(const char* f) { return sysDistro().family == f; }

// The user's real home from the password database (root is /root, and homes are not always /home/<user>)
std::string userHome() {
    struct passwd* pw = getpwnam(USERNAME.c_str());
    if (pw && pw->pw_dir && *pw->pw_dir) return pw->pw_dir;
    return USERNAME == "root" ? "/root" : "/home/" + USERNAME;
}

std::string defaultCloneDir() { return userHome() + "/clone_system_temp"; }

std::string configDirPath() { return userHome() + "/.config/" + APP_BIN; }
std::string configFilePath() { return configDirPath() + "/configuration.txt"; }

void saveConfig() {
    mkdirP(configDirPath());
    std::ofstream f(configFilePath());
    if (!f.is_open()) { std::cerr << COLOR_RED << "Failed to save configuration to " << configFilePath() << COLOR_RESET << std::endl; return; }
    // We run as root: keep the user's own config folder owned by the user
    runCommand("chown -R " + shq(USERNAME) + " " + shq(configDirPath()) + " 2>/dev/null");
    f << "isoName=" << mcfg.isoName << "\n";
    f << "workDir=" << mcfg.workDir << "\n";
    f << "outputDir=" << mcfg.outputDir << "\n";
    f << "level=" << mcfg.level << "\n";
    f << "ramMode=" << (mcfg.ramMode ? "1" : "0") << "\n";
    f << "isoTag=" << mcfg.isoTag << "\n";
    f << "cloneDir=" << mcfg.cloneDir << "\n";
    f << "imageFormat=" << mcfg.imageFormat << "\n";
}

void loadConfig() {
    for (const auto& line : splitLines(readFile(configFilePath()))) {
        size_t d = line.find('=');
        if (d == std::string::npos) continue;
        std::string k = line.substr(0, d), v = line.substr(d + 1);
        if (k == "isoName") mcfg.isoName = v;
        else if (k == "workDir") mcfg.workDir = v;
        else if (k == "outputDir") mcfg.outputDir = v;
        else if (k == "level") mcfg.level = atoi(v.c_str());
        else if (k == "ramMode") mcfg.ramMode = (v == "1");
        else if (k == "isoTag") mcfg.isoTag = v;
        else if (k == "cloneDir") mcfg.cloneDir = v;
        else if (k == "imageFormat") mcfg.imageFormat = (v == "erofs") ? "erofs" : "squashfs";
    }
}

RemasterOptions currentOptions() {
    RemasterOptions o;
    o.workDir = mcfg.workDir;
    o.outputDir = mcfg.outputDir;
    o.isoName = mcfg.isoName;
    o.algo = remaster::compressionFor(sysDistro());
    o.level = mcfg.level;
    o.ramMode = mcfg.ramMode ? 1 : 0;
    o.isoTag = mcfg.isoTag;
    o.cloneDir = mcfg.cloneDir.empty() ? defaultCloneDir() : mcfg.cloneDir;
    o.imageFormat = mcfg.imageFormat;
    return o;
}

void update_time_thread() {
    while (time_thread_running) {
        time_t now = time(NULL);
        struct tm* t = localtime(&now);
        char datetime[50];
        strftime(datetime, sizeof(datetime), "%d/%m/%Y %H:%M:%S", t);
        {
            std::lock_guard<std::mutex> lock(time_mutex);
            current_time_str = datetime;
        }
        menu_needs_refresh = true;
        sleep(1);
    }
}

void clearScreen() { std::cout << "\033[H\033[2J\033[3J"; }

int getch() {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}

void pressAnyKey() {
    std::cout << COLOR_GREEN << "\nPress any key to continue..." << COLOR_RESET;
    std::cout.flush();
    getch();
}

std::string getUserInput(const std::string& prompt) {
    std::cout << COLOR_GREEN << prompt << COLOR_RESET;
    std::cout.flush();

    std::string input;
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    std::cout << "\033[?25h";
    std::cout.flush();

    size_t cursor_pos = 0;
    while (true) {
        int ch = getchar();
        if (ch == '\n' || ch == EOF) break;
        if (ch == 27) {
            if (getchar() == '[') {
                int arrow = getchar();
                if (arrow == 'D' && cursor_pos > 0) { cursor_pos--; std::cout << "\033[D"; }
                else if (arrow == 'C' && cursor_pos < input.length()) { cursor_pos++; std::cout << "\033[C"; }
            }
        } else if (ch == 127 || ch == 8) {
            if (cursor_pos > 0) {
                input.erase(cursor_pos - 1, 1);
                cursor_pos--;
                std::cout << "\b\033[K" << input.substr(cursor_pos);
                for (size_t i = 0; i < input.length() - cursor_pos; i++) std::cout << "\033[D";
                std::cout.flush();
            }
        } else if (ch >= 32 && ch <= 126) {
            input.insert(cursor_pos, 1, (char)ch);
            std::cout << "\033[K" << input.substr(cursor_pos);
            for (size_t i = 0; i < input.length() - cursor_pos - 1; i++) std::cout << "\033[D";
            cursor_pos++;
            std::cout.flush();
        }
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::cout << std::endl;
    return input;
}

// ---------------------------------------------------------------------------
// Status block shown above every menu
// ---------------------------------------------------------------------------
std::string checkLine(bool ok, const std::string& text) {
    return std::string(" ") + (ok ? COLOR_GREEN + "[✓]" : COLOR_RED + "[ ]") + COLOR_RESET +
           (ok ? COLOR_GREEN : COLOR_RED) + " " + text + COLOR_RESET + "\n";
}

std::string compressionText() {
    if (onFamily("archlinux") && mcfg.imageFormat == "erofs") return "erofs (mkfs.erofs zstd)";
    if (onFamily("gentoo")) return "squashfs gzip";
    return "squashfs zstd level " + std::to_string(mcfg.level);
}

// Only the lines that apply to the running distro
std::string statusBlock() {
    const Distro& d = sysDistro();
    bool toolsOk = remaster::missingTools(d).empty();

    std::string s = COLOR_CYAN + "Current Configuration:" + COLOR_RESET + "\n";
    s += checkLine(d.supported(), "Distro: " + d.prettyName + (d.supported() ? " (family: " + d.family + ")" : " (not supported)"));
    s += checkLine(toolsOk, "Needed Tools");
    s += checkLine(remaster::bootFilesReady(d), d.family == "archlinux" ? "Boot Files: embedded claudemods zip" : "Bootloaders");
    if (d.family == "archlinux")
        s += checkLine(!mcfg.isoTag.empty(), "ISO Tag: " + (mcfg.isoTag.empty() ? std::string("2026 (default)") : mcfg.isoTag));
    if (remaster::calamaresAvailable(d)) s += checkLine(commandExists("calamares"), "Calamares");
    if (d.family == "alpine") s += checkLine(remaster::alpineInstallerReady(), "Alpine Installer Tools");
    s += checkLine(!mcfg.isoName.empty(), "ISO Name: " + (mcfg.isoName.empty() ? std::string("Auto") : mcfg.isoName));
    s += checkLine(true, "Work Directory: " + (mcfg.workDir.empty() ? DEFAULT_WORK_DIR : mcfg.workDir));
    s += checkLine(true, "Output Directory: " + (mcfg.outputDir.empty() ? std::string("Work directory") : mcfg.outputDir));
    s += checkLine(!mcfg.cloneDir.empty(), "Clone Directory: " + (mcfg.cloneDir.empty() ? "Not set (uses " + defaultCloneDir() + ")" : mcfg.cloneDir));
    s += checkLine(true, "Image: " + compressionText());
    return s;
}

// showMenu with echo disabled during navigation, cursor hidden
int showMenu(const std::string& title, const std::vector<std::string>& items, int selected) {
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    // Build the whole screen first, then write once to prevent flicker
    std::string menuBuffer = "\033[H\033[2J\033[3J\033[?25l";

    std::istringstream ascii_stream(
        "███████████████████████████████████████████████████████████████████████████████████╗\n"
        "░█████╗░██║░░░░░░█████╗░██║░░░██║██████╗░███████╗███╗░░░███╗░█████╗░██████╗░██████╗\n"
        "██╔══██╗██║░░░░░██╔══██╗██║░░░██║██╔══██╗██╔════╝████╗░████║██╔══██╗██╔══██╗██╔════╝\n"
        "██║░░╚═╝██║░░░░░███████║██║░░░██║██║░░██║█████╗░░██╔████╔██║██║░░██║██║░░██║╚█████╗░\n"
        "██║░░██╗██║░░░░░██╔══██║██║░░░██║██║░░██║██╔══╝░░██║╚██╔╝██║██║░░██║██║░░██║░╚═══██╗\n"
        "╚█████╔╝███████╗██║░░██║╚██████╔╝██████╔╝███████╗██║░╚═╝░██║╚█████╔╝██████╔╝██████╔╝\n"
        "░╚════╝░╚══════╝╚═╝░░╚═╝░░░░░░╚═════╝░╚═════╝░╚══════╝╚═╝╚═╝░░░╚═╝░╚════╝░╚═════╝░╝░\n"
        "███████████████████████████████████████████████████████████████████████████████████╝\n");
    std::string line;
    while (std::getline(ascii_stream, line)) menuBuffer += COLOR_RED + line + COLOR_RESET + "\n";

    menuBuffer += COLOR_RED + "                    " + APP_NAME + " " + APP_VERSION + COLOR_RESET + "\n";
    menuBuffer += COLOR_RED + "Back up your running " + sysDistro().prettyName + " system as a bootable live ISO" + COLOR_RESET + "\n";
    menuBuffer += COLOR_RED + "Sailing the 7 seas like Penguin's Eggs Remastersys, Refracta, Systemback and father Knoppix!" + COLOR_RESET + "\n";
    {
        std::lock_guard<std::mutex> lock(time_mutex);
        menuBuffer += COLOR_GREEN + "Current Time: " + current_time_str + COLOR_RESET + "\n";
    }
    menuBuffer += COLOR_GREEN + "Filesystem      Size  Used Avail Use% Mounted on" + COLOR_RESET + "\n";
    FILE* dfPipe = popen("df -h / | tail -1", "r");
    if (dfPipe) {
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), dfPipe) != nullptr) menuBuffer += COLOR_GREEN + std::string(buffer) + COLOR_RESET;
        pclose(dfPipe);
    }
    menuBuffer += "\n" + statusBlock();

    menuBuffer += COLOR_CYAN + "\n  " + title + COLOR_RESET + "\n";
    menuBuffer += COLOR_CYAN + "  " + std::string(title.length(), '-') + COLOR_RESET + "\n";
    for (size_t i = 0; i < items.size(); i++) {
        if (i == static_cast<size_t>(selected)) menuBuffer += COLOR_HIGHLIGHT + "➤ " + items[i] + COLOR_RESET + "\n";
        else menuBuffer += COLOR_RED + "  " + items[i] + COLOR_RESET + "\n";
    }

    std::cout << menuBuffer;
    std::cout.flush();
    menu_needs_refresh = false;

    int oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    int key = -1;
    while (key == -1) {
        if (menu_needs_refresh) {
            fcntl(STDIN_FILENO, F_SETFL, oldf);
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
            return -1;   // re-render so the clock ticks
        }
        fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
        key = getchar();
        if (key == EOF) {
            key = -1;
            fcntl(STDIN_FILENO, F_SETFL, oldf);
            usleep(50000);
        } else if (key == 27) {
            int key2 = getchar();
            if (key2 == '[') {
                int key3 = getchar();
                key = (key3 == 'A') ? 'A' : (key3 == 'B') ? 'B' : -1;
            } else key = -1;
        }
    }
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::cout << "\033[?25h";
    return key;
}

// Runs a menu loop; returns the chosen index (Enter) once the user picks something
int pickFromMenu(const std::string& title, const std::vector<std::string>& items, int& selected) {
    while (true) {
        int key = showMenu(title, items, selected);
        if (key == -1) continue;
        if (key == 'A' && selected > 0) selected--;
        else if (key == 'B' && selected < static_cast<int>(items.size()) - 1) selected++;
        else if (key == '\n') return selected;
    }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
// The guide for the running distro only
std::string guideText() {
    const std::string& f = sysDistro().family;
    const bool arch = f == "archlinux", alpine = f == "alpine", gentoo = f == "gentoo";
    std::string g =
        "A full clone: your users, passwords, settings and files are copied into the ISO exactly as\n"
        "they are. No live account is created and nothing is added to the clone.\n"
        "The image is made from '/' bind-mounted at your Clone Directory. A bind of '/' only shows the\n"
        "root filesystem: separate partitions or btrfs subvolumes (e.g. @home, /boot) are not included.\n"
        "Each box under 'Current Configuration' shows what is ready ([x]) or still to do ([ ]).\n\n";
    int n = 0;
    auto step = [&](const std::string& text) { g += std::to_string(++n) + ". " + text + "\n"; };
    const std::string pad = "                                     ";

    if (arch)        step("Setup > Install Needed Tools      squashfs-tools, erofs-utils, libisoburn, mtools, unzip, mkinitcpio");
    else if (gentoo) step("Setup > Install Needed Tools      emerge squashfs-tools, libisoburn, mtools, curl, dracut, lvm2\n" +
                          pad + "(fetches the gentoo repo / fixes the profile if needed)");
    else if (f == "debian") step("Setup > Install Needed Tools      squashfs-tools, xorriso, mtools, curl, live-boot");
    else if (f == "fedora") step("Setup > Install Needed Tools      squashfs-tools, xorriso, mtools, curl, dracut-live");
    else             step("Setup > Install Needed Tools      squashfs-tools, xorriso, mtools, dosfstools, curl");
    if (!arch) step("Setup > Download Bootloaders      one-off download of the BIOS/UEFI boot files");
    step("Setup > ISO Name / Work / Output / Clone Directory   (blank ISO name = auto)");
    if (arch) {
        step("Setup > zstd Level / Image Format squashfs (zstd) or erofs (mkfs.erofs zstd, kernel 6.10+)");
        step("Setup > ISO Tag                   volume label the live system boots from (default 2026)");
    } else if (!gentoo) {
        step("Setup > zstd Compression Level    1-22, default 3 (higher = smaller, slower)");
    }
    step(std::string("Setup > RAM Mode                  adds a ") + (arch ? "copytoram=y" : "toram") + " boot entry");
    if (alpine) step("Setup > Alpine Installer Tools    tools the live ISO needs to install the clone to disk\n" +
                     pad + "(do this BEFORE building, so the ISO contains them)");
    else        step("Setup > Install Calamares         ext4 or btrfs zstd:22, claudemods branding\n" +
                     pad + "(do this BEFORE building, so the ISO contains it)");
    step("Detected System And Build Steps   check it says 'Support: yes'");
    step("Create Bootable ISO               everything is unmounted at the end, even on Ctrl+C");
    if (alpine) step("Install Cloned System To Disk     (in the live ISO) your clone back on a disk, not a\n" +
                     pad + "fresh Alpine: ext4 or btrfs, GRUB for UEFI or BIOS");
    else        step("Launch Calamares                  (in the live ISO) installs the clone to a disk");
    g += "   Install ISO To USB erases the whole target drive. Test the ISO in a VM (UEFI) first.\n\n";
    if (gentoo)
        g += "Gentoo: the image is squashfs gzip. The kernel needs CONFIG_SQUASHFS, CONFIG_OVERLAY_FS,\n"
             "CONFIG_ISO9660_FS and CONFIG_BLK_DEV_LOOP (=m or =y); the build names any that are off.\n";
    if (f == "fedora") g += "Fedora: run 'setenforce 0' as root before building.\n";
    g += "Free space needed is roughly twice the size of the finished ISO. Log: " + LOG_FILE + "\n";
    return g;
}

void showGuide() {
    clearScreen();
    std::cout << COLOR_CYAN << APP_NAME << " " << APP_VERSION << " - guide (" << sysDistro().prettyName << ")"
              << COLOR_RESET << "\n\n" << COLOR_GREEN << guideText() << COLOR_RESET;
    pressAnyKey();
}
void showSystemInfo() {
    clearScreen();
    Context c = remaster::makeContext(currentOptions());
    std::cout << COLOR_CYAN << "Detected system" << COLOR_RESET << "\n" << COLOR_GREEN
              << "Distro:  " << c.distro.prettyName << " (id=" << c.distro.distroID << ")\n"
              << "Family:  " << c.distro.family << " -> module " << c.distro.moduleName() << "\n"
              << "Arch:    " << c.distro.arch << "\n"
              << "Support: " << (c.distro.supported() ? "yes" : "NO (unmapped distro)") << "\n"
              << "Boot:    " << families::bootParams(c) << "\n"
              << "ISO:     " << c.isoPath << "\n\nBuild steps:\n";
    int i = 0;
    for (const auto& s : plan::build(c)) {
        i++;
        std::cout << "  " << (i < 10 ? " " : "") << i << ". " << s.name << " - " << s.description << "\n";
    }
    std::cout << COLOR_RESET;
    pressAnyKey();
}

void createISO() {
    clearScreen();
    std::cout << COLOR_CYAN << "Starting ISO creation process...\n" << COLOR_RESET;
    bool ok = remaster::run(currentOptions());
    if (ok) {
        std::string dir = mcfg.outputDir.empty() ? (mcfg.workDir.empty() ? DEFAULT_WORK_DIR : mcfg.workDir) : mcfg.outputDir;
        runCommand("chown -R " + shq(USERNAME) + " " + shq(dir) + "/*.iso 2>/dev/null");
        std::cout << COLOR_GREEN << "\nISO creation finished." << COLOR_RESET << std::endl;
    } else {
        std::cout << COLOR_RED << "\nISO creation failed. See " << LOG_FILE << COLOR_RESET << std::endl;
    }
    pressAnyKey();
}

void installISOToUSB() {
    std::string dir = mcfg.outputDir.empty() ? (mcfg.workDir.empty() ? DEFAULT_WORK_DIR : mcfg.workDir) : mcfg.outputDir;
    std::vector<std::string> isoFiles;
    for (const auto& n : splitLines(captureCommand("ls -1 " + shq(dir) + " 2>/dev/null")))
        if (endsWith(n, ".iso")) isoFiles.push_back(n);

    if (isoFiles.empty()) {
        std::cerr << COLOR_RED << "No ISO files found in " << dir << COLOR_RESET << std::endl;
        pressAnyKey();
        return;
    }
    std::cout << COLOR_GREEN << "Available ISO files:" << COLOR_RESET << std::endl;
    for (size_t i = 0; i < isoFiles.size(); i++) std::cout << COLOR_GREEN << (i + 1) << ") " << isoFiles[i] << COLOR_RESET << std::endl;

    int choice = atoi(getUserInput("Select ISO file (1-" + std::to_string(isoFiles.size()) + "): ").c_str());
    if (choice < 1 || choice > static_cast<int>(isoFiles.size())) {
        std::cerr << COLOR_RED << "Invalid selection!" << COLOR_RESET << std::endl;
        pressAnyKey();
        return;
    }
    std::string iso = dir + "/" + isoFiles[choice - 1];

    std::cout << COLOR_CYAN << "\nAvailable drives:" << COLOR_RESET << std::endl;
    // lsblk is util-linux; plain BusyBox systems (Alpine) only have /proc/partitions
    if (commandExists("lsblk")) runCommand("lsblk -d -o NAME,SIZE,MODEL | grep -v loop");
    else runCommand("grep -vE ' (loop|ram)[0-9]' /proc/partitions");
    std::string target = getUserInput("Enter target drive (e.g., /dev/sda): ");
    if (!startsWith(target, "/dev/")) {
        std::cerr << COLOR_RED << "Invalid drive!" << COLOR_RESET << std::endl;
        pressAnyKey();
        return;
    }
    std::cout << COLOR_RED << "\nWARNING: This will overwrite all data on " << target << "!" << COLOR_RESET << std::endl;
    std::string confirm = getUserInput("Are you sure you want to continue? (y/N): ");
    if (confirm != "y" && confirm != "Y") { std::cout << COLOR_CYAN << "Operation cancelled." << COLOR_RESET << std::endl; pressAnyKey(); return; }

    // GNU dd shows progress; BusyBox dd (Alpine) has no status=progress / oflag=sync
    bool gnuDd = runCommand("dd --version >/dev/null 2>&1") == 0;
    std::string dd = "dd if=" + shq(iso) + " of=" + shq(target) + " bs=4M" + (gnuDd ? " status=progress oflag=sync" : "");
    if (runCommand(dd) != 0 || runCommand("sync") != 0) {
        std::cerr << COLOR_RED << "\nWriting to " << target << " failed." << COLOR_RESET << std::endl;
        pressAnyKey();
        return;
    }
    std::cout << COLOR_GREEN << "\nISO written to " << target << COLOR_RESET << std::endl;
    pressAnyKey();
}

// ---------------------------------------------------------------------------
// Setup menu
// ---------------------------------------------------------------------------
// One Setup item: its label and what it does
struct SetupItem {
    std::string label;
    std::function<void()> run;
};

void setupInstallTools() {
    const Distro& d = sysDistro();
    std::string cmd = remaster::installToolsCommand(d);
    if (cmd.empty()) std::cout << COLOR_RED << "Unsupported distro." << COLOR_RESET << std::endl;
    else if (d.family == "gentoo" && !remaster::prepareGentooPortage())
        logError("Portage is not usable yet (see above), so nothing was emerged.");
    else { std::cout << COLOR_CYAN << "Running: " << cmd << COLOR_RESET << std::endl; runCommand(cmd); }
    pressAnyKey();
}

void setupCloneDir() {
    // cmiadvanced setCloneDir: parent folder + /clone_system_temp
    std::string def = userHome();
    std::cout << COLOR_GREEN << "Current clone directory: " << (mcfg.cloneDir.empty() ? "Not set" : mcfg.cloneDir) << "\n"
              << "Default directory: " << def << COLOR_RESET << std::endl;
    std::string parent = getUserInput("Enter parent directory for clone_system_temp folder (e.g., " + def + " or $USER): ");
    size_t pos = parent.find("$USER");
    if (pos != std::string::npos) parent.replace(pos, 5, USERNAME);
    if (parent.empty()) parent = def;
    while (parent.size() > 1 && parent.back() == '/') parent.pop_back();
    if (parent[0] != '/') {
        std::cout << COLOR_RED << "Please enter an absolute path (starting with /)." << COLOR_RESET << std::endl;
        pressAnyKey();
        return;
    }
    mcfg.cloneDir = (parent == "/" ? "" : parent) + "/clone_system_temp";
    mkdirP(mcfg.cloneDir);
    saveConfig();
}

void setupIsoTag() {
    // Volume label and kernels.cfg label=, so no spaces or quotes
    std::string tag = getUserInput("Enter ISO tag (e.g., default is 2026, blank = 2026): ");
    if (tag.find_first_of(" \t\"'") != std::string::npos) {
        std::cout << COLOR_RED << "The ISO tag cannot contain spaces or quotes." << COLOR_RESET << std::endl;
        pressAnyKey();
    } else { mcfg.isoTag = tag; saveConfig(); }
}

void setupImageFormat() {
    std::cout << COLOR_GREEN << "Choose the system image type:\n"
              << "1) Squashfs (mksquashfs zstd, Setup > zstd Compression Level)\n"
              << "2) Erofs (mkfs.erofs -zzstd -C 65536 --chunksize 65536)" << COLOR_RESET << std::endl;
    std::string f = getUserInput("Enter your choice (1 or 2): ");
    if (f == "1" || f == "2") { mcfg.imageFormat = (f == "2") ? "erofs" : "squashfs"; saveConfig(); }
}

void setupAlpineInstallerTools() {
    // Calamares does not run on Alpine: install what our own installer needs
    std::string cmd = remaster::alpineInstallerToolsCommand(sysDistro());
    std::cout << COLOR_CYAN << "Running: " << cmd << COLOR_RESET << std::endl;
    if (runCommand(cmd) == 0) logSuccess("Installer tools ready - build the ISO now so they are in the clone.");
    else logError("Installing the installer tools failed.");
    pressAnyKey();
}

void setupCalamares() {
    const Distro& d = sysDistro();
    std::cout << COLOR_GREEN << "Choose Calamares configuration for system install:\n"
              << "1) Default configuration (Ext4)\n"
              << "2) Custom configuration with btrfs level 22 compression" << COLOR_RESET << std::endl;
    bool btrfs = getUserInput("Enter your choice (1 or 2): ") == "2";
    bool erofs = false;   // only Arch can build an erofs image
    if (d.family == "archlinux") {
        std::cout << COLOR_GREEN << "Choose Cloned System Image type:\n"
                  << "1) Default Cloned Squashfs System\n"
                  << "2) Erofs Cloned System Image\n"
                  << "(should match Setup > Image Format, currently " << mcfg.imageFormat << ")" << COLOR_RESET << std::endl;
        std::string img = getUserInput("Enter your choice (1 or 2, blank = match Image Format): ");
        erofs = img.empty() ? mcfg.imageFormat == "erofs" : img == "2";
    }
    if (remaster::installCalamares(d, btrfs, erofs)) logSuccess("Calamares installation and configuration completed!");
    else logError("Calamares setup failed.");
    pressAnyKey();
}

// The Setup items for the running distro only (no Arch items on Gentoo, and so on)
std::vector<SetupItem> setupItems() {
    const bool arch = onFamily("archlinux"), gentoo = onFamily("gentoo"), alpine = onFamily("alpine");
    std::vector<SetupItem> items;
    items.push_back({"Install Needed Tools", setupInstallTools});
    if (!arch) items.push_back({"Download Bootloaders", [] {
        if (native::ensureBootloaders()) logSuccess("Bootloaders ready.");
        else logError("Bootloader download failed.");
        pressAnyKey();
    }});
    items.push_back({"Set ISO Name (blank = auto)", [] { mcfg.isoName = getUserInput("ISO name (e.g. my-backup.iso, blank = auto): "); saveConfig(); }});
    items.push_back({"Set Work Directory", [] { mcfg.workDir = getUserInput("Work directory (blank = " + DEFAULT_WORK_DIR + "): "); saveConfig(); }});
    items.push_back({"Set Output Directory", [] { mcfg.outputDir = getUserInput("Output directory (blank = work directory): "); saveConfig(); }});
    items.push_back({"Set Clone Directory (system is bind-mounted here)", setupCloneDir});
    if (!gentoo) items.push_back({"Set zstd Compression Level", [] {
        int l = atoi(getUserInput("zstd level (1-22, default 3): ").c_str());
        if (l >= 1 && l <= 22) { mcfg.level = l; saveConfig(); }
    }});
    if (arch) items.push_back({"Set Image Format (squashfs / erofs)", setupImageFormat});
    items.push_back({"Toggle RAM Mode Boot Entry", [] { mcfg.ramMode = !mcfg.ramMode; saveConfig(); }});
    if (arch) items.push_back({"Set ISO Tag (volume label, default 2026)", setupIsoTag});
    if (alpine) items.push_back({"Alpine Installer Tools (to install the clone back to disk)", setupAlpineInstallerTools});
    else if (sysDistro().supported()) items.push_back({"Install Calamares (ext4 / btrfs zstd:22)", setupCalamares});
    return items;
}

void showSetupMenu() {
    std::vector<SetupItem> items = setupItems();
    std::vector<std::string> labels;
    for (const auto& i : items) labels.push_back(i.label);
    labels.push_back("Back to Main Menu");

    int selected = 0;
    while (true) {
        int pick = pickFromMenu("setup menu:", labels, selected);
        if (pick == static_cast<int>(items.size())) return;
        items[pick].run();
    }
}

// ---------------------------------------------------------------------------
// Alpine: install the cloned system from the live ISO back onto a disk (instead of Calamares)
// ---------------------------------------------------------------------------
void installClonedSystem() {
    clearScreen();
    std::cout << COLOR_CYAN << "Install Cloned System To Disk" << COLOR_RESET << "\n" << COLOR_GREEN
              << "Puts this cloned system (users, settings, /home) onto a disk - not a fresh Alpine install.\n"
              << "Run it from the live ISO." << COLOR_RESET << "\n\n";
    if (!remaster::alpineInstallerReady()) {
        std::cerr << COLOR_RED << "The installer tools are missing. On the original system run Setup > Alpine Installer Tools,\n"
                  << "then build the ISO again." << COLOR_RESET << std::endl;
        pressAnyKey();
        return;
    }

    std::cout << COLOR_CYAN << "Available drives:" << COLOR_RESET << std::endl;
    if (commandExists("lsblk")) runCommand("lsblk -d -o NAME,SIZE,MODEL | grep -v loop");
    else runCommand("grep -vE ' (loop|ram)[0-9]' /proc/partitions");
    std::string disk = getUserInput("Enter target drive (e.g., /dev/sda): ");
    if (!startsWith(disk, "/dev/")) {
        std::cerr << COLOR_RED << "Invalid drive!" << COLOR_RESET << std::endl;
        pressAnyKey();
        return;
    }

    std::cout << COLOR_GREEN << "Choose the filesystem:\n"
              << "1) Ext4\n"
              << "2) Btrfs with level 22 compression (zstd:22)\n"
              << "3) Btrfs without compression" << COLOR_RESET << std::endl;
    std::string f = getUserInput("Enter your choice (1, 2 or 3): ");
    std::string fs = (f == "2" || f == "3") ? "btrfs" : "ext4";
    bool compress = f == "2";

    std::cout << COLOR_RED << "\nWARNING: This will erase EVERYTHING on " << disk << "!" << COLOR_RESET << std::endl;
    std::string confirm = getUserInput("Type YES to install the cloned system on " + disk + ": ");
    if (confirm != "YES") { std::cout << COLOR_CYAN << "Operation cancelled." << COLOR_RESET << std::endl; pressAnyKey(); return; }

    if (remaster::installClonedSystemAlpine(disk, fs, compress)) logSuccess("Cloned system installed on " + disk + ". Remove the live medium and reboot.");
    else logError("Installing the cloned system failed. See the messages above.");
    pressAnyKey();
}

// ---------------------------------------------------------------------------
// Main menu
// ---------------------------------------------------------------------------
std::vector<std::string> mainMenuItems() {
    return {
        "Guide",
        "Setup",
        "Detected System And Build Steps",
        "Create Bootable ISO",
        "Check Disk Usage",
        "Install ISO To USB",
        onFamily("alpine") ? "Install Cloned System To Disk" : "Launch Calamares",
        "Exit"
    };
}

// ---------------------------------------------------------------------------
// Window size: every menu screen (banner, status, items) and the guide must fit without scrolling
// ---------------------------------------------------------------------------
const int UI_COLUMNS = 104;   // widest lines: the banner (86) and the guide (~98)

int countLines(const std::string& s) { return static_cast<int>(std::count(s.begin(), s.end(), '\n')); }

int requiredRows() {
    const int header = 8 + 3 + 1 + 2 + 1;              // banner, 3 text lines, clock, df header + line, blank
    int items = std::max(static_cast<int>(mainMenuItems().size()), static_cast<int>(setupItems().size()) + 1);
    int menuScreen = header + countLines(statusBlock()) + 3 + items + 1;   // + title, underline, cursor line
    int guideScreen = 2 + countLines(guideText()) + 2;                     // + heading, "Press any key"
    return std::max(menuScreen, guideScreen);
}

bool terminalFits(int rows, int cols) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != 0 || w.ws_row == 0) return true;   // not a terminal: nothing to do
    return w.ws_row >= rows && w.ws_col >= cols;
}

// Asks the terminal to resize itself (xterm/VTE/kitty/alacritty and most others honour this)
bool resizeTerminal(int rows, int cols) {
    if (terminalFits(rows, cols)) return true;
    std::cout << "\033[8;" << rows << ";" << cols << "t" << std::flush;
    for (int i = 0; i < 10 && !terminalFits(rows, cols); i++) usleep(50000);
    return terminalFits(rows, cols);
}

void showMainMenu() {
    std::vector<std::string> items = mainMenuItems();
    const bool alpine = onFamily("alpine");
    int selected = 0;
    while (true) {
        int pick = pickFromMenu(APP_NAME + " main menu:", items, selected);
        switch (pick) {
            case 0: showGuide(); break;
            case 1: showSetupMenu(); break;
            case 2: showSystemInfo(); break;
            case 3: createISO(); break;
            case 4: runCommand("df -h"); pressAnyKey(); break;
            case 5: installISOToUSB(); break;
            case 6:
                if (alpine) installClonedSystem();
                else { runCommand("calamares"); pressAnyKey(); }
                break;
            case 7: return;
        }
    }
}

#endif
