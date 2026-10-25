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

// Arch: Calamares from the claudemods zips (port of cmiadvanced extrainstalls.sh).
// $1 = extracted calamares-files parent dir, $2 = 1 for btrfs zstd:22 mount options,
// $3 = 1 when the ISO image is erofs (unpackfs sourcefs: erofs)
inline const char* CALAMARES_SETUP = R"CDB(#!/bin/bash
set -e

SRC="$1/calamares-files"
BTRFS="${2:-0}"
EROFS="${3:-0}"
cd "$SRC"

echo "-> Installing Calamares packages..."
pacman -U --noconfirm --overwrite="*" calamares-*.pkg.tar* ckbcomp-*.pkg.tar*

echo "-> Copying Calamares configuration..."
cp -r calamares /etc/

echo "-> Copying claudemods branding..."
mkdir -p /usr/share/calamares/branding
cp -r claudemods /usr/share/calamares/branding/

echo "-> Extracting extra files..."
unzip -o -q extras.zip -d /

if [ "$BTRFS" = "1" ]; then
    echo "-> Applying btrfs level 22 compression (compress=zstd:22)..."
    cp -f mount.conf /usr/share/calamares/modules/
else
    echo "-> Using the default mount configuration (ext4 / plain btrfs)."
fi

if [ "$EROFS" = "1" ]; then
    echo "-> Applying Erofs Cloned System Image configuration..."
    cp -r erofs/* /
else
    echo "-> Using Default Cloned Squashfs System."
fi

rm -f /usr/share/applications/calamares.desktop
cp -f calamares.desktop /usr/share/applications/
rm -rf /usr/share/calamares/branding/manjaro

# initcpio preset = the running kernel's package (linux-cachyos, linux, linux-zen, ...)
PKGBASE=$(cat "/usr/lib/modules/$(uname -r)/pkgbase" 2>/dev/null || echo linux-cachyos)
for f in /etc/calamares/modules/initcpio.conf /usr/share/calamares/modules/initcpio.conf; do
    if [ -f "$f" ]; then sed -i "s/^kernel:.*/kernel: $PKGBASE/" "$f"; fi
done

echo "-> Calamares installed (kernel preset: $PKGBASE). It installs from /run/arch/bootmnt/LiveOS/rootfs.img."
)CDB";

// Debian, Fedora, openSUSE, Gentoo (Calamares does not run on Alpine): the distro's own
// Calamares package, configured for a full-system clone
// (no users page - the clone keeps its users) with the claudemods branding, unpacking the live
// squashfs. Runs on the host, so the next ISO contains it.
// $1 family, $2 extracted zips dir, $3 = 1 for btrfs zstd:22, $4 squashfs path in the live
// session, $5 os-release ID (EFI boot entry name)
inline const char* CALAMARES_SETUP_GENERIC = R"CDB(#!/bin/bash
set -e

FAMILY="$1"
SRC="$2/calamares-files"
BTRFS="${3:-0}"
UNPACK="$4"
DISTRO_ID="${5:-linux}"
ARCH=$(uname -m)

pkg_install() {
    if ! "$@"; then
        echo "[FATAL ERROR] package install failed: $*"
        echo "              Calamares may not be packaged for this release - enable the repository"
        echo "              that has it (e.g. Ubuntu universe) and try again."
        exit 1
    fi
}

echo "-> Installing Calamares, GRUB and rsync for $FAMILY..."
case "$FAMILY" in
    debian)
        PKGS="calamares rsync efibootmgr dosfstools"
        [ "$ARCH" = "x86_64" ] && PKGS="$PKGS grub-pc-bin grub-efi-amd64-bin"
        apt-get update || true
        pkg_install apt-get install -y $PKGS
        GRUB=grub; INITRAMFS=initramfs ;;
    fedora)
        PKGS="calamares rsync efibootmgr dosfstools"
        [ "$ARCH" = "x86_64" ] && PKGS="$PKGS grub2-pc-modules grub2-efi-x64-modules"
        pkg_install dnf install -y $PKGS
        GRUB=grub2; INITRAMFS=dracut ;;
    opensuse)
        PKGS="calamares rsync efibootmgr dosfstools"
        [ "$ARCH" = "x86_64" ] && PKGS="$PKGS grub2-i386-pc grub2-x86_64-efi"
        pkg_install zypper --non-interactive install $PKGS
        GRUB=grub2; INITRAMFS=dracut ;;
    gentoo)
        # app-admin/calamares is in ::gentoo but can be keyworded testing-only (~arch); accept it
        # for this one package if the plain emerge is refused. It builds Qt 6 / KF 6: be patient.
        PKGS="app-admin/calamares net-misc/rsync sys-boot/grub sys-boot/efibootmgr sys-fs/dosfstools"
        if ! emerge --noreplace --ask=n $PKGS; then
            KW="~$(portageq envvar ARCH 2>/dev/null || echo amd64)"
            echo "-> Accepting $KW for app-admin/calamares and retrying..."
            mkdir -p /etc/portage/package.accept_keywords
            echo "app-admin/calamares $KW" > /etc/portage/package.accept_keywords/claudebackup-calamares
            pkg_install emerge --noreplace --ask=n --autounmask=y --autounmask-continue=y $PKGS
        fi
        GRUB=grub; INITRAMFS=dracut ;;
    *)
        echo "[FATAL ERROR] no Calamares setup for family '$FAMILY'."
        exit 1 ;;
