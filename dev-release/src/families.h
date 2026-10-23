#ifndef FAMILIES_H
#define FAMILIES_H

#include "context.h"

// Per-family remaster modules, ported from penguins-eggs coa/brain.d/modules/*/remaster*.tmpl.
// Each family supplies four pieces that the common plan (plan.h) plugs in:
//   bootParams       kernel command line for the live medium
//   initramfsCmd     builds /boot/cdb-initrd.img inside the chroot
//   copyKernelCmd    copies kernel + initrd into isodir/live
//   hookSquashfsCmd  extra layout the distro's live initramfs expects

namespace families {

inline bool isDracutArch(const Context& c) {
    return c.distro.distroID == "garuda" || c.distro.distroID == "endeavouros";
}

// ---------------------------------------------------------------------------
// Boot parameters
// ---------------------------------------------------------------------------
inline std::string bootParams(const Context& c) {
    const std::string& f = c.distro.family;
    if (f == "debian")
        return "boot=live components cow_spacesize=2G";
    if (f == "archlinux" || f == "manjaro") {
        if (isDracutArch(c))
            return "root=live:CDLABEL=" + LIVE_LABEL + " rd.live.image rd.live.dir=arch/x86_64 rd.live.squashimg=airootfs.sfs rd.live.overlay.size=2048 quiet";
        if (f == "manjaro")
            return "misobasedir=manjaro misolabel=" + LIVE_LABEL + " cow_spacesize=2G";
        return "archisobasedir=arch archisolabel=" + LIVE_LABEL + " cow_spacesize=2G";
    }
    if (f == "fedora" || f == "opensuse")
        return "root=live:LABEL=" + LIVE_LABEL + " rd.live.image rd.shell rd.debug selinux=0 cow_spacesize=2G";
    if (f == "alpine")
        return "modules=loop,squashfs,sd-mod,usb-storage alpinelivelabel=" + LIVE_LABEL + " alpinelivesquashfs=filesystem.squashfs cow_spacesize=2G quiet";
    return "";
}

// ---------------------------------------------------------------------------
// Initramfs (runs chrooted in liveroot)
// ---------------------------------------------------------------------------
inline std::string initramfsDebian() {
    return R"CDB(#!/bin/bash
set -e
# debian: initramfs
KERNEL_VERSION=$(uname -r)
if [ ! -d "/lib/modules/$KERNEL_VERSION" ]; then
    KERNEL_VERSION=$(ls -1 /lib/modules 2>/dev/null | grep -E '^[0-9]' | head -n 1)
fi

echo "-> Generating live initramfs Debian/Ubuntu/Kylin ($KERNEL_VERSION)..."

if ! command -v mkinitramfs >/dev/null 2>&1; then
    echo "[FATAL ERROR] mkinitramfs not found (install initramfs-tools)!"
    exit 1
fi

# Work on a throwaway copy of the initramfs config so /etc/initramfs-tools in the clone stays
# exactly as it is. Adds the modules a live boot needs (virtual storage, iso9660, overlay).
CONF_DIR=$(mktemp -d /tmp/cdb-initramfs.XXXXXX)
trap 'rm -rf "$CONF_DIR"' EXIT
cp -a /etc/initramfs-tools/. "$CONF_DIR/" 2>/dev/null || true
touch "$CONF_DIR/modules"
for mod in virtio_pci virtio_blk virtio_scsi isofs overlay; do
    if ! grep -qs "^$mod" "$CONF_DIR/modules"; then
        echo "$mod" >> "$CONF_DIR/modules"
    fi
done

mkinitramfs -d "$CONF_DIR" -o /boot/cdb-initrd.img "$KERNEL_VERSION"
if [ ! -f /boot/cdb-initrd.img ]; then
    echo "[FATAL ERROR] /boot/cdb-initrd.img was not generated!"
    exit 1
fi
)CDB";
}

inline std::string initramfsArch(const Context& c) {
    std::string s = R"CDB(#!/bin/bash
set -e

echo "-> Preparing Initramfs configuration for Live (Arch Family)..."

KERNEL_VERSION=$(uname -r)
if [ ! -d "/lib/modules/$KERNEL_VERSION" ]; then
    # Fall back if uname -r doesn't match an installed kernel (e.g. an unrebooted upgrade)
    KERNEL_VERSION=$(ls -1 /lib/modules | grep -E '^[0-9]' | head -n 1)
fi
)CDB";

    if (isDracutArch(c)) {
        s += R"CDB(
# Garuda / EndeavourOS branch (dracut)
echo "-> Garuda/EndeavourOS detected: generating live initramfs via dracut"
if ! command -v dracut > /dev/null 2>&1; then
    echo "[FATAL ERROR] dracut is not installed (sudo pacman -S dracut)."
    exit 1
fi
# -N disables host-only mode (crucial for live environments)
dracut -N -f --add "dmsquash-live" --add-drivers "sr_mod iso9660 squashfs loop overlay" --kver "$KERNEL_VERSION" "/boot/cdb-initrd.img"
)CDB";
        return s;
    }

    s += R"CDB(
