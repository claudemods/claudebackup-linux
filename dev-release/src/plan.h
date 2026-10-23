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

inline std::vector<Step> build(const Context& c) {
    std::vector<Step> steps;
    const std::string iso = c.isoDir;
    const std::string live = c.liveRoot;

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

    // 8. Initramfs (chrooted)
    steps.push_back({"initramfs", "Live environment-specific initramfs generation", [&c, live]() {
        return native::runShell(families::initramfsCmd(c), live) == 0;
    }});
    steps.push_back({"copy-kernel-initrd", "Copy kernel and initramfs to the live ISO directory", [&c]() {
        return native::runShell(families::copyKernelCmd(c), "") == 0;
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
