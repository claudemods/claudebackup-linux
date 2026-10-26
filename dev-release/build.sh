#!/bin/sh
# build.sh - everything claudebackup-linux needs, in one go:
#   1. detects the distro
#   2. installs the build tools and the host tools the ISO build uses (per distro)
#   3. optionally installs Calamares (Arch: the claudemods zips; Debian, Fedora, openSUSE,
#      Gentoo: the distro's package + a full-clone config), or on Alpine the tools of its
#      "Install Cloned System To Disk" installer (Calamares does not run on Alpine)
#   4. builds with make, installs the binary and launches it
#   5. puts an "Install <distro>" shortcut on the Desktop (Calamares, or on Alpine Konsole running
#      "claudebackup-linux alpine"), so the live ISO's desktop has it
# Plain POSIX sh, so it also runs on a stock Alpine (sh build.sh) without bash.
# Run it from anywhere; it builds from its own folder (dev-release).
set -e

SELF="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"
SRC_DIR="$(dirname "$SELF")"
TARGET="claudebackup-linux"

say() { printf '\033[1;31m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

# Single-quote one argument for a shell command line (for su -c)
q() { printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"; }

# ---- privilege helper (sudo, doas, run0, pkexec or su) ----------------------
as_root() {
    if [ "$(id -u)" -eq 0 ]; then "$@"
    elif command -v sudo   >/dev/null 2>&1; then sudo "$@"
    elif command -v doas   >/dev/null 2>&1; then doas "$@"
    elif command -v run0   >/dev/null 2>&1; then run0 "$@"
    elif command -v pkexec >/dev/null 2>&1; then pkexec "$@"
    else
        cmd=""
        for a in "$@"; do cmd="$cmd $(q "$a")"; done
        su root -c "$cmd"
    fi
}

# ---- distro detection (ID, then each ID_LIKE entry, as src/distro.h does) ---
detect_family() {
    [ -r /etc/os-release ] || die "/etc/os-release not found"
    # shellcheck disable=SC1091
    . /etc/os-release
    FAMILY=""
    for id in ${ID:-} ${ID_LIKE:-}; do
        case "$(printf '%s' "$id" | tr '[:upper:]' '[:lower:]')" in
            debian|ubuntu|linuxmint|pop|kali|devuan|openkylin|quirinux|abellha) FAMILY=debian ;;
            arch|archlinux|archcraft|cachyos|endeavouros|garuda|rebornos)       FAMILY=arch ;;
            fedora|nobara|rhel)                                                 FAMILY=fedora ;;
            opensuse*|suse)                                                     FAMILY=opensuse ;;
            alpine)                                                             FAMILY=alpine ;;
            gentoo|calculate|funtoo|pentoo)                                     FAMILY=gentoo ;;
        esac
        [ -n "$FAMILY" ] && break
    done
    [ -n "$FAMILY" ] || die "unsupported distro: ${PRETTY_NAME:-${ID:-unknown}}"
    DISTRO_ID="$(printf '%s' "${ID:-linux}" | tr '[:upper:]' '[:lower:]')"
    # What messages call the distro: its own name (e.g. "KDE neon 6.4"), not the family
    DISTRO_NAME="${PRETTY_NAME:-${NAME:-${ID:-Linux}}}"
}

# =============================================================================
# Root phase: everything that installs packages or writes system files
# =============================================================================