esac
if ! command -v calamares >/dev/null 2>&1; then
    echo "[FATAL ERROR] calamares was not installed."
    exit 1
fi

CFG=/etc/calamares
MOD=$CFG/modules
if [ -d "$CFG" ] && [ ! -d "$CFG.orig" ]; then
    echo "-> Keeping the distro's Calamares config in $CFG.orig"
    mv "$CFG" "$CFG.orig"
fi
rm -rf "$CFG"
mkdir -p "$MOD" "$CFG/scripts"

echo "-> Copying claudemods branding..."
mkdir -p /usr/share/calamares/branding
rm -rf /usr/share/calamares/branding/claudemods
cp -r "$SRC/claudemods" /usr/share/calamares/branding/

SYSTEMD=0
[ -d /run/systemd/system ] && SYSTEMD=1

# --- settings.conf ---------------------------------------------------------
# Plain sh word list (runs under /bin/sh when bash is not installed)
EXEC="partition mount unpackfs"
[ "$SYSTEMD" = 1 ] && EXEC="$EXEC machineid"
EXEC="$EXEC fstab locale keyboard localecfg shellprocess@cdbkernel $INITRAMFS grubcfg bootloader umount"
{
    echo "---"
    echo "modules-search: [ local ]"
    echo "instances:"
    echo "- id: cdbkernel"
    echo "  module: shellprocess"
    echo "  config: shellprocess_cdbkernel.conf"
    echo "sequence:"
    echo "- show:"
    for m in welcome locale keyboard partition summary; do echo "  - $m"; done
    echo "- exec:"
    for m in $EXEC; do echo "  - $m"; done
    echo "- show:"
    echo "  - finished"
    echo "branding: claudemods"
    echo "prompt-install: true"
    echo "dont-chroot: false"
    echo "oem-setup: false"
    echo "disable-cancel: false"
    echo "disable-cancel-during-exec: true"
    echo "quit-at-end: false"
} > "$CFG/settings.conf"

# --- module configs ---------------------------------------------------------
FSTYPE=ext4
[ "$BTRFS" = 1 ] && FSTYPE=btrfs

cat > "$MOD/unpackfs.conf" << EOF
---
unpack:
    - source: "$UNPACK"
      sourcefs: "squashfs"
      destination: ""
EOF

cat > "$MOD/partition.conf" << EOF
---
efiSystemPartition: "/boot/efi"
userSwapChoices:
    - none
    - small
    - file
initialPartitioningChoice: erase
initialSwapChoice: none
defaultFileSystemType: "$FSTYPE"
availableFileSystemTypes: ["ext4","btrfs","xfs"]
EOF

# Same mount.conf as Arch (btrfs subvolumes); compress=zstd:22 only when btrfs was chosen
cp -f "$SRC/mount.conf" "$MOD/mount.conf"
if [ "$BTRFS" != 1 ]; then
    sed -i 's/, compress=zstd:22//' "$MOD/mount.conf"
fi

# Calamares 3.2 reads mount options from fstab.conf (3.3 ignores these keys)
BTRFS_OPTS="defaults,noatime"
[ "$BTRFS" = 1 ] && BTRFS_OPTS="defaults,noatime,compress=zstd:22"
cat > "$MOD/fstab.conf" << EOF
---
mountOptions:
    default: defaults,noatime
    btrfs: $BTRFS_OPTS
ssdExtraMountOptions:
    ext4: discard
    btrfs: discard
crypttabOptions: luks
EOF

cat > "$MOD/bootloader.conf" << EOF
---
efiBootLoader: "grub"
timeout: "10"
grubInstall: "$GRUB-install"
grubMkconfig: "$GRUB-mkconfig"
grubCfg: "/boot/$GRUB/grub.cfg"
grubProbe: "$GRUB-probe"
efiBootMgr: "efibootmgr"
efiBootloaderId: "$DISTRO_ID"
installEFIFallback: true
EOF

cat > "$MOD/grubcfg.conf" << EOF
---
overwrite: false
keepDistributor: true
EOF

cat > "$MOD/initramfs.conf" << EOF
---
kernel: all
EOF

echo "---" > "$MOD/dracut.conf"

cat > "$MOD/machineid.conf" << EOF
---
systemd: true
dbus: true
dbus-symlink: true
EOF

