#ifndef FAMILIES_H
#define FAMILIES_H

#include "context.h"

// Per-family remaster modules, ported from penguins-eggs coa/brain.d/modules/*/remaster*.tmpl.
// Each family supplies four pieces that the common plan (plan.h) plugs in:
//   bootParams       kernel command line for the live medium
//   initramfsCmd     builds /boot/cbl-initrd.img inside the chroot
//   copyKernelCmd    copies kernel + initrd into isodir/live
//   hookSquashfsCmd  extra layout the distro's live initramfs expects
//
// The Arch family instead uses the claudemods zip method (ported from cmiadvanced): the ISO tree
// and mkinitcpio hooks come from the embedded build-image-arch-img.zip, the image is
// LiveOS/rootfs.img and GRUB boots it with label=<ISO Tag> (the volume label, default 2026).

namespace families {

inline bool isArch(const Context& c) { return c.distro.family == "archlinux"; }

// Where the extracted build-image-arch-img.zip lives (native::ensureArchFiles)
inline std::string archZipDir() { return STAGING_DIR + "/arch"; }

// Path of the compressed root filesystem inside the ISO tree
inline std::string squashfsDest(const Context& c) {
    if (isArch(c)) return c.isoDir + "/LiveOS/rootfs.img";
    return c.isoDir + "/live/filesystem.squashfs";
}

// ---------------------------------------------------------------------------
// Boot parameters
// ---------------------------------------------------------------------------
inline std::string bootParams(const Context& c) {
    const std::string& f = c.distro.family;
    if (f == "debian")
        return "boot=live components cow_spacesize=2G";
    if (f == "archlinux")
        return "label=" + c.isoTag;
    if (f == "fedora" || f == "opensuse")
        return "root=live:LABEL=" + LIVE_LABEL + " rd.live.image rd.shell rd.debug selinux=0 cow_spacesize=2G";
    if (f == "gentoo")
        return "root=live:LABEL=" + LIVE_LABEL + " rd.live.image rd.live.overlay.overlayfs=1 rd.shell";
    if (f == "alpine")
        return std::string("modules=loop,squashfs,") + (c.settings.imageFormat == "erofs" ? "erofs," : "") + "sd-mod,usb-storage alpinelivelabel=" + LIVE_LABEL + " alpinelivesquashfs=filesystem.squashfs cow_spacesize=2G quiet";
    return "";
}

// ---------------------------------------------------------------------------
// Initramfs (runs chrooted in liveroot)
// ---------------------------------------------------------------------------
inline bool isErofs(const Context& c) { return c.settings.imageFormat == "erofs"; }

// Extra kernel module the live initramfs needs to mount the image (erofs images only)
inline std::string erofsDriver(const Context& c) { return isErofs(c) ? " erofs" : ""; }

inline std::string initramfsDebian(const Context& c) {
    std::string s = R"CBL(#!/bin/bash
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
CONF_DIR=$(mktemp -d /tmp/cbl-initramfs.XXXXXX)
trap 'rm -rf "$CONF_DIR"' EXIT
cp -a /etc/initramfs-tools/. "$CONF_DIR/" 2>/dev/null || true
touch "$CONF_DIR/modules"
for mod in virtio_pci virtio_blk virtio_scsi isofs overlay@@EROFS@@; do
    if ! grep -qs "^$mod" "$CONF_DIR/modules"; then
        echo "$mod" >> "$CONF_DIR/modules"
    fi
done
# Setup > Edit Configs > Initramfs Modules (staged by the build)
if [ -f /tmp/cbl-initramfs.modules ]; then
    echo "-> Adding the custom initramfs modules..."
    grep -v '^[[:space:]]*#' /tmp/cbl-initramfs.modules >> "$CONF_DIR/modules" || true
    rm -f /tmp/cbl-initramfs.modules
fi

mkinitramfs -d "$CONF_DIR" -o /boot/cbl-initrd.img "$KERNEL_VERSION"
if [ ! -f /boot/cbl-initrd.img ]; then
    echo "[FATAL ERROR] /boot/cbl-initrd.img was not generated!"
    exit 1
fi
)CBL";
    return replaceAll(s, "@@EROFS@@", erofsDriver(c));
}

