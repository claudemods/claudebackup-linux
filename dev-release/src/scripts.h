#ifndef SCRIPTS_H
#define SCRIPTS_H

#include "common.h"

// Helper scripts embedded in the binary (ported from penguins-eggs configs/scripts).
// They are written to STAGING_DIR/scripts at run time, so no files need installing.
namespace scripts {

inline const char* BOOTLOADER_COPY = R"CDB(#!/bin/bash
set -e

BOOTLOADER_SRC="$1"
DEST="$2"

if [ -z "$DEST" ] || [ -z "$BOOTLOADER_SRC" ]; then
    echo "Error:  BOOTLOADER_SRC and DEST must be provided."
    exit 1
fi

echo "Preparing bootloader in: $DEST"
echo "Using bootloaders from: $BOOTLOADER_SRC"

mkdir -p "$DEST/live" "$DEST/isolinux" "$DEST/boot/grub" "$DEST/EFI/BOOT"

if [ -d "$BOOTLOADER_SRC/ISOLINUX" ]; then
    cp -f "$BOOTLOADER_SRC/ISOLINUX/isolinux.bin" "$DEST/isolinux/" 2>/dev/null || true
    cp -f "$BOOTLOADER_SRC/ISOLINUX/isohdpfx.bin" "$DEST/isolinux/" 2>/dev/null || true
fi
if [ -d "$BOOTLOADER_SRC/syslinux/modules/bios" ]; then
    cp -f "$BOOTLOADER_SRC/syslinux/modules/bios/"*.c32 "$DEST/isolinux/" 2>/dev/null || true
fi

if [ -f "$BOOTLOADER_SRC/grub/x86_64-efi/monolithic/grubx64.efi" ]; then
    cp -f "$BOOTLOADER_SRC/grub/x86_64-efi/monolithic/grubx64.efi" "$DEST/EFI/BOOT/BOOTX64.EFI"
fi
if [ -f "$BOOTLOADER_SRC/grub/arm64-efi/monolithic/grubaa64.efi" ]; then
    cp -f "$BOOTLOADER_SRC/grub/arm64-efi/monolithic/grubaa64.efi" "$DEST/EFI/BOOT/BOOTAA64.EFI"
fi
if [ -f "$BOOTLOADER_SRC/grub/riscv64-efi/monolithic/grubriscv64.efi" ]; then
    cp -f "$BOOTLOADER_SRC/grub/riscv64-efi/monolithic/grubriscv64.efi" "$DEST/EFI/BOOT/BOOTRISCV64.EFI"
fi
if [ -f "$BOOTLOADER_SRC/grub/i386-efi/monolithic/grubia32.efi" ]; then
    cp -f "$BOOTLOADER_SRC/grub/i386-efi/monolithic/grubia32.efi" "$DEST/EFI/BOOT/BOOTIA32.EFI"
fi

# Fallback to the host's installed grub binaries
for pair in "arm64-efi:grubaa64.efi:BOOTAA64.EFI" "riscv64-efi:grubriscv64.efi:BOOTRISCV64.EFI" "x86_64-efi:grubx64.efi:BOOTX64.EFI"; do
    dir="${pair%%:*}"; rest="${pair#*:}"; src="${rest%%:*}"; dst="${rest#*:}"
    if [ ! -f "$DEST/EFI/BOOT/$dst" ]; then
        for f in "/usr/lib/grub/$dir/monolithic/$src" "/usr/lib/grub/$dir/grub.efi" "/boot/efi/EFI/BOOT/$dst"; do
            if [ -f "$f" ]; then
                cp -f "$f" "$DEST/EFI/BOOT/$dst"
                break
            fi
        done
    fi
done

echo "Bootloader preparation complete."
)CDB";

inline const char* GENERATE_MENUS = R"CDB(#!/bin/bash
set -e

ISODIR="$1"
PRETTY_NAME=$(grep ^PRETTY_NAME= /etc/os-release | cut -d= -f2 | tr -d '"')
BOOT_PARAMS="$2"
BOOT_COMMON="audit=0 splash quiet loglevel=3 systemd.show_status=auto udev.log_priority=3"
RAM_MODE_ENABLED="${3:-1}"