# Gentoo: emerge refuses to work without the ::gentoo repository and a valid profile. Systems
# installed from a live ISO often have neither (no repo, make.profile copied as a folder).
gentoo_prepare_portage() {
    REPO=$(portageq get_repo_path / gentoo 2>/dev/null || true)
    [ -n "$REPO" ] || REPO=/var/db/repos/gentoo
    if [ ! -d "$REPO/profiles" ]; then
        say "No Gentoo repository in $REPO, fetching it (emerge-webrsync)"
        mkdir -p "$REPO"
        emerge-webrsync || emerge --sync
    fi
    [ -d "$REPO/profiles" ] || die "the Gentoo repository could not be fetched into $REPO"

    MP=/etc/portage/make.profile
    if [ ! -L "$MP" ] || [ ! -e "$MP" ]; then
        # The live ISO's copied profile folder usually names the profile it came from
        WANT=""
        if [ -f "$MP/parent" ]; then
            WANT=$(head -n 1 "$MP/parent" | sed -e 's#^gentoo:##' -e 's#.*/profiles/##')
        fi
        if [ -z "$WANT" ] || [ ! -d "$REPO/profiles/$WANT" ]; then
            GARCH=$(portageq envvar ARCH 2>/dev/null || echo amd64)
            LIST=$(eselect profile list 2>/dev/null | grep '(stable)' | awk '{print $2}')
            WANT=""
            for suffix in /desktop/plasma /desktop ""; do
                WANT=$(echo "$LIST" | grep -E "^default/linux/$GARCH/[0-9.]+$suffix\$" | sort -V | tail -n 1)
                [ -n "$WANT" ] && break
            done
        fi
        [ -n "$WANT" ] || die "no Gentoo profile found - pick one with: eselect profile list / set"
        if [ -e "$MP" ] || [ -L "$MP" ]; then
            rm -rf "$MP.livecd-backup"
            mv "$MP" "$MP.livecd-backup"
        fi
        say "Setting the Gentoo profile to $WANT"
        eselect profile set "$WANT"
    fi
}

# Build tools (compiler, make) + the host tools the ISO build runs, per distro
install_host_tools() {
    say "Installing build and host tools for $DISTRO_NAME"
    case "$FAMILY" in
        debian)
            apt-get update
            apt-get install -y g++ make squashfs-tools xorriso mtools dosfstools curl tar unzip \
                live-boot live-boot-initramfs-tools ;;
        arch)
            pacman -S --needed --noconfirm gcc make squashfs-tools libisoburn mtools dosfstools tar unzip \
                erofs-utils mkinitcpio device-mapper gnupg ;;
        fedora)
            dnf install -y gcc-c++ make squashfs-tools xorriso mtools dosfstools curl tar unzip dracut-live ;;
        opensuse)
            zypper --non-interactive install gcc-c++ make squashfs xorriso mtools dosfstools curl tar unzip ;;
        alpine)
            apk add build-base squashfs-tools xorriso mtools dosfstools curl tar ;;
        gentoo)
            gentoo_prepare_portage
            emerge --noreplace --ask=n sys-fs/squashfs-tools dev-libs/libisoburn sys-fs/mtools sys-fs/dosfstools \
                net-misc/curl app-arch/unzip sys-kernel/dracut sys-fs/lvm2 ;;
    esac
}

# erofs images need mkfs.erofs; older releases do not package it, so it is optional
install_erofs_utils() {
    command -v mkfs.erofs >/dev/null 2>&1 && return 0
    say "Installing erofs-utils (optional: only needed for erofs images)"
    case "$FAMILY" in
        debian)   apt-get install -y erofs-utils ;;
        fedora)   dnf install -y erofs-utils ;;
        opensuse) zypper --non-interactive install erofs-utils ;;
        alpine)   apk add erofs-utils ;;
        gentoo)   emerge --noreplace --ask=n sys-fs/erofs-utils ;;
    esac || say "erofs-utils is not available here - only the squashfs image format will work"
}

# Alpine: the tools the live ISO's "Install Cloned System To Disk" needs (they go into the clone)
install_alpine_installer_tools() {
    say "Installing the Alpine installer tools"
    PKGS="sfdisk e2fsprogs btrfs-progs dosfstools squashfs-tools grub efibootmgr"
    [ "$(uname -m)" = "x86_64" ] && PKGS="$PKGS grub-bios grub-efi"
    # shellcheck disable=SC2086
    apk add $PKGS
}

# The claudemods branding says "claudemods Distro": name it after this distro instead (window
# title, welcome text, and the boot entry of the installed system). $1 = branding.desc
brand_for_distro() {
    [ -f "$1" ] || return 0
    PN=$(printf '%s' "$DISTRO_NAME" | tr -d '"#\\')
    SN=$(printf '%s' "${NAME:-$DISTRO_NAME}" | tr -d '"#\\')
    VER=$(printf '%s' "${VERSION_ID:-${VERSION:-1}}" | tr -d '"#\\')
    sed -i -E \
        -e "s#^([[:space:]]*(productName|versionedName|shortVersionedName)[[:space:]]*:).*#\1 $PN#" \
        -e "s#^([[:space:]]*(shortProductName|bootloaderEntryName)[[:space:]]*:).*#\1 $SN#" \
        -e "s#^([[:space:]]*(version|shortVersion)[[:space:]]*:).*#\1 $VER#" "$1"
}