// Arch (host side, before the chroot): stage the zip's mkinitcpio.conf, 11-dm-initramfs.rules and
// working-hooks-btrfs-ext4 hooks on the liveroot tmpfs (/tmp is never cloned), pointing the hooks
// at the staged files instead of /etc/initcpio and /usr/lib/initcpio/udev.
inline std::string initramfsPrepArch(const Context& c) {
    return "set -e\nSRC=" + shq(archZipDir()) + "\nDEST=" + shq(c.liveRoot + "/tmp/cbl-initcpio") + "\n" + R"CBL(
if [ ! -d "$SRC/working-hooks-btrfs-ext4" ] || [ ! -f "$SRC/build-image-arch-img/mkinitcpio.conf" ]; then
    echo "[FATAL ERROR] the embedded Arch files were not extracted to $SRC!"
    exit 1
fi
echo "-> Staging the claudemods mkinitcpio hooks..."
rm -rf "$DEST"
mkdir -p "$DEST"
cp -a "$SRC/working-hooks-btrfs-ext4/." "$DEST/"
cp -f "$SRC/build-image-arch-img/mkinitcpio.conf" "$SRC/build-image-arch-img/11-dm-initramfs.rules" "$DEST/"
sed -i 's|/usr/lib/initcpio/udev/11-dm-initramfs.rules|/tmp/cbl-initcpio/11-dm-initramfs.rules|' "$DEST/install/arch"
sed -i 's|/etc/initcpio/arch_shutdown|/tmp/cbl-initcpio/arch_shutdown|' "$DEST/install/arch_shutdown"
)CBL";
}