# Arch / Manjaro branch (mkinitcpio)
cp /etc/mkinitcpio.conf /etc/mkinitcpio.conf.bak
mkdir -p /etc/mkinitcpio.conf.d.bak /usr/lib/mkinitcpio.conf.d.bak
[ "$(ls -A /etc/mkinitcpio.conf.d/ 2>/dev/null)" ] && mv /etc/mkinitcpio.conf.d/*.conf /etc/mkinitcpio.conf.d.bak/
[ "$(ls -A /usr/lib/mkinitcpio.conf.d/ 2>/dev/null)" ] && mv /usr/lib/mkinitcpio.conf.d/*.conf /usr/lib/mkinitcpio.conf.d.bak/

trap '
mv /etc/mkinitcpio.conf.bak /etc/mkinitcpio.conf
[ "$(ls -A /etc/mkinitcpio.conf.d.bak/ 2>/dev/null)" ] && mv /etc/mkinitcpio.conf.d.bak/*.conf /etc/mkinitcpio.conf.d/
[ "$(ls -A /usr/lib/mkinitcpio.conf.d.bak/ 2>/dev/null)" ] && mv /usr/lib/mkinitcpio.conf.d.bak/*.conf /usr/lib/mkinitcpio.conf.d/
rm -rf /etc/mkinitcpio.conf.d.bak /usr/lib/mkinitcpio.conf.d.bak
' EXIT
)CDB";

    if (c.distro.family == "manjaro") {
        s += R"CDB(
if ! pacman -Qq manjaro-tools-iso > /dev/null 2>&1; then
    echo "[FATAL ERROR] manjaro-tools-iso is not installed (sudo pacman -S manjaro-tools-iso)."
    exit 1
fi
cat << 'EOF' > /etc/mkinitcpio.conf
MODULES=(loop dm-snapshot btrfs sr_mod isofs)
HOOKS=(base udev microcode modconf kms miso miso_loop_mnt block keyboard keymap consolefont filesystems)
COMPRESSION="zstd"
EOF
)CDB";
    } else {
        s += R"CDB(
if ! pacman -Qq mkinitcpio-archiso > /dev/null 2>&1; then
    echo "[FATAL ERROR] mkinitcpio-archiso is not installed (sudo pacman -S archiso mkinitcpio-archiso)."
    exit 1
fi
cat << 'EOF' > /etc/mkinitcpio.conf
MODULES=(loop dm-snapshot btrfs sr_mod isofs)
HOOKS=(base udev microcode modconf kms archiso archiso_loop_mnt block filesystems keyboard keymap consolefont)
COMPRESSION="zstd"
EOF
)CDB";
    }

    s += R"CDB(
echo "-> Generating unique live initramfs for kernel $KERNEL_VERSION..."
mkinitcpio -c /etc/mkinitcpio.conf -k "$KERNEL_VERSION" -g "/boot/cdb-initrd.img"
)CDB";
    return s;
}

inline std::string initramfsFedora() {
    return R"CDB(#!/bin/bash
set -e
# fedora: initramfs
echo "Generating live initramfs (dracut)..."
dracut -f --no-hostonly --add "dmsquash-live livenet" --add-drivers "sr_mod iso9660 squashfs loop overlay" /boot/cdb-initrd.img 2>&1 | tail -1
)CDB";
}

inline std::string initramfsOpensuse() {
    return R"CDB(#!/bin/bash
set -e
# opensuse: initramfs
echo "Generating live initramfs (dracut)..."
# Drivers for physical and virtual controllers (AHCI, VirtIO, IDE)
dracut -f --no-hostonly --add "dmsquash-live livenet" --add-drivers "sr_mod iso9660 squashfs loop overlay ahci virtio_blk virtio_pci virtio_scsi cdrom ata_piix sd_mod" /boot/cdb-initrd.img
)CDB";
}

// Alpine: patches the stock initramfs-init with a "sidecar" that mounts the live medium
// when Alpine's own boot fails, then rebuilds with mkinitfs. (oa-alpine-mkinitfs-custom)
inline std::string initramfsAlpine() {
    std::string s = R"CDB(#!/bin/bash
set -e
INIT_FILE="/usr/share/mkinitfs/initramfs-init"
PATCH_FILE="/tmp/cdb-sidecar.patch"
CUSTOM_INIT="/tmp/initramfs-cdb-init"

# mkinitfs reads its live config from /etc/mkinitfs: remove what we add so the clone is unchanged
trap 'rm -f /etc/mkinitfs/live.conf /etc/mkinitfs/features.d/cdblive.modules "$CUSTOM_INIT" "$PATCH_FILE"' EXIT

echo "-> 1. Generating the CDB-SIDECAR rescue (standard /run/live/medium)..."
cat << 'EOF' > "$PATCH_FILE"
    echo ">>> [CDB-SIDECAR] Alpine boot failed. Starting claudemods rescue... <<<" > /dev/console

    modprobe isofs squashfs loop overlay 2>/dev/null

    CMDLINE=$(cat /proc/cmdline)
    LABEL=$(echo "$CMDLINE" | tr ' ' '\n' | grep -o 'alpinelivelabel=[^ ]*' | cut -d= -f2)
    [ -z "$LABEL" ] && LABEL="@@LIVE_LABEL@@"

    echo ">>> [CDB-SIDECAR] Looking for volume with LABEL: $LABEL <<<" > /dev/console

    for wait in 1 2 3 4 5; do
        mdev -s 2>/dev/null
        ISO_DEV=$(findfs "LABEL=$LABEL" 2>/dev/null)
        [ -n "$ISO_DEV" ] && break
        sleep 1
    done

    if [ -n "$ISO_DEV" ]; then
        mkdir -p /live_data
        mount -t tmpfs tmpfs /live_data
        mkdir -p /live_data/medium /live_data/rootfs/filesystem.squashfs /live_data/overlay /sysroot
        mount -o ro "$ISO_DEV" /live_data/medium
        if mount -t squashfs -o ro "/live_data/medium/live/filesystem.squashfs" /live_data/rootfs/filesystem.squashfs; then
            mkdir -p /live_data/overlay/rw /live_data/overlay/work
            mount -t overlay overlay -o lowerdir=/live_data/rootfs/filesystem.squashfs,upperdir=/live_data/overlay/rw,workdir=/live_data/overlay/work /sysroot
            mkdir -p /sysroot/sys /sysroot/proc /sysroot/dev /sysroot/run
            mount --move /sys /sysroot/sys
            mount --move /proc /sysroot/proc
            mount --move /dev /sysroot/dev
            mkdir -p /sysroot/lib/live/mount
            mount --move /live_data /sysroot/lib/live/mount 2>/dev/null
            echo ">>> [CDB-SIDECAR] SYSTEM READY! switch_root... <<<" > /dev/console
            exec switch_root -c /dev/console /sysroot /sbin/init
        fi
    fi
    echo ">>> [CDB-SIDECAR] Rescue failed. Back to Alpine shell. <<<" > /dev/console
EOF

echo "-> 2. Injecting into a temporary init file (host untouched)..."
while IFS= read -r line || [ -n "$line" ]; do
    printf "%s\n" "$line"
    case "$line" in
        *"recovery_shell() {"*)
            cat "$PATCH_FILE"
            ;;
    esac
done < "$INIT_FILE" > "$CUSTOM_INIT"

rm -f "$PATCH_FILE"
chmod +x "$CUSTOM_INIT"

echo "-> 3. Configuring mkinitfs modules for LIVE..."
mkdir -p /etc/mkinitfs/features.d
cat << 'MODULES' > /etc/mkinitfs/features.d/cdblive.modules
kernel/fs/isofs
kernel/fs/squashfs
kernel/fs/overlayfs
kernel/fs/loop
kernel/lib/libcrc32c*
MODULES

echo 'features="ata base ide scsi cdrom usb virtio ext4 squashfs cdblive"' > /etc/mkinitfs/live.conf

echo "-> 4. Building initramfs with the custom init..."
KVER=$(ls -1 /lib/modules/ | head -n 1)
mkinitfs -i "$CUSTOM_INIT" -c /etc/mkinitfs/live.conf -o /boot/initramfs-cdb "$KVER"
rm -f "$CUSTOM_INIT"
echo "-> alpine initramfs completed."
)CDB";
    return replaceAll(s, "@@LIVE_LABEL@@", LIVE_LABEL);
}

inline std::string initramfsCmd(const Context& c) {
    const std::string& f = c.distro.family;
    if (f == "debian")   return initramfsDebian();
    if (f == "archlinux" || f == "manjaro") return initramfsArch(c);
    if (f == "fedora")   return initramfsFedora();
    if (f == "opensuse") return initramfsOpensuse();
    if (f == "alpine")   return initramfsAlpine();
    return "";
}

// ---------------------------------------------------------------------------
// Copy kernel + initrd (runs on the host, reads from liveroot)
// ---------------------------------------------------------------------------
inline std::string vars(const Context& c) {
    return "LIVEROOT=" + shq(c.liveRoot) + "\nISODIR=" + shq(c.isoDir) + "\n";
}

inline std::string copyKernelCmd(const Context& c) {
    const std::string& f = c.distro.family;
    std::string s = "#!/bin/bash\nset -e\n" + vars(c);

    if (f == "debian") {
        s += R"CDB(
KERNEL_VERSION=$(uname -r)
if [ ! -d "$LIVEROOT/lib/modules/$KERNEL_VERSION" ]; then
    KERNEL_VERSION=$(ls -1 "$LIVEROOT/lib/modules" 2>/dev/null | grep -E '^[0-9]' | head -n 1)
fi
echo "-> Copying Kernel and Initramfs Debian/Ubuntu/Kylin ($KERNEL_VERSION)..."

if [ -f "$LIVEROOT/boot/vmlinuz-$KERNEL_VERSION" ]; then
    KERNEL_SRC="$LIVEROOT/boot/vmlinuz-$KERNEL_VERSION"
else
    KERNEL_SRC=$(find "$LIVEROOT/boot" -maxdepth 1 -name "vmlinuz-*" -type f | sort -V | tail -n 1)
fi
if [ ! -f "$KERNEL_SRC" ]; then
    echo "[FATAL ERROR] Kernel not found in $LIVEROOT/boot!"
    exit 1
fi

if [ -f "$LIVEROOT/boot/cdb-initrd.img" ]; then
    INITRD_SRC="$LIVEROOT/boot/cdb-initrd.img"
elif [ -f "$LIVEROOT/boot/initrd.img-$KERNEL_VERSION" ]; then
    INITRD_SRC="$LIVEROOT/boot/initrd.img-$KERNEL_VERSION"
else
    INITRD_SRC=$(find "$LIVEROOT/boot" -maxdepth 1 -name "initrd.img-*" -type f | sort -V | tail -n 1)
fi
if [ ! -f "$INITRD_SRC" ]; then
    echo "[FATAL ERROR] initrd not found in $LIVEROOT/boot!"
    exit 1
fi

mkdir -p "$ISODIR/live"
cp "$KERNEL_SRC" "$ISODIR/live/vmlinuz"
echo "-> Copying initrd ($INITRD_SRC -> $ISODIR/live/initrd.img)..."
cp "$INITRD_SRC" "$ISODIR/live/initrd.img"
rm -f "$LIVEROOT/boot/cdb-initrd.img"
echo "-> Copy completed."
)CDB";
    } else if (f == "archlinux" || f == "manjaro") {
        s += R"CDB(
KERNEL_VERSION=$(uname -r)
KVER_MAJOR_MINOR=$(echo "$KERNEL_VERSION" | cut -d. -f1,2)
ARCH=$(uname -m)
echo "-> Copying Kernel and Initramfs for the Arch family ($KERNEL_VERSION)..."

PKGBASE_FILE="$LIVEROOT/lib/modules/$KERNEL_VERSION/pkgbase"
if [ ! -f "$PKGBASE_FILE" ]; then
    echo "[WARNING] pkgbase file missing in $PKGBASE_FILE! Attempting logical fallback."
fi
PKG_NAME=$(cat "$PKGBASE_FILE" 2>/dev/null || echo "linux")

if [ -f "$LIVEROOT/boot/vmlinuz-$PKG_NAME" ]; then
    KERNEL_SRC="$LIVEROOT/boot/vmlinuz-$PKG_NAME"
elif [ -f "$LIVEROOT/boot/vmlinuz-${KVER_MAJOR_MINOR}-${ARCH}" ]; then
    KERNEL_SRC="$LIVEROOT/boot/vmlinuz-${KVER_MAJOR_MINOR}-${ARCH}"
elif [ -f "$LIVEROOT/usr/lib/modules/$KERNEL_VERSION/vmlinuz" ]; then
    echo "-> Found kernel inside modules directory (systemd-boot layout detected)"
    KERNEL_SRC="$LIVEROOT/usr/lib/modules/$KERNEL_VERSION/vmlinuz"
else
    echo "-> Standard patterns not found, searching for a valid vmlinuz in /boot..."
    KERNEL_SRC=$(find "$LIVEROOT/boot" -maxdepth 1 -name "vmlinuz-*" -type f -print -quit)
fi

if [ -z "$KERNEL_SRC" ] || [ ! -f "$KERNEL_SRC" ]; then
    echo "[FATAL ERROR] Unable to determine the kernel file in $LIVEROOT!"
    exit 1
fi

echo "-> Copying from: $KERNEL_SRC"
mkdir -p "$ISODIR/live"
cp "$KERNEL_SRC" "$ISODIR/live/vmlinuz"

if [ ! -f "$LIVEROOT/boot/cdb-initrd.img" ]; then
    echo "[FATAL ERROR] cdb-initrd.img not found in $LIVEROOT/boot!"
    exit 1
fi
echo "-> Copying cdb-initrd.img..."
cp "$LIVEROOT/boot/cdb-initrd.img" "$ISODIR/live/initrd.img"

rm -f "$LIVEROOT/boot/initramfs-cdb-initrd.img"
rm -f "$LIVEROOT/boot/cdb-initrd.img"
echo "-> Copy completed."
)CDB";
    } else if (f == "fedora" || f == "opensuse") {
        s += R"CDB(
KERNEL_VERSION=$(uname -r)
echo "-> Copying Kernel ($KERNEL_VERSION)..."
KERNEL_SRC="$LIVEROOT/boot/vmlinuz-$KERNEL_VERSION"
INITRD_SRC="$LIVEROOT/boot/cdb-initrd.img"
DEST_DIR="$ISODIR/live"

if [ ! -f "$KERNEL_SRC" ]; then
    echo "[FATAL ERROR] Cannot find kernel in $KERNEL_SRC!"
    exit 1
fi
if [ ! -f "$INITRD_SRC" ]; then
    echo "[FATAL ERROR] Cannot find initrd in $INITRD_SRC! dracut did not generate it."
    exit 1
fi

mkdir -p "$DEST_DIR"
cp "$KERNEL_SRC" "$DEST_DIR/vmlinuz"
echo "-> Copying cdb-initrd.img..."
cp "$INITRD_SRC" "$DEST_DIR/initrd.img"
echo "-> Copy completed."
)CDB";
    } else if (f == "alpine") {
        s += R"CDB(
FLAVOR="$(uname -r | awk -F- '{print $NF}')"
mkdir -p "$ISODIR/live"
cp "$LIVEROOT/boot/vmlinuz-$FLAVOR" "$ISODIR/live/vmlinuz"
cp "$LIVEROOT/boot/initramfs-cdb" "$ISODIR/live/initrd.img"
)CDB";
    }
    return s;
}

// ---------------------------------------------------------------------------
// Hard link isodir/live/filesystem.squashfs where the distro's live initramfs looks for it
// ---------------------------------------------------------------------------
inline std::string hookSquashfsCmd(const Context& c) {
    const std::string& f = c.distro.family;
    const std::string sq = shq(c.isoDir + "/live/filesystem.squashfs");
    const std::string iso = shq(c.isoDir);

    std::string wait = "timeout=10\nwhile [ ! -f " + sq + " ] && [ $timeout -gt 0 ]; do\n    sleep 1; timeout=$((timeout-1))\ndone\n";

    if (f == "archlinux" || f == "manjaro") {
        std::string s = "set -e\necho \"-> Preparing filesystem.squashfs hardlink...\"\n" + wait +
            "if [ ! -f " + sq + " ]; then\n    echo \"[FATAL ERROR] filesystem.squashfs not found after timeout!\"\n    exit 1\nfi\n";
        if (f == "manjaro")
            s += "mkdir -p " + iso + "/manjaro/x86_64\nln -f " + sq + " " + iso + "/manjaro/x86_64/livefs.sfs\n";
        else
            s += "mkdir -p " + iso + "/arch/x86_64\nln -f " + sq + " " + iso + "/arch/x86_64/airootfs.sfs\n";
        return s;
    }
    if (f == "fedora")
        return "set -e\nmkdir -p " + iso + "/LiveOS\n" + wait + "ln -f " + sq + " " + iso + "/LiveOS/squashfs.img\n";
    if (f == "opensuse")
        return "set -e\nmkdir -p " + iso + "/LiveOS\nln -f " + sq + " " + iso + "/LiveOS/squashfs.img\n";
    return "true\n";   // debian, alpine: nothing to do
}

}  // namespace families

#endif
