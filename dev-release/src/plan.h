#ifndef PLAN_H
#define PLAN_H

#include <functional>
#include "families.h"
#include "scripts.h"
#include "native.h"

// The remaster plan: the ordered steps of penguins-eggs coa/brain.d/base.yaml.tmpl, built in C++
// instead of rendered from YAML. Family differences come from families.h.
struct Step {
    std::string name;
    std::string description;
    std::function<bool()> run;
};

namespace plan {

inline std::string scriptPath(const std::string& name) { return STAGING_DIR + "/scripts/" + name; }

// Write a file inside liveroot (the "template" module)
inline bool writeLiveFile(const Context& c, const std::string& dest, const std::string& content, mode_t mode) {
    std::string full = c.liveRoot + dest;
    mkdirP(dirName(full));
    return writeFile(full, content, mode);
}

inline std::vector<Step> build(const Context& c) {
    std::vector<Step> steps;
    const std::string iso = c.isoDir;
    const std::string live = c.liveRoot;
    const bool userMode = (c.mode != "clone");

    // 1. Mount
    steps.push_back({"mount-logic", "Chroot environment and mount point preparation",
        [&c]() { return native::bootstrapLiveroot(c); }});

    // 2. Bootloader copy
    steps.push_back({"bootloader-copy", "Copy the bootloader assets (BIOS/UEFI)", [&c, iso]() {
        mkdirP(STAGING_DIR + "/scripts");
        return native::runShell(scripts::render(scripts::BOOTLOADER_COPY), "", {STAGING_DIR + "/bootloaders", iso}) == 0;
    }});

    // 3. EFI image
    steps.push_back({"generate-efi.img", "Generating a FAT image for EFI", [iso]() {
        std::string img = shq(iso + "/EFI/BOOT/efi.img");
        std::string cmd =
            "dd if=/dev/zero of=" + img + " bs=1M count=12\n"
            "mkfs.vfat " + img + "\n"
            "mmd -i " + img + " ::/EFI\n"
            "mmd -i " + img + " ::/EFI/BOOT\n"
            "for f in " + shq(iso + "/EFI/BOOT") + "/*.EFI " + shq(iso + "/EFI/BOOT") + "/*.efi; do\n"
            "    if [ -f \"$f\" ]; then\n"
            "        mcopy -i " + img + " \"$f\" \"::/EFI/BOOT/$(basename \"$f\")\"\n"
            "    fi\n"
            "done\n";
        return native::runShell(cmd, "") == 0;
    }});

    // 4. Boot assets
    steps.push_back({"boot-assets", "Copy the splash screen and configure the GRUB/ISOLINUX fonts", [iso]() {
        std::string cmd =
            "DEFAULT_BRANDING=" + CONFIG_DIR + "/branding.default/livecd\n"
            "LIVECD_BRANDING=" + CONFIG_DIR + "/branding/livecd\n"
            "[ -f \"$LIVECD_BRANDING/splash.png\" ] || LIVECD_BRANDING=$DEFAULT_BRANDING\n"
            "[ -f \"$LIVECD_BRANDING/splash.png\" ] && cp -f \"$LIVECD_BRANDING/splash.png\" " + shq(iso + "/boot/grub/splash.png") + "\n"
            "[ -f \"$LIVECD_BRANDING/splash.png\" ] && cp -f \"$LIVECD_BRANDING/splash.png\" " + shq(iso + "/isolinux/splash.png") + "\n"
            "[ -f \"$LIVECD_BRANDING/grub.theme.cfg\" ] && cp -f \"$LIVECD_BRANDING/grub.theme.cfg\" " + shq(iso + "/boot/grub/theme.cfg") + "\n"
            "[ -f \"$LIVECD_BRANDING/isolinux.theme.cfg\" ] && cp -f \"$LIVECD_BRANDING/isolinux.theme.cfg\" " + shq(iso + "/isolinux/isolinux.theme.cfg") + "\n"
            "if [ -f /usr/share/grub/unicode.pf2 ]; then\n"
            "    cp -f /usr/share/grub/unicode.pf2 " + shq(iso + "/boot/grub/font.pf2") + "\n"
            "elif [ -f \"$LIVECD_BRANDING/unicode.pf2\" ]; then\n"
            "    cp -f \"$LIVECD_BRANDING/unicode.pf2\" " + shq(iso + "/boot/grub/font.pf2") + "\n"
            "fi\n"
            "true\n";
        return native::runShell(cmd, "") == 0;
    }});

    // 5. Boot menus
    steps.push_back({"generate-boot-menus", "Dynamic generation of GRUB, ISOLINUX and EFI trampoline", [&c, iso]() {
        std::string params = families::bootParams(c);
        return native::runShell(scripts::render(scripts::GENERATE_MENUS), "", {iso, params, c.ramModeEnabled ? "1" : "0"}) == 0;
    }});

    // 5.1 EFI grub.cfg into efi.img
    steps.push_back({"inject-efi-grubcfg", "Copy the EFI trampoline grub.cfg into efi.img", [iso]() {
        std::string img = shq(iso + "/EFI/BOOT/efi.img"), cfg = shq(iso + "/EFI/BOOT/grub.cfg");
        return native::runShell("if [ -f " + img + " ] && [ -f " + cfg + " ]; then\n    mcopy -i " + img + " -o " + cfg + " ::/EFI/BOOT/grub.cfg\nfi\n", "") == 0;
    }});

    // 6. Identity (clone keeps the real users)
    if (userMode) {
        steps.push_back({"identity", "Live user injection and password sanitization",
            [&c]() { return native::injectLiveUser(c); }});
    }

    // 7. Sanitize system files
    steps.push_back({"sanitize-system-files", "Sanitize fstab, crypttab and basic system config for the live system", [live]() {
        std::string r = shq(live);
        std::string cmd =
            "echo '# UNCONFIGURED FSTAB' > " + r + "/etc/fstab\n"
            "echo '# UNCONFIGURED CRYPTTAB' > " + r + "/etc/crypttab\n"
            "chmod 644 " + r + "/etc/fstab " + r + "/etc/crypttab\n"
            "if [ -d " + r + "/etc/initramfs-tools/conf.d ]; then\n    echo 'RESUME=none' > " + r + "/etc/initramfs-tools/conf.d/resume\nfi\n"
            "if [ -f " + r + "/etc/initramfs-tools/initramfs.conf ]; then\n    sed -i 's/^RESUME=.*/RESUME=none/' " + r + "/etc/initramfs-tools/initramfs.conf\nfi\n"
            "if [ ! -e " + r + "/etc/localtime ] && [ ! -L " + r + "/etc/localtime ]; then\n    ln -sf /usr/share/zoneinfo/UTC " + r + "/etc/localtime\nfi\n"
            "if [ -f " + r + "/etc/default/locale ]; then\n    :\nelif [ ! -f " + r + "/etc/locale.conf ]; then\n    echo 'LANG=C.UTF-8' > " + r + "/etc/locale.conf\nfi\n"
            "if [ ! -f " + r + "/etc/hostname ]; then\n    echo 'live-system' > " + r + "/etc/hostname\nfi\n";
        return native::runShell(cmd, "") == 0;
    }});

    // 8. Initramfs (chrooted)
    steps.push_back({"initramfs", "Live environment-specific initramfs generation", [&c, live]() {
        return native::runShell(families::initramfsCmd(c), live) == 0;
    }});
    steps.push_back({"copy-kernel-initrd", "Copy kernel and initramfs to the live ISO directory", [&c]() {
        return native::runShell(families::copyKernelCmd(c), "") == 0;
    }});

    // 10-11. sudoers and polkit
    steps.push_back({"conf-live-sudoers", "Configure sudoers for the live user", [&c]() {
        return writeLiveFile(c, "/etc/sudoers.d/00-live",
            c.liveUser() + " ALL=(ALL) NOPASSWD:ALL\n%wheel ALL=(ALL) NOPASSWD:ALL\n%sudo ALL=(ALL) NOPASSWD:ALL\n", 0440);
    }});

    steps.push_back({"polkit-policy", "Polkit policy for the installer launcher", [&c]() {
        std::string xml =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE policyconfig PUBLIC\n"
            "\"-//freedesktop//DTD PolicyKit Policy Configuration 1.0//EN\"\n"
            "\"http://www.freedesktop.org/standards/PolicyKit/1/policyconfig.dtd\">\n"
            "<policyconfig>\n"
            "  <vendor>claudemods</vendor>\n"
            "  <action id=\"net.claudemods.claudebackup.run\">\n"
            "    <description>Run System Installer</description>\n"
            "    <message>Authentication is required to run the system installer</message>\n"
            "    <defaults>\n"
            "      <allow_any>no</allow_any>\n"
            "      <allow_inactive>no</allow_inactive>\n"
            "      <allow_active>yes</allow_active>\n"
            "    </defaults>\n"
            "    <annotate key=\"org.freedesktop.policykit.exec.path\">/usr/bin/" + APP_BIN + "</annotate>\n"
            "    <annotate key=\"org.freedesktop.policykit.exec.allow_gui\">true</annotate>\n"
            "  </action>\n"
            "</policyconfig>\n";
        return writeLiveFile(c, "/usr/share/polkit-1/actions/net.claudemods.claudebackup.policy", xml, 0644);
    }});

    steps.push_back({"polkit-rules", "Polkit rules: require authentication for cloning personal data", [&c]() {
        std::string rules =
            "/* claudebackup linux: require authentication for system cloning. */\n"
            "polkit.addRule(function(action, subject) {\n"
            "    if (action.id === \"net.claudemods.claudebackup.run\" ||\n"
            "        (action.id === \"org.freedesktop.policykit.exec\" && action.lookup(\"program\") === \"/usr/bin/" + APP_BIN + "\")) {\n"
            "        var cmd = action.lookup(\"command_line\");\n"
            "        if (cmd && (cmd.indexOf(\"--clone\") !== -1)) {\n"
            "            return polkit.Result.AUTH_ADMIN;\n"
            "        }\n"
            "    }\n"
            "});\n";
        return writeLiveFile(c, "/usr/share/polkit-1/rules.d/49-claudebackup-linux.rules", rules, 0644);
    }});

    // 12. Desktop launcher
    steps.push_back({"create-live-launcher", "Add universal desktop launcher", [&c]() {
        if (!c.hasCalamares) return true;   // launcher only makes sense when Calamares is installed
        std::string d = scripts::LAUNCHER_DESKTOP;
        d = replaceAll(d, "@@EXEC@@", "pkexec calamares");
        d = replaceAll(d, "@@TERMINAL@@", "false");
        return writeLiveFile(c, "/usr/share/applications/install-system.desktop", d, 0755);
    }});

    // 13. Trust agent
    steps.push_back({"trust-agent-script", "Copy dynamic trust script for the desktop launcher", [&c]() {
        return writeLiveFile(c, "/usr/local/bin/trust-desktop", scripts::TRUST_DESKTOP, 0755);
    }});
    steps.push_back({"trust-agent-autostart", "Enable XDG Autostart for the trust agent", [&c]() {
        return writeLiveFile(c, "/etc/xdg/autostart/trust-installer.desktop",
            "[Desktop Entry]\nType=Application\nName=Trust Installer\nExec=/usr/local/bin/trust-desktop\nHidden=false\nNoDisplay=true\n", 0644);
    }});

    // 14. Autologin
    steps.push_back({"autologin-gui", "Set up autologin for the live user", [&c]() {
        native::configureAutologin(c, c.liveUser());
        return true;
    }});

    // 16. Squashfs
    steps.push_back({"mksquashfs", "filesystem.squashfs compression", [&c]() { return native::runMksquashfs(c); }});

    // 17. Distro hook
    steps.push_back({"distro_hook_squashfs", "Hard link filesystem.squashfs where the distro expects it", [&c]() {
        return native::runShell(families::hookSquashfsCmd(c), "") == 0;
    }});

    // 18. .disk metadata, ISO, optional symlink
    steps.push_back({"xorriso", "ISO packaging", [&c]() {
        native::writeDotDisk(c);
        if (!native::runXorriso(c)) return false;
        if (!c.symlinkIsoPath.empty())
            runCommand("ln -sf " + shq(c.isoPath) + " " + shq(c.symlinkIsoPath));
        return true;
    }});

    return steps;
}

}  // namespace plan

#endif