# The squashfs is a bind of "/", so a separate /boot partition (common on Gentoo and Fedora) is
# empty in the clone: give the installed system a kernel before the initramfs step. Runs on the
# live system (dontChroot) and finds Calamares' target root itself.
cat > "$CFG/scripts/cdb-kernel.sh" << 'EOF'
#!/bin/sh
R="${1:-}"
[ -n "$R" ] || R=$(awk '$2 ~ /^\/tmp\/calamares-root-[^\/]*$/ {print $2}' /proc/mounts | head -n 1)
[ -d "$R" ] || { echo "cdb-kernel: target root not found"; exit 1; }
KV=$(uname -r)
if ls "$R"/boot/vmlinuz-* "$R"/boot/kernel-* >/dev/null 2>&1; then
    exit 0
fi
mkdir -p "$R/boot"
for k in "$R/usr/lib/modules/$KV/vmlinuz" "$R/lib/modules/$KV/vmlinuz" \
         /run/live/medium/live/vmlinuz /run/initramfs/live/live/vmlinuz; do
    if [ -f "$k" ]; then
        cp "$k" "$R/boot/vmlinuz-$KV"
        echo "cdb-kernel: /boot was empty, installed $k as /boot/vmlinuz-$KV"
        break
    fi
done
[ -f "$R/boot/vmlinuz-$KV" ] || { echo "cdb-kernel: no kernel found for $KV"; exit 1; }
# Fedora's GRUB boots BLS entries: create the entry for the copied kernel
if [ "@FAMILY@" = fedora ] && [ -x "$R/usr/bin/kernel-install" ]; then
    chroot "$R" kernel-install add "$KV" "/boot/vmlinuz-$KV" || true
fi
exit 0
EOF
sed -i "s/@FAMILY@/$FAMILY/" "$CFG/scripts/cdb-kernel.sh"
chmod 755 "$CFG/scripts/cdb-kernel.sh"
cat > "$MOD/shellprocess_cdbkernel.conf" << EOF
---
dontChroot: true
timeout: 600
script:
    - "/etc/calamares/scripts/cdb-kernel.sh"
EOF

REBOOT="reboot"
[ "$SYSTEMD" = 1 ] && REBOOT="systemctl -i reboot"
cat > "$MOD/finished.conf" << EOF
---
restartNowEnabled: true
restartNowChecked: false
restartNowCommand: "$REBOOT"
EOF

cat > "$MOD/welcome.conf" << EOF
---
showSupportUrl: false
showKnownIssuesUrl: false
showReleaseNotesUrl: false
requirements:
    requiredStorage: 10
    requiredRam: 1.0
    check:
        - storage
        - ram
        - root
    required:
        - storage
        - root
EOF

echo "-> Calamares configured for $FAMILY (unpacks $UNPACK, $FSTYPE default, $GRUB bootloader)."
)CDB";

// Alpine (Calamares does not run there): installs the cloned system from the live ISO back onto a
// disk - not a fresh setup-alpine install. Run from the live session. Plain sh (BusyBox).
// $1 target disk (/dev/sda, /dev/nvme0n1, ...), $2 ext4 | btrfs, $3 = 1 for btrfs compress=zstd:22
inline const char* ALPINE_INSTALLER = R"CDB(
DISK="$1"
FS="${2:-ext4}"
COMPRESS="${3:-0}"
T=/mnt/cdb-target
LIVE_ROOT=/lib/live/mount/rootfs/filesystem.squashfs        # mounted by the CDB-SIDECAR
LIVE_SQ=/lib/live/mount/medium/live/filesystem.squashfs

fail() { echo "[FATAL ERROR] $*"; exit 1; }

[ -b "$DISK" ] || fail "$DISK is not a disk."
MEDIUM=$(awk '$2=="/lib/live/mount/medium"{print $1}' /proc/mounts | head -n 1)
OWN_MEDIUM=""
if [ ! -d "$LIVE_ROOT/etc" ] && [ ! -f "$LIVE_SQ" ]; then
    # The live boot did not leave its mounts where expected: find the ISO by its label
    MEDIUM=$(findfs "LABEL=@@LIVE_LABEL@@" 2>/dev/null || blkid -L "@@LIVE_LABEL@@" 2>/dev/null || true)
    [ -n "$MEDIUM" ] || fail "the cloned system was not found - run this from the claudebackup live ISO."
    OWN_MEDIUM=/mnt/cdb-medium
    mkdir -p "$OWN_MEDIUM"
    mount -o ro "$MEDIUM" "$OWN_MEDIUM" || fail "cannot mount the live medium $MEDIUM."
    LIVE_SQ="$OWN_MEDIUM/live/filesystem.squashfs"
    [ -f "$LIVE_SQ" ] || fail "$LIVE_SQ not found on $MEDIUM."
fi
case "$MEDIUM" in "$DISK"*) fail "$DISK holds the live ISO you booted from." ;; esac
for tool in sfdisk mkfs.vfat grub-install grub-mkconfig mkinitfs; do
    command -v "$tool" >/dev/null 2>&1 || fail "$tool is missing - run Setup > Alpine Installer Tools before building the ISO."
done
MKFS=mkfs.ext4
[ "$FS" = btrfs ] && MKFS=mkfs.btrfs
command -v "$MKFS" >/dev/null 2>&1 || fail "$MKFS is missing - run Setup > Alpine Installer Tools before building the ISO."

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