# Unpacks the claudemods Calamares zips from resources/ into $1
unpack_calamares_zips() {
    command -v unzip >/dev/null 2>&1 || die "unzip is not installed"
    rm -rf "$1"
    mkdir -p "$1"
    unzip -o -q "$SRC_DIR/resources/calamares-files.zip" -d "$1"
    unzip -o -q "$SRC_DIR/resources/claudemods.zip" -d "$1/calamares-files"
}

# Arch: Calamares from the claudemods zips (port of cmiadvanced extrainstalls.sh)
# $1 work dir with the unpacked zips, $2 = 1 for btrfs zstd:22, $3 = 1 for an erofs image
install_calamares_arch() {
    cd "$1/calamares-files"
    say "Installing Calamares packages"
    pacman -U --noconfirm --overwrite="*" calamares-*.pkg.tar* ckbcomp-*.pkg.tar*
    say "Copying Calamares configuration and claudemods branding"
    cp -r calamares /etc/
    mkdir -p /usr/share/calamares/branding
    cp -r claudemods /usr/share/calamares/branding/
    brand_for_distro /usr/share/calamares/branding/claudemods/branding.desc
    unzip -o -q extras.zip -d /
    if [ "$2" = "1" ]; then
        say "Applying btrfs level 22 compression (compress=zstd:22)"
        cp -f mount.conf /usr/share/calamares/modules/
    fi
    if [ "$3" = "1" ]; then
        say "Applying the erofs cloned system image configuration"
        cp -r erofs/* /
    fi
    rm -f /usr/share/applications/calamares.desktop
    cp -f calamares.desktop /usr/share/applications/
    rm -rf /usr/share/calamares/branding/manjaro
    # initcpio preset = the running kernel's package (linux-cachyos, linux, linux-zen, ...)
    PKGBASE=$(cat "/usr/lib/modules/$(uname -r)/pkgbase" 2>/dev/null || echo linux-cachyos)
    for f in /etc/calamares/modules/initcpio.conf /usr/share/calamares/modules/initcpio.conf; do
        if [ -f "$f" ]; then sed -i "s/^kernel:.*/kernel: $PKGBASE/" "$f"; fi
    done
    cd "$SRC_DIR"
    say "Calamares installed (kernel preset: $PKGBASE)"
}