# live-config does not inherit the squashed filesystem's language: pass the master's
# locale explicitly as a boot parameter so the live session matches.
MASTER_LANG=""
if [ -f /etc/default/locale ]; then
    MASTER_LANG=$(grep -m1 '^LANG=' /etc/default/locale | cut -d= -f2 | tr -d '"')
fi
if [ -z "$MASTER_LANG" ] && [ -f /etc/locale.conf ]; then
    MASTER_LANG=$(grep -m1 '^LANG=' /etc/locale.conf | cut -d= -f2 | tr -d '"')
fi
[ -z "$MASTER_LANG" ] && MASTER_LANG="$LANG"
if [ -n "$MASTER_LANG" ]; then
    BOOT_COMMON="$BOOT_COMMON locales=$MASTER_LANG"
    echo "Master language detected: $MASTER_LANG"
fi

if [ -z "$ISODIR" ] || [ -z "$PRETTY_NAME" ]; then
    echo "Error: ISODIR or PRETTY_NAME missing."
    exit 1
fi
echo "Generating menus for: $PRETTY_NAME"

DEFAULT_BRANDING_DIR="@@CONFIG_DIR@@/branding.default/livecd"
BRANDING_DIR="@@CONFIG_DIR@@/branding/livecd"
if [ ! -d "$BRANDING_DIR" ]; then
    BRANDING_DIR="$DEFAULT_BRANDING_DIR"
fi

