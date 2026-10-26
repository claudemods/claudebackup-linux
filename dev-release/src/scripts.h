#ifndef SCRIPTS_H
#define SCRIPTS_H

#include "common.h"

// Helper scripts embedded in the binary (ported from penguins-eggs configs/scripts).
// They are written to STAGING_DIR/scripts at run time, so no files need installing.
namespace scripts {

inline const char* BOOTLOADER_COPY = R"CBL(#!/bin/bash
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
)CBL";

inline const char* GENERATE_MENUS = R"CBL(#!/bin/bash
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

# The boot menu shows the distro's own name (e.g. "KDE neon 6.4"), not the app's
MENU_TITLE="$PRETTY_NAME"
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
)CBL";

// Alpine (Calamares does not run there): installs the cloned system from the live ISO back onto a
// disk - not a fresh setup-alpine install. Run from the live session. Plain sh (BusyBox).
// $1 target disk (/dev/sda, /dev/nvme0n1, ...), $2 ext4 | btrfs, $3 = 1 for btrfs compress=zstd:22
inline const char* ALPINE_INSTALLER = R"CBL(
DISK="$1"
FS="${2:-ext4}"
COMPRESS="${3:-0}"
T=/mnt/cbl-target
LIVE_ROOT=/lib/live/mount/rootfs/filesystem.squashfs        # mounted by the CBL-SIDECAR
LIVE_SQ=/lib/live/mount/medium/live/filesystem.squashfs

fail() { echo "[FATAL ERROR] $*"; exit 1; }

[ -b "$DISK" ] || fail "$DISK is not a disk."
MEDIUM=$(awk '$2=="/lib/live/mount/medium"{print $1}' /proc/mounts | head -n 1)
OWN_MEDIUM=""
if [ ! -d "$LIVE_ROOT/etc" ] && [ ! -f "$LIVE_SQ" ]; then
    # The live boot did not leave its mounts where expected: find the ISO by its label
    MEDIUM=$(findfs "LABEL=@@LIVE_LABEL@@" 2>/dev/null || blkid -L "@@LIVE_LABEL@@" 2>/dev/null || true)
    [ -n "$MEDIUM" ] || fail "the cloned system was not found - run this from the claudebackup live ISO."
    OWN_MEDIUM=/mnt/cbl-medium
    mkdir -p "$OWN_MEDIUM"
    mount -o ro "$MEDIUM" "$OWN_MEDIUM" || fail "cannot mount the live medium $MEDIUM."
    LIVE_SQ="$OWN_MEDIUM/live/filesystem.squashfs"
    [ -f "$LIVE_SQ" ] || fail "$LIVE_SQ not found on $MEDIUM."
fi
case "$MEDIUM" in "$DISK"*) fail "$DISK holds the live ISO you booted from." ;; esac
for tool in sfdisk mkfs.vfat grub-install grub-mkconfig mkinitfs; do
    command -v "$tool" >/dev/null 2>&1 || fail "$tool is missing - run dev-release/build.sh (Alpine installer tools) before building the ISO."
done
MKFS=mkfs.ext4
[ "$FS" = btrfs ] && MKFS=mkfs.btrfs
command -v "$MKFS" >/dev/null 2>&1 || fail "$MKFS is missing - run dev-release/build.sh (Alpine installer tools) before building the ISO."

UEFI=0
[ -d /sys/firmware/efi ] && UEFI=1
case "$DISK" in *[0-9]) P="${DISK}p" ;; *) P="$DISK" ;; esac

cleanup() {
    for m in sys/firmware/efi/efivars dev proc sys boot/efi ""; do
        mountpoint -q "$T/$m" 2>/dev/null && umount "$T/$m" 2>/dev/null
    done
    [ -n "$OWN_MEDIUM" ] && umount "$OWN_MEDIUM" 2>/dev/null
    sync
}
trap cleanup EXIT

echo "-> Partitioning $DISK ($([ "$UEFI" = 1 ] && echo UEFI/GPT || echo BIOS/MBR))..."
for p in $(awk '{print $1}' /proc/mounts | grep "^$DISK"); do umount "$p" 2>/dev/null || true; done
if [ "$UEFI" = 1 ]; then
    printf 'label: gpt\nsize=512M, type=U\ntype=L\n' | sfdisk --wipe always "$DISK"
    ESP="${P}1"; ROOT="${P}2"
else
    printf 'label: dos\ntype=83, bootable\n' | sfdisk --wipe always "$DISK"
    ESP=""; ROOT="${P}1"
fi
mdev -s 2>/dev/null || true
for i in 1 2 3 4 5 6 7 8 9 10; do [ -b "$ROOT" ] && break; sleep 1; done
[ -b "$ROOT" ] || fail "$ROOT did not appear after partitioning."