# Debian, Fedora, openSUSE, Gentoo: the distro's Calamares package, configured for a full-system
# clone (no users page - the clone keeps its users) with the claudemods branding.
# $1 work dir with the unpacked zips, $2 = 1 for btrfs zstd:22, $3 = 1 for an erofs image
install_calamares_generic() {
    ZIPS="$1/calamares-files"
    BTRFS="$2"
    SOURCEFS=squashfs
    [ "$3" = "1" ] && SOURCEFS=erofs
    ARCH=$(uname -m)
    # Where the family's live initramfs leaves the image during the live session
    case "$FAMILY" in
        debian) UNPACK=/run/live/medium/live/filesystem.squashfs ;;   # live-boot
        *)      UNPACK=/run/initramfs/live/LiveOS/squashfs.img ;;     # dracut dmsquash-live
    esac

    say "Installing Calamares, GRUB and rsync for $DISTRO_NAME"
    case "$FAMILY" in
        debian)
            PKGS="calamares rsync efibootmgr dosfstools"
            [ "$ARCH" = "x86_64" ] && PKGS="$PKGS grub-pc-bin grub-efi-amd64-bin"
            # shellcheck disable=SC2086
            apt-get install -y $PKGS || die "Calamares install failed - enable the repository that has it (e.g. Ubuntu universe)"
            GRUB=grub; INITRAMFS=initramfs ;;
        fedora)
            PKGS="calamares rsync efibootmgr dosfstools"
            [ "$ARCH" = "x86_64" ] && PKGS="$PKGS grub2-pc-modules grub2-efi-x64-modules"
            # shellcheck disable=SC2086
            dnf install -y $PKGS || die "Calamares install failed"
            GRUB=grub2; INITRAMFS=dracut ;;
        opensuse)
            PKGS="calamares rsync efibootmgr dosfstools"
            [ "$ARCH" = "x86_64" ] && PKGS="$PKGS grub2-i386-pc grub2-x86_64-efi"
            # shellcheck disable=SC2086
            zypper --non-interactive install $PKGS || die "Calamares install failed"
            GRUB=grub2; INITRAMFS=dracut ;;
        gentoo)
            # app-admin/calamares can be keyworded testing-only (~arch): accept it for this one
            # package if the plain emerge is refused. It builds Qt 6 / KF 6: be patient.
            PKGS="app-admin/calamares net-misc/rsync sys-boot/grub sys-boot/efibootmgr sys-fs/dosfstools"
            # shellcheck disable=SC2086
            if ! emerge --noreplace --ask=n $PKGS; then
                KW="~$(portageq envvar ARCH 2>/dev/null || echo amd64)"
                say "Accepting $KW for app-admin/calamares and retrying"
                mkdir -p /etc/portage/package.accept_keywords
                echo "app-admin/calamares $KW" > /etc/portage/package.accept_keywords/claudebackup-calamares
                # shellcheck disable=SC2086
                emerge --noreplace --ask=n --autounmask=y --autounmask-continue=y $PKGS || die "Calamares install failed"
            fi
            GRUB=grub; INITRAMFS=dracut ;;
    esac
    command -v calamares >/dev/null 2>&1 || die "calamares was not installed"

    CFG=/etc/calamares
    MOD=$CFG/modules
    if [ -d "$CFG" ] && [ ! -d "$CFG.orig" ]; then
        say "Keeping the distro's Calamares config in $CFG.orig"
        mv "$CFG" "$CFG.orig"
    fi
    rm -rf "$CFG"
    mkdir -p "$MOD" "$CFG/scripts"

    mkdir -p /usr/share/calamares/branding
    rm -rf /usr/share/calamares/branding/claudemods
    cp -r "$ZIPS/claudemods" /usr/share/calamares/branding/
    brand_for_distro /usr/share/calamares/branding/claudemods/branding.desc

    SYSTEMD=0
    [ -d /run/systemd/system ] && SYSTEMD=1

    # --- settings.conf -------------------------------------------------------
    EXEC="partition mount unpackfs"
    [ "$SYSTEMD" = 1 ] && EXEC="$EXEC machineid"
    EXEC="$EXEC fstab locale keyboard localecfg shellprocess@cblkernel $INITRAMFS grubcfg bootloader umount"
    {
        echo "---"
        echo "modules-search: [ local ]"
        echo "instances:"
        echo "- id: cblkernel"
        echo "  module: shellprocess"
        echo "  config: shellprocess_cblkernel.conf"
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

    # --- module configs ------------------------------------------------------
    FSTYPE=ext4
    [ "$BTRFS" = 1 ] && FSTYPE=btrfs

    cat > "$MOD/unpackfs.conf" << EOF
---
unpack:
    - source: "$UNPACK"
      sourcefs: "$SOURCEFS"
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
    cp -f "$ZIPS/mount.conf" "$MOD/mount.conf"
    if [ "$BTRFS" != 1 ]; then sed -i 's/, compress=zstd:22//' "$MOD/mount.conf"; fi

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

    printf '%s\n' '---' 'overwrite: false' 'keepDistributor: true' > "$MOD/grubcfg.conf"
    printf '%s\n' '---' 'kernel: all' > "$MOD/initramfs.conf"
    echo "---" > "$MOD/dracut.conf"
    printf '%s\n' '---' 'systemd: true' 'dbus: true' 'dbus-symlink: true' > "$MOD/machineid.conf"

    # The image is a bind of "/", so a separate /boot partition (common on Gentoo and Fedora) is
    # empty in the clone: give the installed system a kernel before the initramfs step. Runs on
    # the live system (dontChroot) and finds Calamares' target root itself.
    cat > "$CFG/scripts/cbl-kernel.sh" << 'EOF'
#!/bin/sh
R="${1:-}"
[ -n "$R" ] || R=$(awk '$2 ~ /^\/tmp\/calamares-root-[^\/]*$/ {print $2}' /proc/mounts | head -n 1)
[ -d "$R" ] || { echo "cbl-kernel: target root not found"; exit 1; }
KV=$(uname -r)
if ls "$R"/boot/vmlinuz-* "$R"/boot/kernel-* >/dev/null 2>&1; then
    exit 0
fi
mkdir -p "$R/boot"
for k in "$R/usr/lib/modules/$KV/vmlinuz" "$R/lib/modules/$KV/vmlinuz" \
         /run/live/medium/live/vmlinuz /run/initramfs/live/live/vmlinuz; do
    if [ -f "$k" ]; then
        cp "$k" "$R/boot/vmlinuz-$KV"
        echo "cbl-kernel: /boot was empty, installed $k as /boot/vmlinuz-$KV"
        break
    fi
done
[ -f "$R/boot/vmlinuz-$KV" ] || { echo "cbl-kernel: no kernel found for $KV"; exit 1; }
# Fedora's GRUB boots BLS entries: create the entry for the copied kernel
if [ "@FAMILY@" = fedora ] && [ -x "$R/usr/bin/kernel-install" ]; then
    chroot "$R" kernel-install add "$KV" "/boot/vmlinuz-$KV" || true
fi
exit 0
EOF
    sed -i "s/@FAMILY@/$FAMILY/" "$CFG/scripts/cbl-kernel.sh"
    chmod 755 "$CFG/scripts/cbl-kernel.sh"
    printf '%s\n' '---' 'dontChroot: true' 'timeout: 600' 'script:' '    - "/etc/calamares/scripts/cbl-kernel.sh"' \
        > "$MOD/shellprocess_cblkernel.conf"

    REBOOT="reboot"
    [ "$SYSTEMD" = 1 ] && REBOOT="systemctl -i reboot"
    printf '%s\n' '---' 'restartNowEnabled: true' 'restartNowChecked: false' "restartNowCommand: \"$REBOOT\"" \
        > "$MOD/finished.conf"

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
    say "Calamares configured for $DISTRO_NAME (unpacks $UNPACK as $SOURCEFS, $FSTYPE default, $GRUB)"
}

