#ifndef RESOURCES_H
#define RESOURCES_H

#include "common.h"

// The claudemods zips from cmiadvanced, embedded into the binary by the assembler (.incbin), so
// building needs only g++ - no separate ld step (KDE neon / Ubuntu ship no plain "ld" without
// the binutils package). Paths are relative to dev-release/, where make runs the compiler.
//   build-image-arch-img.zip  Arch ISO tree (GRUB BIOS/UEFI, theme, ucode) + mkinitcpio hooks
//   calamares-files.zip       Calamares packages and config (ext4 / btrfs)
//   claudemods.zip            Calamares branding
#define CDB_EMBED_ZIP(sym, file)            \
    ".section .rodata\n"                    \
    ".global " sym "_start\n"               \
    ".balign 16\n"                          \
    sym "_start:\n"                         \
    ".incbin \"" file "\"\n"                \
    ".global " sym "_end\n"                 \
    sym "_end:\n"                           \
    ".byte 0\n"                             \
    ".previous\n"

asm(CDB_EMBED_ZIP("_binary_build_image_arch_img_zip", "resources/build-image-arch-img.zip")
    CDB_EMBED_ZIP("_binary_calamares_files_zip", "resources/calamares-files.zip")
    CDB_EMBED_ZIP("_binary_claudemods_zip", "resources/claudemods.zip"));

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
    // claudemods.zip is bzip2-compressed and some distros' unzip is built without bzip2,
    // so fall back to extractors that have it
    if (rc != 0) rc = runCommand("python3 -c 'import sys,zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' " +
                                 shq(zipPath) + " " + shq(destDir));
    if (rc != 0) rc = runCommand("bsdtar -xf " + shq(zipPath) + " -C " + shq(destDir));
    if (rc != 0) rc = runCommand("7z x -y -bso0 -o" + shq(destDir) + " " + shq(zipPath));
    unlink(zipPath.c_str());
    if (rc != 0) logError("unzip of " + name + " failed");
    return rc == 0;
}

}  // namespace resources

#endif