render_branding_template() {
    local source="$1"
    local target="$2"
    local content
    content=$(cat "$source")
    content=${content//\{\{\{fullname\}\}\}/$PRETTY_NAME}
    content=${content//\{\{\{kernel\}\}\}/$(uname -r)}
    content=${content//\{\{\{vmlinuz\}\}\}//live/vmlinuz}
    content=${content//\{\{\{initrdImg\}\}\}//live/initrd.img}
    content=${content//\{\{\{kernel_parameters\}\}\}/$BOOT_PARAMS $BOOT_COMMON}
    content=${content//\{\{\{rmModules\}\}\}/}
    printf '%s\n' "$content" > "$target"
}

if [ -f "$BRANDING_DIR/grub.main.cfg" ] && [ -f "$BRANDING_DIR/isolinux.main.cfg" ]; then
    render_branding_template "$BRANDING_DIR/grub.main.cfg" "$ISODIR/boot/grub/grub.cfg"
    render_branding_template "$BRANDING_DIR/isolinux.main.cfg" "$ISODIR/isolinux/isolinux.cfg"
    cat <<EOF > "$ISODIR/EFI/BOOT/grub.cfg"
search --set=root --label @@LIVE_LABEL@@
set prefix=(\$root)/boot/grub
configfile \$prefix/grub.cfg
EOF
    echo "Branding menus generated."
    exit 0
fi

MENU_TITLE="claudebackup linux"
START_LABEL="Start"
RAM_LABEL="RAM mode"

GRUB_RAM_ENTRY=""
ISOLINUX_RAM_ENTRY=""
if [ "$RAM_MODE_ENABLED" = "1" ]; then
    GRUB_RAM_ENTRY="menuentry \"$START_LABEL $PRETTY_NAME - $RAM_LABEL\" {
    linux /live/vmlinuz $BOOT_PARAMS $BOOT_COMMON toram
    initrd /live/initrd.img
}"
    ISOLINUX_RAM_ENTRY="
LABEL ram
    MENU LABEL $START_LABEL $PRETTY_NAME - $RAM_LABEL
    LINUX /live/vmlinuz
    APPEND $BOOT_PARAMS $BOOT_COMMON toram
    INITRD /live/initrd.img"
fi

GRUB_THEME_BLOCK="background_image /boot/grub/splash.png

set menu_color_normal=white/black
set menu_color_highlight=white/blue

set color_normal=white/black
set color_highlight=white/blue"
GRUB_HEADER_ENTRIES="menuentry \"--- $MENU_TITLE ---\" {
    true
}
menuentry \"\" {
    true
}"
GRUB_DEFAULT_INDEX=2

if [ -f "$ISODIR/boot/grub/theme.cfg" ]; then
    GRUB_THEME_BLOCK="insmod gfxmenu
set theme=/boot/grub/theme.cfg"
    GRUB_HEADER_ENTRIES=""
    GRUB_DEFAULT_INDEX=0
fi

cat <<EOF > "$ISODIR/boot/grub/grub.cfg"
set timeout=5
set default=$GRUB_DEFAULT_INDEX

insmod efi_gop
insmod efi_uga
insmod all_video
insmod gfxterm
insmod png
insmod part_gpt
insmod part_msdos
insmod fat
insmod iso9660

search --no-floppy --set=root --label @@LIVE_LABEL@@

if loadfont /boot/grub/font.pf2; then
    set gfxmode=auto
    terminal_output gfxterm
fi

$GRUB_THEME_BLOCK

$GRUB_HEADER_ENTRIES
menuentry "$START_LABEL $PRETTY_NAME" {
    linux /live/vmlinuz $BOOT_PARAMS $BOOT_COMMON
    initrd /live/initrd.img
}
$GRUB_RAM_ENTRY
EOF

ISOLINUX_HEADER="MENU BACKGROUND splash.png
MENU TITLE $MENU_TITLE
MENU ROW 2
MENU MARGIN 10
MENU ROWS 12
MENU TABMSGROW 27
MENU AUTOBOOTROW 28
MENU CMDLINEROW 27
MENU HELPMSGROW 27"

if [ -f "$ISODIR/isolinux/isolinux.theme.cfg" ]; then
    ISOLINUX_HEADER="MENU BACKGROUND splash.png
MENU TITLE $MENU_TITLE
include isolinux.theme.cfg"
fi

cat <<EOF > "$ISODIR/isolinux/isolinux.cfg"
UI vesamenu.c32
TIMEOUT 50
DEFAULT live

$ISOLINUX_HEADER

LABEL live
    MENU LABEL $START_LABEL $PRETTY_NAME
    LINUX /live/vmlinuz
    APPEND $BOOT_PARAMS $BOOT_COMMON
    INITRD /live/initrd.img
$ISOLINUX_RAM_ENTRY
EOF

cat <<EOF > "$ISODIR/EFI/BOOT/grub.cfg"
search --set=root --label @@LIVE_LABEL@@
set prefix=(\$root)/boot/grub
configfile \$prefix/grub.cfg
EOF

echo "Menus generated."
)CDB";

inline const char* TRUST_DESKTOP = R"CDB(#!/bin/bash
DESKTOP_DIR="$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")"
mkdir -p "$DESKTOP_DIR"

LAUNCHER_SRC="/usr/share/applications/install-system.desktop"
LAUNCHER_DEST="$DESKTOP_DIR/install-system.desktop"

if [ -f "$LAUNCHER_SRC" ]; then
    cp "$LAUNCHER_SRC" "$LAUNCHER_DEST"
    chmod +x "$LAUNCHER_DEST"
fi

for i in {1..15}; do
    if pgrep -x xfdesktop >/dev/null || pgrep -x nautilus >/dev/null || pgrep -x nemo >/dev/null || pgrep -x caja >/dev/null || pgrep -f ding >/dev/null || pgrep -x plasmashell >/dev/null || pgrep -x pcmanfm >/dev/null || pgrep -x pcmanfm-qt >/dev/null; then
        break
    fi
    sleep 1
done
sleep 2

if ! pgrep -x plasmashell > /dev/null; then
    if [ -f "$LAUNCHER_DEST" ]; then
        gio set "$LAUNCHER_DEST" metadata::trusted yes 2>/dev/null
        gio set "$LAUNCHER_DEST" metadata::xfce-exe-checksum "$(sha256sum "$LAUNCHER_DEST" | awk '{print $1}')" 2>/dev/null
    fi
fi
)CDB";

// Rendered by plan.h with the installer command filled in
inline const char* LAUNCHER_DESKTOP = R"CDB([Desktop Entry]
Type=Application
Name=Install System
Comment=Install this live system to disk
Icon=system-software-install
Exec=@@EXEC@@
Terminal=@@TERMINAL@@
Categories=System;
)CDB";

// Applies the build-time constants to an embedded script
inline std::string render(const char* script) {
    std::string s = script;
    s = replaceAll(s, "@@LIVE_LABEL@@", LIVE_LABEL);
    s = replaceAll(s, "@@CONFIG_DIR@@", CONFIG_DIR);
    return s;
}

}  // namespace scripts

#endif