inline std::string initramfsArch() {
    return R"CBL(#!/bin/bash
set -e

echo "-> Preparing Initramfs configuration for Live (Arch Family, claudemods hooks)..."

KERNEL_VERSION=$(uname -r)
if [ ! -d "/lib/modules/$KERNEL_VERSION" ]; then
    # Fall back if uname -r doesn't match an installed kernel (e.g. an unrebooted upgrade)
    KERNEL_VERSION=$(ls -1 /lib/modules | grep -E '^[0-9]' | head -n 1)
fi

HOOKS_DIR=/tmp/cbl-initcpio
if [ ! -f "$HOOKS_DIR/mkinitcpio.conf" ]; then
    echo "[FATAL ERROR] staged hooks not found in $HOOKS_DIR!"
    exit 1
fi
if ! command -v mkinitcpio > /dev/null 2>&1; then
    echo "[FATAL ERROR] mkinitcpio is not installed (pacman -S mkinitcpio)."
    exit 1
fi

# Drop-ins would override the live config: park them for the build, put them back on exit
mkdir -p /etc/mkinitcpio.conf.d.bak /usr/lib/mkinitcpio.conf.d.bak
[ "$(ls -A /etc/mkinitcpio.conf.d/ 2>/dev/null)" ] && mv /etc/mkinitcpio.conf.d/*.conf /etc/mkinitcpio.conf.d.bak/
[ "$(ls -A /usr/lib/mkinitcpio.conf.d/ 2>/dev/null)" ] && mv /usr/lib/mkinitcpio.conf.d/*.conf /usr/lib/mkinitcpio.conf.d.bak/

trap '
[ "$(ls -A /etc/mkinitcpio.conf.d.bak/ 2>/dev/null)" ] && mv /etc/mkinitcpio.conf.d.bak/*.conf /etc/mkinitcpio.conf.d/
[ "$(ls -A /usr/lib/mkinitcpio.conf.d.bak/ 2>/dev/null)" ] && mv /usr/lib/mkinitcpio.conf.d.bak/*.conf /usr/lib/mkinitcpio.conf.d/
rm -rf /etc/mkinitcpio.conf.d.bak /usr/lib/mkinitcpio.conf.d.bak "$HOOKS_DIR"
' EXIT

# HOOKS=(base udev arch_shutdown arch arch_loop_mnt arch_kms modconf block filesystems keyboard keymap)
echo "-> Generating unique live initramfs for kernel $KERNEL_VERSION..."
mkinitcpio -c "$HOOKS_DIR/mkinitcpio.conf" -D "$HOOKS_DIR" -D /etc/initcpio -D /usr/lib/initcpio \
    -k "$KERNEL_VERSION" -g "/boot/cbl-initrd.img"
)CBL";
}

// Arch: copies the zip's ISO tree (GRUB i386-pc/x86_64-efi, efi.img, theme, memtest, ucode) into
// isodir. The host's microcode images are used when installed, they are newer than the zip's.
inline std::string archBootFilesCmd(const Context& c) {
    return "set -e\nSRC=" + shq(archZipDir() + "/build-image-arch-img") + "\nISODIR=" + shq(c.isoDir) + "\n" + R"CBL(
if [ ! -f "$SRC/boot/efi.img" ] || [ ! -f "$SRC/boot/grub/i386-pc/eltorito.img" ]; then
    echo "[FATAL ERROR] the embedded Arch ISO tree is incomplete in $SRC!"
    exit 1
fi
echo "-> Copying the claudemods ISO tree into $ISODIR..."
mkdir -p "$ISODIR"
cp -a "$SRC/." "$ISODIR/"
# build-time files only, xorriso writes its own boot.catalog
rm -f "$ISODIR/mkinitcpio.conf" "$ISODIR/11-dm-initramfs.rules" "$ISODIR/boot.catalog" \
      "$ISODIR/boot/grub/themes/claudemods/background.png~"
mkdir -p "$ISODIR/LiveOS"
for u in intel-ucode.img amd-ucode.img; do
    if [ -f "/boot/$u" ]; then cp -f "/boot/$u" "$ISODIR/boot/$u"; fi
done
echo "-> ISO tree ready."
)CBL";
}

// Arch: boot/grub/kernels.cfg (sourced by the zip's grub.cfg). GRUB passes "$title" as $2, so
// "linux /boot/vmlinuz-$2" expands to /boot/vmlinuz-x86_64 <options> label=<ISO Tag>.
inline std::string archKernelsCfg(const Context& c) {
    std::string name = replaceAll(c.distro.prettyName, "\"", "");
    std::string s =
        "if cpuid -l; then\n"
        "    havekernel=\"Y\"\n"
        "    title=\"\"\n"
        "    for kopt in x86_64 $kopts " + bootParams(c) + "; do\n"
        "        if [ -n \"$title\" ] ; then\n"
        "            title=\"$title $kopt\";\n"
        "        else\n"
        "            title=\"$kopt\";\n"
        "        fi;\n"
        "    done\n";
    auto entry = [&](const std::string& label, const std::string& extra) {
        return "    menuentry \"" + label + "\" --class=arch.x86.64 \"$title" + extra + "\" {# set arguments above with the editor\n"
               "        linux /boot/vmlinuz-$2\n"
               "        initrd /boot/intel-ucode.img /boot/amd-ucode.img /boot/initramfs-x86_64.img\n"
               "    }\n";
    };
    s += entry("From CD/DVD/ISO: Boot " + name, "");
    s += entry("From Stick/HDD: Boot " + name, "");
    if (c.ramModeEnabled) s += entry("Boot " + name + " - RAM mode", " copytoram=y");
    s += R"CBL(fi
if [ "${havekernel}" != "Y" ]; then
    menuentry --class=find.none "NO SUITABLE KERNELS AVAILABLE" {echo $@  echo "There are no kernels suitable for this machine available."
        echo ""
        if ! cpuid -l; then
            echo "This machine is NOT 64bit capable."
            for kk in /boot/vmlinu*x86_64; do
                if [ "$kk" != "/boot/vmlinu*x86_64" ]; then
                    have32="true"
                fi
            done
            if [ "${have32}" != "true" ]; then
                echo "There are no 32bit kernels available"
                echo "It appears you are trying to boot a 64bit release on a 32bit machine"
                echo "This cannot work!"
            fi
        fi
        echo "Press Escape to return to the main menu"
        sleep --interruptible 9999
        menu_reload
    }
fi
)CBL";
    return s;
}

// dracut's main config: the user's edited copy (Setup > Edit Configs > Dracut Config) when there is one
inline const char* DRACUT_CUSTOM_CONF = R"CBL(
DRACUT_CONF=""
if [ -f /tmp/cbl-dracut.conf ]; then
    echo "-> Using the custom dracut config."
    DRACUT_CONF="--conf /tmp/cbl-dracut.conf"
fi
)CBL";

inline std::string initramfsFedora(const Context& c) {
    std::string s = std::string("#!/bin/bash\nset -e\n# fedora: initramfs\n") + DRACUT_CUSTOM_CONF + R"CBL(
echo "Generating live initramfs (dracut)..."
dracut -f --no-hostonly $DRACUT_CONF --add "dmsquash-live livenet" --add-drivers "sr_mod iso9660 squashfs loop overlay@@EROFS@@" /boot/cbl-initrd.img 2>&1 | tail -1
rm -f /tmp/cbl-dracut.conf
)CBL";
    return replaceAll(s, "@@EROFS@@", erofsDriver(c));
}

inline std::string initramfsOpensuse(const Context& c) {
    std::string s = std::string("#!/bin/bash\nset -e\n# opensuse: initramfs\n") + DRACUT_CUSTOM_CONF + R"CBL(
echo "Generating live initramfs (dracut)..."
# Drivers for physical and virtual controllers (AHCI, VirtIO, IDE)
dracut -f --no-hostonly $DRACUT_CONF --add "dmsquash-live livenet" --add-drivers "sr_mod iso9660 squashfs loop overlay@@EROFS@@ ahci virtio_blk virtio_pci virtio_scsi cdrom ata_piix sd_mod" /boot/cbl-initrd.img
rm -f /tmp/cbl-dracut.conf
)CBL";
    return replaceAll(s, "@@EROFS@@", erofsDriver(c));
}

// Gentoo: dracut dmsquash-live, like Fedora. Kernels are often hand-configured, so first check
// that the ones the live boot needs exist (as a module or built in) and say which option is missing.
inline std::string initramfsGentoo(const Context& c) {
    std::string s = std::string("#!/bin/bash\nset -e\n# gentoo: initramfs\n") + DRACUT_CUSTOM_CONF + R"CBL(
KERNEL_VERSION=$(uname -r)
if [ ! -d "/lib/modules/$KERNEL_VERSION" ]; then
    KERNEL_VERSION=$(ls -1 /lib/modules 2>/dev/null | grep -E '^[0-9]' | sort -V | tail -n 1)
fi
echo "-> Generating live initramfs Gentoo ($KERNEL_VERSION)..."

if ! command -v dracut >/dev/null 2>&1; then
    echo "[FATAL ERROR] dracut is not installed (emerge sys-kernel/dracut)."
    exit 1
fi
# The module folder's number/location differs between dracut and dracut-ng: match by name
DMSQ=$(ls -d /usr/lib/dracut/modules.d/*dmsquash-live /usr/lib64/dracut/modules.d/*dmsquash-live 2>/dev/null | head -n 1 || true)
if [ -z "$DMSQ" ]; then
    echo "[WARNING] dracut's dmsquash-live module was not found in /usr/lib*/dracut/modules.d -"
    echo "          letting dracut decide (if it fails: emerge --oneshot sys-kernel/dracut)."
fi
# dmsquash-live pulls in dracut's dm module, which needs dmsetup
if ! command -v dmsetup >/dev/null 2>&1; then
    echo "[FATAL ERROR] dmsetup is not installed (emerge sys-fs/lvm2 - dev-release/build.sh does this)."
    exit 1
fi

has_mod() {
    modinfo -k "$KERNEL_VERSION" "$1" >/dev/null 2>&1 ||
        grep -qs "/$1.ko" "/lib/modules/$KERNEL_VERSION/modules.builtin"
}
MISSING=""
has_mod squashfs || MISSING="$MISSING CONFIG_SQUASHFS"
has_mod overlay  || MISSING="$MISSING CONFIG_OVERLAY_FS"
has_mod isofs    || MISSING="$MISSING CONFIG_ISO9660_FS"
has_mod loop     || MISSING="$MISSING CONFIG_BLK_DEV_LOOP"
@@EROFS_CHECK@@
if [ -n "$MISSING" ]; then
    echo "[FATAL ERROR] kernel $KERNEL_VERSION cannot boot a live ISO, enable (=m or =y):$MISSING"
    echo "              then rebuild and install the kernel and run the build again."
    exit 1
fi

DRIVERS="iso9660 squashfs loop overlay@@EROFS@@"
has_mod sr_mod && DRIVERS="sr_mod $DRIVERS"
# multipath is not needed to boot a live ISO and fails on systems with multipath-tools installed
if ! dracut -f --no-hostonly $DRACUT_CONF --add "dmsquash-live" --omit "multipath" --add-drivers "$DRIVERS" --kver "$KERNEL_VERSION" /boot/cbl-initrd.img; then
    echo "[FATAL ERROR] dracut could not build the live initramfs - see its messages above."
    echo "              Check: ls /usr/lib/dracut/modules.d | grep squash   (re-emerge sys-kernel/dracut if empty)"
    exit 1
fi
rm -f /tmp/cbl-dracut.conf
if [ ! -f /boot/cbl-initrd.img ]; then
    echo "[FATAL ERROR] /boot/cbl-initrd.img was not generated!"
    exit 1
fi
)CBL";
    s = replaceAll(s, "@@EROFS@@", erofsDriver(c));
    return replaceAll(s, "@@EROFS_CHECK@@", isErofs(c) ? "has_mod erofs || MISSING=\"$MISSING CONFIG_EROFS_FS\"" : "");
}