# The Calamares shortcut asks for the password through pkexec (polkit), so it must be installed
# on every Calamares distro, not only where it happens to be there already
install_pkexec() {
    command -v pkexec >/dev/null 2>&1 && return 0
    say "Installing pkexec (polkit), used by the Calamares shortcut to ask for the password"
    case "$FAMILY" in
        debian)   apt-get install -y pkexec || apt-get install -y policykit-1 ;;
        arch)     pacman -S --needed --noconfirm polkit ;;
        fedora)   dnf install -y polkit ;;
        opensuse) zypper --non-interactive install polkit ;;
        gentoo)   emerge --noreplace --ask=n sys-auth/polkit ;;
    esac || true
    command -v pkexec >/dev/null 2>&1 || die "pkexec could not be installed - the Calamares shortcut needs it"
}

# Starts Calamares as root from the live desktop through pkexec, keeping the session's display
# (pkexec drops it, and Calamares is a GUI), via the root helper below
install_calamares_launcher() {
    install_pkexec

    # Root part: runs right before Calamares, in the live session. The image type chosen when
    # Calamares was installed can differ from the ISO's real image (squashfs / erofs), and Calamares
    # only accepts a type the kernel has loaded - so detect the real type, write it into the
    # unpackfs config and load its module.
    mkdir -p /usr/local/lib/claudebackup
    cat > /usr/local/lib/claudebackup/calamares-root.sh << 'EOF'
#!/bin/sh
for conf in /etc/calamares/modules/unpackfs.conf /usr/share/calamares/modules/unpackfs.conf; do
    [ -f "$conf" ] || continue
    SRC=$(sed -n 's/^[[:space:]]*-[[:space:]]*source:[[:space:]]*"\{0,1\}\([^"]*\)"\{0,1\}[[:space:]]*$/\1/p' "$conf" | head -n 1)
    [ -f "$SRC" ] || continue
    TYPE=$(blkid -p -o value -s TYPE "$SRC" 2>/dev/null || true)
    case "$TYPE" in
        squashfs|erofs)
            echo "claudebackup: $SRC is $TYPE"
            sed -i -E "s#^([[:space:]]*sourcefs:[[:space:]]*).*#\1\"$TYPE\"#" "$conf"
            modprobe "$TYPE" 2>/dev/null || true ;;
    esac