echo "-> Creating filesystems..."
if [ "$FS" = btrfs ]; then mkfs.btrfs -f "$ROOT"; else mkfs.ext4 -F "$ROOT"; fi
if [ -n "$ESP" ]; then mkfs.vfat -F 32 "$ESP"; fi

OPTS=defaults
[ "$FS" = btrfs ] && [ "$COMPRESS" = 1 ] && OPTS="defaults,compress=zstd:22"
mkdir -p "$T"
mount -o "$OPTS" "$ROOT" "$T"
if [ -n "$ESP" ]; then mkdir -p "$T/boot/efi"; mount "$ESP" "$T/boot/efi"; fi

echo "-> Copying the cloned system (this takes a while)..."
if [ -d "$LIVE_ROOT/etc" ]; then
    cp -a "$LIVE_ROOT/." "$T/"
else
    unsquashfs -f -d "$T" "$LIVE_SQ"
fi
for d in proc sys dev run tmp mnt media; do mkdir -p "$T/$d"; done
chmod 1777 "$T/tmp"

# /boot may have been a separate partition on the original system: keep the live kernel then
FLAVOR=$(uname -r | awk -F- '{print $NF}')
if ! ls "$T"/boot/vmlinuz-* >/dev/null 2>&1; then
    echo "-> No kernel in the clone's /boot, copying the live kernel..."
    mkdir -p "$T/boot"
    cp "$(dirname "$LIVE_SQ")/vmlinuz" "$T/boot/vmlinuz-$FLAVOR"
fi

echo "-> Writing /etc/fstab..."
uuid() { blkid "$1" | sed -n 's/.* UUID="\([^"]*\)".*/\1/p'; }
echo "UUID=$(uuid "$ROOT")	/	$FS	$OPTS	0 1" > "$T/etc/fstab"
if [ -n "$ESP" ]; then
    echo "UUID=$(uuid "$ESP")	/boot/efi	vfat	defaults	0 2" >> "$T/etc/fstab"
fi

echo "-> Installing the boot loader..."
for d in proc sys dev; do mount --bind "/$d" "$T/$d"; done
if [ "$UEFI" = 1 ]; then
    mount -t efivarfs efivarfs "$T/sys/firmware/efi/efivars" 2>/dev/null || true
fi
chroot "$T" /bin/sh -e -s "$FS" "$UEFI" "$DISK" << 'CHROOT'
FS="$1"; UEFI="$2"; DISK="$3"
CONF=/etc/mkinitfs/mkinitfs.conf
if [ -f "$CONF" ] && ! grep -q "$FS" "$CONF"; then
    sed -i "s/^features=\"/features=\"$FS /" "$CONF"
fi
KVER=$(ls -1 /lib/modules | head -n 1)
mkinitfs "$KVER"
CMDLINE="modules=sd-mod,usb-storage,$FS quiet rootfstype=$FS"
mkdir -p /etc/default
touch /etc/default/grub
if grep -q '^GRUB_CMDLINE_LINUX_DEFAULT=' /etc/default/grub; then
    sed -i "s|^GRUB_CMDLINE_LINUX_DEFAULT=.*|GRUB_CMDLINE_LINUX_DEFAULT=\"$CMDLINE\"|" /etc/default/grub
else
    echo "GRUB_CMDLINE_LINUX_DEFAULT=\"$CMDLINE\"" >> /etc/default/grub
fi
grep -q '^GRUB_TIMEOUT=' /etc/default/grub || echo "GRUB_TIMEOUT=5" >> /etc/default/grub
if [ "$UEFI" = 1 ]; then
    # The NVRAM boot entry can fail (VMs, locked firmware); the removable path below boots anyway
    grub-install --target=x86_64-efi --efi-directory=/boot/efi --bootloader-id=alpine ||
        echo "[WARNING] could not add a UEFI boot entry, relying on the removable EFI/BOOT path"
    grub-install --target=x86_64-efi --efi-directory=/boot/efi --removable
else
    grub-install --target=i386-pc "$DISK"
fi
grub-mkconfig -o /boot/grub/grub.cfg
CHROOT

echo "-> The cloned system is installed on $DISK. Remove the live medium and reboot."
)CBL";

// Applies the build-time constants to an embedded script
inline std::string render(const char* script) {
    std::string s = script;
    s = replaceAll(s, "@@LIVE_LABEL@@", LIVE_LABEL);
    s = replaceAll(s, "@@CONFIG_DIR@@", CONFIG_DIR);
    return s;
}

}  // namespace scripts

#endif
