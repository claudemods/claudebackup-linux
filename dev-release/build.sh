#!/usr/bin/env bash
# build.sh - detect the distro, install what claudebackup-linux needs, build it
# with make, install the compiled binary, then launch it.
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/dev-release"
TARGET="claudebackup-linux"

say() { printf '\033[1;31m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$*" >&2; exit 1; }

[ -d "$SRC_DIR" ] || die "dev-release folder not found next to build.sh"
[ "$(uname -s)" = "Linux" ] || die "this project only builds on Linux"

# ---- privilege helper (sudo, doas, run0, pkexec or su) ----------------------
as_root() {
    if [ "$(id -u)" -eq 0 ]; then "$@"
    elif command -v sudo   >/dev/null; then sudo "$@"
    elif command -v doas   >/dev/null; then doas "$@"
    elif command -v run0   >/dev/null; then run0 "$@"
    elif command -v pkexec >/dev/null; then pkexec "$@"
    else su -c "$(printf '%q ' "$@")"
    fi
}

# ---- distro detection (ID, then each ID_LIKE entry, as src/distro.h does) ---
[ -r /etc/os-release ] || die "/etc/os-release not found"
# shellcheck disable=SC1091
. /etc/os-release
FAMILY=""
for id in "${ID:-}" ${ID_LIKE:-}; do
    case "${id,,}" in
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
say "Detected ${PRETTY_NAME:-$ID} (family: $FAMILY)"

# ---- install build dependencies + host tools --------------------------------
say "Installing dependencies"
case "$FAMILY" in
    debian)
        as_root apt-get update
        as_root apt-get install -y g++ make squashfs-tools xorriso mtools dosfstools curl tar ;;
    arch)
        as_root pacman -S --needed --noconfirm gcc make squashfs-tools xorriso mtools dosfstools \
            tar unzip erofs-utils mkinitcpio device-mapper gnupg ;;
    fedora)
        as_root dnf install -y gcc-c++ make squashfs-tools xorriso mtools dosfstools curl tar ;;
    opensuse)
        as_root zypper --non-interactive install gcc-c++ make squashfs xorriso mtools dosfstools curl tar ;;
    alpine)
        as_root apk add --no-cache build-base make squashfs-tools xorriso mtools dosfstools curl tar ;;
    gentoo)
        as_root emerge --noreplace --quiet sys-devel/gcc sys-devel/make sys-fs/squashfs-tools \
            dev-libs/libisoburn sys-fs/mtools sys-fs/dosfstools net-misc/curl app-arch/tar sys-fs/lvm2 ;;
esac

# ---- build with make --------------------------------------------------------
say "Building with make"
make -C "$SRC_DIR" -j"$(nproc 2>/dev/null || echo 1)"

# ---- install the compiled binary --------------------------------------------
say "Installing $TARGET"
as_root make -C "$SRC_DIR" install

command -v "$TARGET" >/dev/null || die "$TARGET is not on PATH after install"

# ---- launch -----------------------------------------------------------------
say "Launching $TARGET"
exec "$TARGET"
