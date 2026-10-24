#ifndef RESOURCES_H
#define RESOURCES_H

#include "common.h"

// The claudemods zips from cmiadvanced, linked into the binary by the Makefile (ld -r -b binary):
//   build-image-arch-img.zip  Arch ISO tree (GRUB BIOS/UEFI, theme, ucode) + mkinitcpio hooks
//   calamares-files.zip       Calamares packages and config (ext4 / btrfs)
//   claudemods.zip            Calamares branding
extern "C" {
extern const char _binary_build_image_arch_img_zip_start[];
extern const char _binary_build_image_arch_img_zip_end[];
extern const char _binary_calamares_files_zip_start[];
extern const char _binary_calamares_files_zip_end[];
extern const char _binary_claudemods_zip_start[];
extern const char _binary_claudemods_zip_end[];
}

namespace resources {

enum class Zip { BuildImageArch, CalamaresFiles, Claudemods };

// Writes the embedded zip to disk and unzips it into destDir
inline bool extract(Zip z, const std::string& destDir) {
    const char* begin = nullptr;
    const char* end = nullptr;
    std::string name;
    switch (z) {
        case Zip::BuildImageArch: begin = _binary_build_image_arch_img_zip_start; end = _binary_build_image_arch_img_zip_end; name = "build-image-arch-img.zip"; break;
        case Zip::CalamaresFiles: begin = _binary_calamares_files_zip_start; end = _binary_calamares_files_zip_end; name = "calamares-files.zip"; break;
        case Zip::Claudemods:     begin = _binary_claudemods_zip_start; end = _binary_claudemods_zip_end; name = "claudemods.zip"; break;
    }

    mkdirP(destDir);
    std::string zipPath = destDir + "/" + name;
    if (!writeFile(zipPath, std::string(begin, end - begin))) { logError("cannot write " + zipPath); return false; }
    int rc = runCommand("unzip -o -q " + shq(zipPath) + " -d " + shq(destDir));
    unlink(zipPath.c_str());
    if (rc != 0) logError("unzip of " + name + " failed");
    return rc == 0;
}

}  // namespace resources

#endif
