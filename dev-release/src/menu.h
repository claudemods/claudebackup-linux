#ifndef MENU_H
#define MENU_H

#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <termios.h>
#include <fcntl.h>
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
    std::string algo = "zstd";
    int level = 3;
    bool ramMode = true;
    std::string isoTag;        // Arch volume label, empty = 2026
    std::string cloneDir;      // <parent>/clone_system_temp, "/" is bound here; empty = default
    std::string imageFormat = "squashfs";   // Arch only: squashfs | erofs
};
MenuConfig mcfg;

std::string defaultCloneDir() { return "/home/" + USERNAME + "/clone_system_temp"; }

std::string configDirPath() { return "/home/" + USERNAME + "/.config/" + APP_BIN; }
std::string configFilePath() { return configDirPath() + "/configuration.txt"; }

void saveConfig() {
    mkdirP(configDirPath());
    std::ofstream f(configFilePath());
    if (!f.is_open()) { std::cerr << COLOR_RED << "Failed to save configuration to " << configFilePath() << COLOR_RESET << std::endl; return; }
    f << "isoName=" << mcfg.isoName << "\n";
    f << "workDir=" << mcfg.workDir << "\n";
    f << "outputDir=" << mcfg.outputDir << "\n";
    f << "algo=" << mcfg.algo << "\n";
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
        else if (k == "algo") mcfg.algo = v;
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
    o.algo = mcfg.algo;
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

std::string statusBlock() {
    static Distro d = detectDistro();
    bool toolsOk = remaster::missingTools(d).empty();

    std::string s = COLOR_CYAN + "Current Configuration:" + COLOR_RESET + "\n";
    s += checkLine(d.supported(), "Distro: " + d.prettyName + (d.supported() ? " (family: " + d.family + ")" : " (not supported)"));
    s += checkLine(toolsOk, "Needed Tools");
    s += checkLine(remaster::bootFilesReady(d), d.family == "archlinux" ? "Boot Files: embedded claudemods zip" : "Bootloaders");
    if (d.family == "archlinux") {
        s += checkLine(!mcfg.isoTag.empty(), "ISO Tag: " + (mcfg.isoTag.empty() ? std::string("2026 (default)") : mcfg.isoTag));
        s += checkLine(true, "Image Format: " + mcfg.imageFormat + (mcfg.imageFormat == "erofs" ? " (mkfs.erofs zstd)" : ""));
    }
    if (d.supported()) s += checkLine(commandExists("calamares"), "Calamares");
    s += checkLine(!mcfg.isoName.empty(), "ISO Name: " + (mcfg.isoName.empty() ? std::string("Auto") : mcfg.isoName));
    s += checkLine(true, "Work Directory: " + (mcfg.workDir.empty() ? DEFAULT_WORK_DIR : mcfg.workDir));
    s += checkLine(true, "Output Directory: " + (mcfg.outputDir.empty() ? std::string("Work directory") : mcfg.outputDir));
    s += checkLine(!mcfg.cloneDir.empty(), "Clone Directory: " + (mcfg.cloneDir.empty() ? "Not set (uses " + defaultCloneDir() + ")" : mcfg.cloneDir));
    s += checkLine(true, "Compression: " + mcfg.algo + " level " + std::to_string(mcfg.level));
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
    menuBuffer += COLOR_RED + "Back up your running system as a bootable live ISO - Debian, Arch, Fedora, openSUSE, Alpine, Gentoo" + COLOR_RESET + "\n";
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
void showGuide() {
    clearScreen();
    std::cout << COLOR_CYAN << APP_NAME << " " << APP_VERSION << " - guide" << COLOR_RESET << "\n\n" << COLOR_GREEN <<
        "A full clone: your own users, passwords, settings and /home are copied into the ISO\n"
        "exactly as they are. No live account is created and nothing is added to the clone.\n"
        "The squashfs is made from the whole system bind-mounted (mount --bind /) at your Clone\n"
        "Directory (all distros). A bind of '/' only shows the root filesystem: separate partitions\n"
        "or btrfs subvolumes (e.g. @home, /boot) are NOT included.\n"
        "Every box under 'Current Configuration' shows what is ready ([x]) or still to do ([ ]).\n\n"
        "1. Setup > Install Needed Tools      squashfs-tools, xorriso, mtools, dosfstools, curl\n"
        "                                     plus your distro's live-boot packages\n"
        "                                     (Arch: unzip, erofs-utils, mkinitcpio, device-mapper, gnupg)\n"
        "                                     (Gentoo: emerge ... sys-kernel/dracut)\n"
        "2. Setup > Download Bootloaders      one-off download of the BIOS/UEFI boot files\n"
        "                                     (Arch: not needed, the claudemods zip is built in)\n"
        "3. Setup > ISO Name, Work Directory, Output Directory, Clone Directory\n"
        "     ISO name blank = auto. Output directory blank = the work directory.\n"
        "     Clone directory: parent folder, the system is bound at <parent>/clone_system_temp.\n"
        "4. Setup > Compression and Level     zstd (1-22, default 3), xz or gzip\n"
        "   Setup > Image Format (Arch)       squashfs, or erofs (mkfs.erofs -zzstd -C 65536\n"
        "                                     --chunksize 65536, kernel 6.10+ for erofs zstd)\n"
        "5. Setup > RAM Mode                  adds a 'toram' boot entry (Arch: copytoram=y)\n"
        "6. Setup > Install Calamares         ext4 or btrfs zstd:22, claudemods branding;\n"
        "                                     Arch: the claudemods zip packages, squashfs or erofs\n"
        "                                     (match Image Format). Other distros: your repo's\n"
        "                                     calamares package + a full-clone config (no users\n"
        "                                     page, GRUB, initramfs-tools / dracut / mkinitfs).\n"
        "                                     Do this BEFORE building so the ISO contains it\n"
        "7. Detected System And Build Steps   check your distro says 'Support: yes'\n"
        "8. Create Bootable ISO               mount, bootloaders, EFI image, boot menus,\n"
        "                                     initramfs, kernel, squashfs, ISO\n"
        "                                     (everything is unmounted at the end, even on Ctrl+C)\n"
        "9. Install ISO To USB / Launch Calamares\n"
        "     USB erases the whole target drive. Test the ISO in a VM (UEFI) first.\n\n"
        "Arch family (Arch, CachyOS, ...): the ISO is built from the embedded claudemods zip -\n"
        "GRUB BIOS/UEFI tree, LiveOS/rootfs.img and the btrfs/ext4 mkinitcpio hooks - with the\n"
        "original cmiadvanced mksquashfs and xorriso commands. Setup > Set ISO Tag sets the\n"
        "volume label the live system boots from (default 2026).\n"
        "Gentoo: dracut dmsquash-live; your kernel needs CONFIG_SQUASHFS, CONFIG_OVERLAY_FS,\n"
        "CONFIG_ISO9660_FS and CONFIG_BLK_DEV_LOOP (=m or =y) - the build tells you if one is off.\n\n"
        "Free space needed is roughly twice the size of the finished ISO.\n"
        "Fedora: run 'sudo setenforce 0' before building.\n"
        "Log: " << LOG_FILE << COLOR_RESET << "\n";
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
    int i = 1;
    for (const auto& s : plan::build(c))
        std::cout << "  " << (i < 10 ? " " : "") << i++ << ". " << s.name << " - " << s.description << "\n";
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
    runCommand("lsblk -d -o NAME,SIZE,MODEL | grep -v loop");
    std::string target = getUserInput("Enter target drive (e.g., /dev/sda): ");
    if (!startsWith(target, "/dev/")) {
        std::cerr << COLOR_RED << "Invalid drive!" << COLOR_RESET << std::endl;
        pressAnyKey();
        return;
    }
    std::cout << COLOR_RED << "\nWARNING: This will overwrite all data on " << target << "!" << COLOR_RESET << std::endl;
    std::string confirm = getUserInput("Are you sure you want to continue? (y/N): ");
    if (confirm != "y" && confirm != "Y") { std::cout << COLOR_CYAN << "Operation cancelled." << COLOR_RESET << std::endl; pressAnyKey(); return; }

    runCommand("dd if=" + shq(iso) + " of=" + shq(target) + " bs=4M status=progress oflag=sync");
    std::cout << COLOR_GREEN << "\nISO written to " << target << COLOR_RESET << std::endl;
    pressAnyKey();
}

// ---------------------------------------------------------------------------
// Setup menu
// ---------------------------------------------------------------------------
void showSetupMenu() {
    std::vector<std::string> items = {
        "Install Needed Tools",
        "Download Bootloaders",
        "Set ISO Name (blank = auto)",
        "Set Work Directory",
        "Set Output Directory",
        "Set Clone Directory (system is bind-mounted here)",
        "Set Compression (zstd / xz / gzip)",
        "Set Compression Level",
        "Toggle RAM Mode Boot Entry",
        "Set ISO Tag (Arch volume label, default 2026)",
        "Set Image Format (Arch: squashfs / erofs)",
        "Install Calamares (ext4 / btrfs zstd:22)",
        "Back to Main Menu"
    };
    int selected = 0;
    while (true) {
        int pick = pickFromMenu("setup menu:", items, selected);
        switch (pick) {
            case 0: {
                std::string cmd = remaster::installToolsCommand(detectDistro());
                if (cmd.empty()) std::cout << COLOR_RED << "Unsupported distro." << COLOR_RESET << std::endl;
                else { std::cout << COLOR_CYAN << "Running: " << cmd << COLOR_RESET << std::endl; runCommand(cmd); }
                pressAnyKey();
                break;
            }
            case 1:
                if (detectDistro().family == "archlinux") logSuccess("Arch uses the boot files built into this binary - nothing to download.");
                else if (native::ensureBootloaders()) logSuccess("Bootloaders ready.");
                else logError("Bootloader download failed.");
                pressAnyKey();
                break;
            case 2: mcfg.isoName = getUserInput("ISO name (e.g. my-backup.iso, blank = auto): "); saveConfig(); break;
            case 3: mcfg.workDir = getUserInput("Work directory (blank = " + DEFAULT_WORK_DIR + "): "); saveConfig(); break;
            case 4: mcfg.outputDir = getUserInput("Output directory (blank = work directory): "); saveConfig(); break;
            case 5: {
                // cmiadvanced setCloneDir: parent folder + /clone_system_temp
                std::string def = "/home/" + USERNAME;
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
                    break;
                }
                mcfg.cloneDir = (parent == "/" ? "" : parent) + "/clone_system_temp";
                mkdirP(mcfg.cloneDir);
                saveConfig();
                break;
            }
            case 6: {
                std::string a = getUserInput("Algorithm (zstd/xz/gzip/lz4): ");
                if (a == "zstd" || a == "xz" || a == "gzip" || a == "lz4") { mcfg.algo = a; saveConfig(); }
                else { std::cout << COLOR_RED << "Unknown algorithm." << COLOR_RESET << std::endl; pressAnyKey(); }
                break;
            }
            case 7: {
                int l = atoi(getUserInput("Level (zstd 1-22, default 3): ").c_str());
                if (l > 0) { mcfg.level = l; saveConfig(); }
                break;
            }
            case 8: mcfg.ramMode = !mcfg.ramMode; saveConfig(); break;
            case 9: {
                // Volume label and kernels.cfg label=, so no spaces or quotes
                std::string tag = getUserInput("Enter ISO tag (e.g., default is 2026, blank = 2026): ");
                if (tag.find_first_of(" \t\"'") != std::string::npos) {
                    std::cout << COLOR_RED << "The ISO tag cannot contain spaces or quotes." << COLOR_RESET << std::endl;
                    pressAnyKey();
                } else { mcfg.isoTag = tag; saveConfig(); }
                break;
            }
            case 10: {
                if (detectDistro().family != "archlinux") {
                    std::cout << COLOR_RED << "Only the Arch family can build an erofs image; other distros use squashfs." << COLOR_RESET << std::endl;
                    pressAnyKey();
                    break;
                }
                std::cout << COLOR_GREEN << "Choose the system image type:\n"
                          << "1) Squashfs (mksquashfs, Setup > Compression and Level)\n"
                          << "2) Erofs (mkfs.erofs -zzstd -C 65536 --chunksize 65536)" << COLOR_RESET << std::endl;
                std::string f = getUserInput("Enter your choice (1 or 2): ");
                if (f == "1" || f == "2") { mcfg.imageFormat = (f == "2") ? "erofs" : "squashfs"; saveConfig(); }
                break;
            }
            case 11: {
                Distro d = detectDistro();
                if (!d.supported()) {
                    std::cout << COLOR_RED << "Unsupported distro." << COLOR_RESET << std::endl;
                    pressAnyKey();
                    break;
                }
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
                break;
            }
            case 12: return;
        }
    }
}

// ---------------------------------------------------------------------------
// Main menu
// ---------------------------------------------------------------------------
void showMainMenu() {
    std::vector<std::string> items = {
        "Guide",
        "Setup",
        "Detected System And Build Steps",
        "Create Bootable ISO",
        "Check Disk Usage",
        "Install ISO To USB",
        "Launch Calamares",
        "Exit"
    };
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
            case 6: runCommand("calamares"); pressAnyKey(); break;
            case 7: return;
        }
    }
}

#endif