done
exec calamares "$@"
EOF
    chmod 755 /usr/local/lib/claudebackup/calamares-root.sh

    cat > /usr/local/bin/claudebackup-calamares << 'EOF'
#!/bin/sh
# Opens Calamares as root (the "Install ..." desktop shortcut of the claudebackup live ISO).
# Everything goes to a log, and if Calamares does not start a message window says why, so a
# desktop click never fails silently.
LOG="/tmp/claudebackup-calamares-$(id -u).log"
echo "=== $(date): starting Calamares (user $(id -un), session ${XDG_SESSION_TYPE:-?})" >> "$LOG"

# The root helper matches Calamares' unpack settings to the ISO's real image, then starts it
ROOT_HELPER=/usr/local/lib/claudebackup/calamares-root.sh
if [ "$(id -u)" -eq 0 ]; then
    "$ROOT_HELPER" -d "$@" >> "$LOG" 2>&1
elif command -v pkexec >/dev/null 2>&1; then
    # pkexec drops the environment; Calamares is a GUI and needs the session's display
    pkexec env DISPLAY="$DISPLAY" XAUTHORITY="$XAUTHORITY" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" \
        XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" XDG_SESSION_TYPE="$XDG_SESSION_TYPE" \
        "$ROOT_HELPER" -d "$@" >> "$LOG" 2>&1
else
    echo "pkexec is not installed" >> "$LOG"
    false
fi
RC=$?
echo "=== exit code $RC" >> "$LOG"

if [ "$RC" -ne 0 ]; then
    case "$RC" in
        126) MSG="Calamares was not started: the password prompt was cancelled or refused." ;;
        127) MSG="Calamares was not started: a program was not found (calamares or pkexec)." ;;
        *)   MSG="Calamares stopped with an error (exit code $RC)." ;;
    esac
    MSG="$MSG
Details (Calamares debug output): $LOG"
    kdialog --error "$MSG" 2>/dev/null ||
        zenity --error --text="$MSG" 2>/dev/null ||
        notify-send "Install" "$MSG" 2>/dev/null ||
        xmessage "$MSG" 2>/dev/null || true
fi
exit "$RC"
EOF
    chmod 755 /usr/local/bin/claudebackup-calamares
}

# Called through as_root: $1 family, $2 calamares (1/0), $3 btrfs (1/0), $4 erofs (1/0),
# $5 alpine installer tools (1/0)
root_phase() {
    detect_family
    [ "$1" = "$FAMILY" ] || die "the distro detected as root differs ($DISTRO_NAME)"
    install_host_tools
    install_erofs_utils
    if [ "$FAMILY" = alpine ]; then
        if [ "$5" = "1" ]; then install_alpine_installer_tools; fi
    elif [ "$2" = "1" ]; then
        WORK=/tmp/cbl-calamares
        unpack_calamares_zips "$WORK"
        if [ "$FAMILY" = arch ]; then install_calamares_arch "$WORK" "$3" "$4"
        else install_calamares_generic "$WORK" "$3" "$4"; fi
        rm -rf "$WORK"
        install_calamares_launcher
    fi
}

if [ "${1:-}" = "--root-phase" ]; then
    shift
    root_phase "$@"
    exit 0
fi

# =============================================================================
# User phase: questions, root phase, build, install, launch
# =============================================================================
[ -f "$SRC_DIR/Makefile" ] || die "Makefile not found next to build.sh (keep build.sh in dev-release)"
[ "$(uname -s)" = "Linux" ] || die "this project only builds on Linux"
detect_family
say "Detected $DISTRO_NAME"

# yes/no question; $2 is the default (y/n); no terminal = the default
ask() {
    if [ ! -t 0 ]; then [ "$2" = y ]; return; fi
    printf '\033[1;32m%s\033[0m ' "$1"
    read -r reply
    reply=$(printf '%s' "${reply:-$2}" | tr '[:upper:]' '[:lower:]')
    [ "$reply" = y ] || [ "$reply" = yes ]
}
# choice of 1/2; prints 1 or 2 (default 1)
choose() {
    if [ ! -t 0 ]; then echo 1; return; fi
    printf '\033[1;32m%s\033[0m ' "$1" >&2
    read -r reply
    [ "$reply" = 2 ] && echo 2 || echo 1
}