// Alpine: patches the stock initramfs-init with a "sidecar" that mounts the live medium
// when Alpine's own boot fails, then rebuilds with mkinitfs. (oa-alpine-mkinitfs-custom)
inline std::string initramfsAlpine(const Context& c) {
    std::string s = R"CBL(#!/bin/bash
set -e
INIT_FILE="/usr/share/mkinitfs/initramfs-init"
PATCH_FILE="/tmp/cbl-sidecar.patch"
CUSTOM_INIT="/tmp/initramfs-cbl-init"

# mkinitfs reads its live config from /etc/mkinitfs: remove what we add so the clone is unchanged
trap 'rm -f /etc/mkinitfs/live.conf /etc/mkinitfs/features.d/cbllive.modules "$CUSTOM_INIT" "$PATCH_FILE"' EXIT

echo "-> 1. Generating the CBL-SIDECAR rescue (standard /run/live/medium)..."
cat << 'EOF' > "$PATCH_FILE"
    echo ">>> [CBL-SIDECAR] Alpine boot failed. Starting claudemods rescue... <<<" > /dev/console

    modprobe isofs squashfs erofs loop overlay 2>/dev/null

    CMDLINE=$(cat /proc/cmdline)
    LABEL=$(echo "$CMDLINE" | tr ' ' '\n' | grep -o 'alpinelivelabel=[^ ]*' | cut -d= -f2)
    [ -z "$LABEL" ] && LABEL="@@LIVE_LABEL@@"

    echo ">>> [CBL-SIDECAR] Looking for volume with LABEL: $LABEL <<<" > /dev/console

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
        if mount -t @@IMAGE_FS@@ -o ro "/live_data/medium/live/filesystem.squashfs" /live_data/rootfs/filesystem.squashfs; then
            mkdir -p /live_data/overlay/rw /live_data/overlay/work
            mount -t overlay overlay -o lowerdir=/live_data/rootfs/filesystem.squashfs,upperdir=/live_data/overlay/rw,workdir=/live_data/overlay/work /sysroot
            mkdir -p /sysroot/sys /sysroot/proc /sysroot/dev /sysroot/run
            mount --move /sys /sysroot/sys
            mount --move /proc /sysroot/proc
            mount --move /dev /sysroot/dev
            mkdir -p /sysroot/lib/live/mount
            mount --move /live_data /sysroot/lib/live/mount 2>/dev/null
            echo ">>> [CBL-SIDECAR] SYSTEM READY! switch_root... <<<" > /dev/console
            exec switch_root -c /dev/console /sysroot /sbin/init
        fi
    fi
    echo ">>> [CBL-SIDECAR] Rescue failed. Back to Alpine shell. <<<" > /dev/console
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
cat << 'MODULES' > /etc/mkinitfs/features.d/cbllive.modules
kernel/fs/isofs
kernel/fs/squashfs
kernel/fs/overlayfs
kernel/fs/loop
kernel/lib/libcrc32c*
@@EROFS_MODULE@@
MODULES

echo 'features="ata base ide scsi cdrom usb virtio ext4 squashfs cbllive"' > /etc/mkinitfs/live.conf

echo "-> 4. Building initramfs with the custom init..."
KVER=$(ls -1 /lib/modules/ | head -n 1)
mkinitfs -i "$CUSTOM_INIT" -c /etc/mkinitfs/live.conf -o /boot/initramfs-cbl "$KVER"
rm -f "$CUSTOM_INIT"
echo "-> alpine initramfs completed."
)CBL";
    s = replaceAll(s, "@@IMAGE_FS@@", isErofs(c) ? "erofs" : "squashfs");
    s = replaceAll(s, "@@EROFS_MODULE@@", isErofs(c) ? "kernel/fs/erofs" : "");
    return replaceAll(s, "@@LIVE_LABEL@@", LIVE_LABEL);
}

