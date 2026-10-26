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

// Splash, theme and font files the boot menus refer to (non-Arch families)
inline std::string bootAssetsCmd(const std::string& iso) {
    return
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
}

// Writes grub.cfg, isolinux.cfg and the EFI trampoline into iso (non-Arch families)
inline bool generateMenus(const Context& c, const std::string& iso) {
    return native::runShell(scripts::render(scripts::GENERATE_MENUS), "", {iso, families::bootParams(c), c.ramModeEnabled ? "1" : "0"}) == 0;
}

// Files of Setup > Edit Configs that are copied over the generated ISO tree: name -> path inside the ISO
struct EditTarget { const char* name; const char* isoPath; bool arch; };
inline const std::vector<EditTarget>& editTargets() {
    static const std::vector<EditTarget> t = {
        {"grub.cfg",     "/boot/grub/grub.cfg",       true},
        {"kernels.cfg",  "/boot/grub/kernels.cfg",    true},    // Arch only (boot text)
        {"isolinux.cfg", "/isolinux/isolinux.cfg",    false},   // not Arch (boot text)
    };
    return t;
}
inline bool editApplies(const EditTarget& e, const Context& c) {
    if (std::string(e.name) == "grub.cfg") return true;
    return e.arch == families::isArch(c);
}

// Puts the user's edited boot files over the generated ones
inline bool applyCustomEdits(const Context& c) {
    int n = 0;
    for (const auto& e : editTargets()) {
        if (!editApplies(e, c)) continue;
        std::string src = families::customFile(c, e.name);
        if (src.empty()) continue;
        std::string dst = c.isoDir + e.isoPath;
        mkdirP(dirName(dst));
        if (!writeFile(dst, readFile(src))) { logError(std::string("cannot write ") + dst); return false; }
        logNormal(std::string("Using your edited ") + e.name);
        n++;
    }
    if (n == 0) logNormal("No edited boot files.");
    return true;
}

inline std::vector<Step> build(const Context& c) {
    std::vector<Step> steps;
    const std::string iso = c.isoDir;
    const std::string live = c.liveRoot;

    // 1. Mount
    steps.push_back({"mount-logic", "Chroot environment and mount point preparation",
        [&c]() { return native::bootstrapLiveroot(c); }});

    if (families::isArch(c)) {
        // 2-5 (Arch). The claudemods zip tree: GRUB BIOS/UEFI, efi.img, theme
        steps.push_back({"arch-boot-files", "Copy the claudemods ISO tree (GRUB BIOS/UEFI, theme) from the embedded zip", [&c]() {
            return native::runShell(families::archBootFilesCmd(c), "") == 0;
        }});
        steps.push_back({"generate-boot-menus", "Write the GRUB boot entries (boot/grub/kernels.cfg)", [&c, iso]() {
            return writeFile(iso + "/boot/grub/kernels.cfg", families::archKernelsCfg(c));
        }});
    } else {   // Debian, Fedora, openSUSE, Alpine: unchanged steps 2-5.1

    // 2. Bootloader copy
    steps.push_back({"bootloader-copy", "Copy the bootloader assets (BIOS/UEFI)", [&c, iso]() {
        mkdirP(STAGING_DIR + "/scripts");
        return native::runShell(scripts::render(scripts::BOOTLOADER_COPY), "", {BOOTLOADER_DIR, iso}) == 0;
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
        return native::runShell(bootAssetsCmd(iso), "") == 0;
    }});

    // 5. Boot menus
    steps.push_back({"generate-boot-menus", "Dynamic generation of GRUB, ISOLINUX and EFI trampoline", [&c, iso]() {
        return generateMenus(c, iso);
    }});

    // 5.1 EFI grub.cfg into efi.img
    steps.push_back({"inject-efi-grubcfg", "Copy the EFI trampoline grub.cfg into efi.img", [iso]() {
        std::string img = shq(iso + "/EFI/BOOT/efi.img"), cfg = shq(iso + "/EFI/BOOT/grub.cfg");
        return native::runShell("if [ -f " + img + " ] && [ -f " + cfg + " ]; then\n    mcopy -i " + img + " -o " + cfg + " ::/EFI/BOOT/grub.cfg\nfi\n", "") == 0;
    }});
    }   // end non-Arch steps

    // 5.2 The user's edited boot files (Setup > Edit Configs) go over the generated ones
    if (!c.customDir.empty())
        steps.push_back({"custom-edits", "Use the boot files edited in Setup > Edit Configs", [&c]() { return applyCustomEdits(c); }});

    // 8. Initramfs (chrooted; Arch first stages its hooks from the host)
    steps.push_back({"initramfs", "Live environment-specific initramfs generation", [&c, live]() {
        std::string prep = families::initramfsPrepCmd(c);
        if (!prep.empty() && native::runShell(prep, "") != 0) return false;
        return native::runShell(families::initramfsCmd(c), live) == 0;
    }});
    steps.push_back({"copy-kernel-initrd", "Copy kernel and initramfs to the live ISO directory", [&c]() {
        return native::runShell(families::copyKernelCmd(c), "") == 0;
    }});

    // 15.5 Package caches stay out of the image
    if (c.cleanCache)
        steps.push_back({"clean-package-cache", "Leave package caches and temp files out of the image", [&c]() { return native::reportPackageCaches(c); }});

    // 16. Squashfs
    steps.push_back({"mksquashfs", baseName(families::squashfsDest(c)) + (c.settings.imageFormat == "erofs" ? " (erofs)" : "") + " compression", [&c]() {
        return native::runMksquashfs(c, families::squashfsDest(c));
    }});

    // 17. Distro hook
    steps.push_back({"distro_hook_squashfs", families::isArch(c) ? "Write the rootfs.img checksum" : "Hard link filesystem.squashfs where the distro expects it", [&c]() {
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