CALAMARES=0; BTRFS=0; EROFS=0; ALPINE_TOOLS=0
if [ "$FAMILY" = alpine ]; then
    say "Calamares does not run on Alpine; the live ISO has 'Install Cloned System To Disk' instead"
    if ask "Install the Alpine installer tools (sfdisk, mkfs, GRUB) so the ISO can install itself? [Y/n]" y; then
        ALPINE_TOOLS=1
    fi
elif ask "Install Calamares, so the live ISO can install itself to a disk? [Y/n]" y; then
    CALAMARES=1
    echo "Calamares configuration for system install:"
    echo "  1) Default configuration (Ext4)"
    echo "  2) Custom configuration with btrfs level 22 compression"
    [ "$(choose "Enter your choice (1 or 2):")" = 2 ] && BTRFS=1
    echo "Cloned system image type (must match Setup > Image Format in the app):"
    echo "  1) Squashfs (default)"
    echo "  2) Erofs"
    [ "$(choose "Enter your choice (1 or 2):")" = 2 ] && EROFS=1
fi

# The user whose Desktop gets the shortcut (also when build.sh itself was started through sudo/doas)
DESK_USER=""
DESK_HOME="$HOME"
if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_USER:-${DOAS_USER:-}}" ]; then
    DESK_USER="${SUDO_USER:-$DOAS_USER}"
    DESK_HOME=$(awk -F: -v u="$DESK_USER" '$1 == u { print $6 }' /etc/passwd)
    [ -n "$DESK_HOME" ] || DESK_HOME="/home/$DESK_USER"
fi

# Puts an "Install <distro>" shortcut on the Desktop: it is part of the clone, so the live ISO's
# desktop has it. Calamares distros open Calamares; Alpine opens Konsole running the Alpine installer.
add_desktop_shortcut() {
    DIR=""
    if command -v xdg-user-dir >/dev/null 2>&1; then DIR=$(HOME="$DESK_HOME" xdg-user-dir DESKTOP 2>/dev/null || true); fi
    if [ -z "$DIR" ] || [ "$DIR" = "$DESK_HOME" ]; then DIR="$DESK_HOME/Desktop"; fi
    mkdir -p "$DIR"
    FILE="$DIR/claudebackup-install.desktop"

    if [ "$FAMILY" = alpine ]; then
        COMMENT="Install this cloned system to a disk (claudebackup-linux alpine)"
        ICON=drive-harddisk
        # --hold: the window stays open, so the installer's output (or an error) can be read
        if command -v konsole >/dev/null 2>&1; then EXEC="konsole --hold -e $TARGET alpine"; IN_TERMINAL=false
        else EXEC="$TARGET alpine"; IN_TERMINAL=true; fi          # no Konsole: the default terminal
    else
        COMMENT="Install this cloned system to a disk (Calamares)"
        ICON=calamares
        EXEC=/usr/local/bin/claudebackup-calamares
        IN_TERMINAL=false
    fi

    cat > "$FILE" << EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=Install $DISTRO_NAME
Comment=$COMMENT
Exec=$EXEC
Icon=$ICON
Terminal=$IN_TERMINAL
Categories=System;
EOF
    chmod 755 "$FILE"
    # GNOME only starts desktop launchers that are marked trusted
    if command -v gio >/dev/null 2>&1; then gio set "$FILE" metadata::trusted true 2>/dev/null || true; fi
    if [ -n "$DESK_USER" ]; then chown "$DESK_USER" "$DIR" "$FILE" 2>/dev/null || true; fi
    say "Added 'Install $DISTRO_NAME' to $DIR"
}

say "Installing packages as root"
as_root sh "$SELF" --root-phase "$FAMILY" "$CALAMARES" "$BTRFS" "$EROFS" "$ALPINE_TOOLS"

say "Building with make"
make -C "$SRC_DIR" -j"$(nproc 2>/dev/null || echo 1)"

say "Installing $TARGET"
as_root make -C "$SRC_DIR" install
command -v "$TARGET" >/dev/null 2>&1 || die "$TARGET is not on PATH after install"

if [ "$CALAMARES" = 1 ] || [ "$ALPINE_TOOLS" = 1 ]; then add_desktop_shortcut; fi

say "Launching $TARGET"
exec "$TARGET"