inline std::string initramfsCmd(const Context& c) {
    const std::string& f = c.distro.family;
    if (f == "debian")   return initramfsDebian(c);
    if (f == "archlinux") return initramfsArch();
    if (f == "fedora")   return initramfsFedora(c);
    if (f == "opensuse") return initramfsOpensuse(c);
    if (f == "alpine")   return initramfsAlpine(c);
    if (f == "gentoo")   return initramfsGentoo(c);
    return "";
}

// The user's edited copy of a file from Setup > Edit Configs (empty path = none)
inline std::string customFile(const Context& c, const std::string& name) {
    if (c.customDir.empty()) return "";
    std::string p = c.customDir + "/" + name;
    return isRegularFile(p) ? p : "";
}

// Copies an edited config onto the liveroot tmpfs, where the chrooted initramfs script finds it
inline std::string stageCustomCmd(const Context& c, const std::string& name, const std::string& staged) {
    std::string src = customFile(c, name);
    if (src.empty()) return "";
    return "echo \"-> Using your edited " + name + "\"\ncp -f " + shq(src) + " " + shq(c.liveRoot + "/tmp/" + staged) + "\n";
}

// Host-side preparation run before initramfsCmd (empty = nothing to do)
inline std::string initramfsPrepCmd(const Context& c) {
    const std::string& f = c.distro.family;
    if (isArch(c)) {
        std::string s = initramfsPrepArch(c);
        std::string mk = customFile(c, "mkinitcpio.conf");
        if (!mk.empty()) s += "echo \"-> Using your edited mkinitcpio.conf\"\ncp -f " + shq(mk) + " \"$DEST/mkinitcpio.conf\"\n";
        return s;
    }
    if (f == "debian") return stageCustomCmd(c, "initramfs-modules", "cbl-initramfs.modules");
    if (f == "fedora" || f == "opensuse" || f == "gentoo") return stageCustomCmd(c, "dracut.conf", "cbl-dracut.conf");
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
        s += R"CBL(
KERNEL_VERSION=$(uname -r)
if [ ! -d "$LIVEROOT/lib/modules/$KERNEL_VERSION" ]; then
    KERNEL_VERSION=$(ls -1 "$LIVEROOT/lib/modules" 2>/dev/null | grep -E '^[0-9]' | head -n 1)
fi
echo "-> Copying Kernel and Initramfs Debian/Ubuntu/Kylin ($KERNEL_VERSION)..."

if [ -f "$LIVEROOT/boot/vmlinuz-$KERNEL_VERSION" ]; then
    KERNEL_SRC="$LIVEROOT/boot/vmlinuz-$KERNEL_VERSION"
elif [ -f "$LIVEROOT/usr/lib/modules/$KERNEL_VERSION/vmlinuz" ]; then
    KERNEL_SRC="$LIVEROOT/usr/lib/modules/$KERNEL_VERSION/vmlinuz"
else
    KERNEL_SRC=$(find "$LIVEROOT/boot" -maxdepth 1 -name "vmlinuz-*" -type f | sort -V | tail -n 1)
fi
if [ ! -f "$KERNEL_SRC" ]; then
    echo "[FATAL ERROR] Kernel not found in $LIVEROOT/boot or /usr/lib/modules!"
    echo "              Kernel images in /boot: $(ls -1 "$LIVEROOT/boot" 2>/dev/null | tr '\n' ' ')"
    echo "              If the kernel was updated since the last reboot, reboot into it and build again."
    exit 1
fi

if [ -f "$LIVEROOT/boot/cbl-initrd.img" ]; then
    INITRD_SRC="$LIVEROOT/boot/cbl-initrd.img"
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
rm -f "$LIVEROOT/boot/cbl-initrd.img"
echo "-> Copy completed."
)CBL";
    } else if (f == "archlinux") {
        s += R"CBL(
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
mkdir -p "$ISODIR/boot"
cp "$KERNEL_SRC" "$ISODIR/boot/vmlinuz-x86_64"

if [ ! -f "$LIVEROOT/boot/cbl-initrd.img" ]; then
    echo "[FATAL ERROR] cbl-initrd.img not found in $LIVEROOT/boot!"
    exit 1
fi
echo "-> Copying cbl-initrd.img..."
cp "$LIVEROOT/boot/cbl-initrd.img" "$ISODIR/boot/initramfs-x86_64.img"

rm -f "$LIVEROOT/boot/initramfs-cbl-initrd.img"
rm -f "$LIVEROOT/boot/cbl-initrd.img"
echo "-> Copy completed."
)CBL";
    } else if (f == "fedora" || f == "opensuse") {
        s += R"CBL(
KERNEL_VERSION=$(uname -r)
echo "-> Copying Kernel ($KERNEL_VERSION)..."
INITRD_SRC="$LIVEROOT/boot/cbl-initrd.img"
DEST_DIR="$ISODIR/live"

# The kernel image of the version the initramfs was built for (its modules). It is not always
# /boot/vmlinuz-<version>: newer openSUSE / Fedora keep it in /usr/lib/modules/<version>/vmlinuz
# or in a boot-loader-spec folder (/boot/<machine-id>/<version>/linux).
KERNEL_SRC=""
for k in "$LIVEROOT/boot/vmlinuz-$KERNEL_VERSION" \
         "$LIVEROOT/usr/lib/modules/$KERNEL_VERSION/vmlinuz" \
         "$LIVEROOT/lib/modules/$KERNEL_VERSION/vmlinuz"; do
    if [ -f "$k" ]; then KERNEL_SRC="$k"; break; fi
done
if [ -z "$KERNEL_SRC" ]; then
    KERNEL_SRC=$(find "$LIVEROOT/boot" -maxdepth 4 -type f \( -path "*/$KERNEL_VERSION/linux" -o -path "*/$KERNEL_VERSION/linux-*" \) 2>/dev/null | head -n 1)
fi

if [ -z "$KERNEL_SRC" ] || [ ! -f "$KERNEL_SRC" ]; then
    echo "[FATAL ERROR] Cannot find the kernel image for $KERNEL_VERSION in $LIVEROOT/boot or /usr/lib/modules!"
    echo "              Kernel images in /boot:"
    ls -1 "$LIVEROOT/boot" 2>/dev/null | head -n 20
    echo "              Installed module sets: $(ls -1 "$LIVEROOT/lib/modules" 2>/dev/null | tr '\n' ' ')"
    echo "              If the kernel was updated since the last reboot, reboot into it and build again."
    exit 1
fi
echo "-> Kernel image: $KERNEL_SRC"
if [ ! -f "$INITRD_SRC" ]; then
    echo "[FATAL ERROR] Cannot find initrd in $INITRD_SRC! dracut did not generate it."
    exit 1
fi

mkdir -p "$DEST_DIR"
cp "$KERNEL_SRC" "$DEST_DIR/vmlinuz"
echo "-> Copying cbl-initrd.img..."
cp "$INITRD_SRC" "$DEST_DIR/initrd.img"
echo "-> Copy completed."
)CBL";
    } else if (f == "alpine") {
        s += R"CBL(
FLAVOR="$(uname -r | awk -F- '{print $NF}')"
if [ ! -f "$LIVEROOT/boot/vmlinuz-$FLAVOR" ]; then
    echo "[FATAL ERROR] Cannot find the kernel image $LIVEROOT/boot/vmlinuz-$FLAVOR!"
    echo "              Kernel images in /boot: $(ls -1 "$LIVEROOT/boot" 2>/dev/null | tr '\n' ' ')"
    echo "              If the kernel was updated since the last reboot, reboot into it and build again."
    exit 1
fi
if [ ! -f "$LIVEROOT/boot/initramfs-cbl" ]; then
    echo "[FATAL ERROR] initramfs-cbl was not generated in $LIVEROOT/boot!"
    exit 1
fi
mkdir -p "$ISODIR/live"
cp "$LIVEROOT/boot/vmlinuz-$FLAVOR" "$ISODIR/live/vmlinuz"
cp "$LIVEROOT/boot/initramfs-cbl" "$ISODIR/live/initrd.img"
)CBL";
    } else if (f == "gentoo") {
        // installkernel (vmlinuz-*), genkernel (kernel-*), dist-kernel / kernel-install
        // (/usr/lib/modules/*/vmlinuz) and a plain "make" in /usr/src
        s += R"CBL(
KERNEL_VERSION=$(uname -r)
if [ ! -d "$LIVEROOT/lib/modules/$KERNEL_VERSION" ]; then
    KERNEL_VERSION=$(ls -1 "$LIVEROOT/lib/modules" 2>/dev/null | grep -E '^[0-9]' | sort -V | tail -n 1)
fi
echo "-> Copying Kernel and Initramfs Gentoo ($KERNEL_VERSION)..."

KERNEL_SRC=""
for k in "$LIVEROOT/boot/vmlinuz-$KERNEL_VERSION" \
         "$LIVEROOT/boot/kernel-$KERNEL_VERSION" \
         "$LIVEROOT/boot/kernel-genkernel-$(uname -m)-$KERNEL_VERSION" \
         "$LIVEROOT/usr/lib/modules/$KERNEL_VERSION/vmlinuz" \
         "$LIVEROOT/usr/src/linux-$KERNEL_VERSION/arch/x86/boot/bzImage" \
         "$LIVEROOT/usr/src/linux-$KERNEL_VERSION/arch/arm64/boot/Image"; do
    if [ -f "$k" ]; then KERNEL_SRC="$k"; break; fi
done
if [ -z "$KERNEL_SRC" ]; then
    KERNEL_SRC=$(find "$LIVEROOT/boot" -maxdepth 1 -type f \( -name "vmlinuz-*" -o -name "kernel-*" \) | sort -V | tail -n 1)
fi
if [ -z "$KERNEL_SRC" ] || [ ! -f "$KERNEL_SRC" ]; then
    echo "[FATAL ERROR] Kernel for $KERNEL_VERSION not found in $LIVEROOT/boot, /usr/lib/modules or /usr/src!"
    exit 1
fi
if [ ! -f "$LIVEROOT/boot/cbl-initrd.img" ]; then
    echo "[FATAL ERROR] cbl-initrd.img not found in $LIVEROOT/boot! dracut did not generate it."
    exit 1
fi

echo "-> Copying from: $KERNEL_SRC"
mkdir -p "$ISODIR/live"
cp "$KERNEL_SRC" "$ISODIR/live/vmlinuz"
cp "$LIVEROOT/boot/cbl-initrd.img" "$ISODIR/live/initrd.img"
rm -f "$LIVEROOT/boot/cbl-initrd.img"
echo "-> Copy completed."
)CBL";
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

    if (f == "archlinux") {
        // mksquashfs already wrote LiveOS/rootfs.img; add its checksum like cmiadvanced did
        return "set -e\ncd " + shq(c.isoDir + "/LiveOS") + "\n"
               "echo \"-> Writing rootfs.img.sha512...\"\n"
               "sha512sum rootfs.img > rootfs.img.sha512\n";
    }
    if (f == "fedora" || f == "gentoo")
        return "set -e\nmkdir -p " + iso + "/LiveOS\n" + wait + "ln -f " + sq + " " + iso + "/LiveOS/squashfs.img\n";
    if (f == "opensuse")
        return "set -e\nmkdir -p " + iso + "/LiveOS\nln -f " + sq + " " + iso + "/LiveOS/squashfs.img\n";
    return "true\n";   // debian, alpine: nothing to do
}

}  // namespace families

#endif
