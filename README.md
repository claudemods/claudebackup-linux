<p align="center">
<img src="https://i.postimg.cc/JhMRf2RZ/claudemods-03-17-2025.gif">

<div align="center">

  <a href="https://www.linux.org" target="_blank"><img src="https://img.shields.io/badge/OS-Linux-e06c75?style=for-the-badge&logo=linux" /></a>

  <a href="https://archlinux.org" target="_blank"><img src="https://img.shields.io/badge/DISTRO-Arch-56b6c2?style=for-the-badge&logo=arch-linux" /></a>
  <a href="https://ubuntu.com/" target="_blank"><img src="https://img.shields.io/badge/DISTRO-Ubuntu-E95420?style=for-the-badge&logo=Ubuntu" /></a>
  <a href="https://www.debian.org" target="_blank"><img src="https://img.shields.io/badge/DISTRO-Debian-CE0058?style=for-the-badge&logo=Debian" /></a>
  <a href="https://getfedora.org" target="_blank"><img src="https://img.shields.io/badge/DISTRO-Fedora-51A2DA?style=for-the-badge&logo=fedora" /></a>
  <a href="https://www.opensuse.org" target="_blank"><img src="https://img.shields.io/badge/DISTRO-openSUSE-73BA25?style=for-the-badge&logo=opensuse" /></a>
  <a href="https://alpinelinux.org" target="_blank"><img src="https://img.shields.io/badge/DISTRO-Alpine-0D597F?style=for-the-badge&logo=alpine-linux" /></a>

  <a href="https://claude.com/claude-code" target="_blank">
    <img src="https://img.shields.io/badge/Built_Using-Claude_Code-D97757?style=for-the-badge&logo=claude&logoColor=white" alt="Built Using Claude Code">
  </a>

  ## [ Guide ](#-usage)

  ## [ Support Me ](https://www.paypal.com/paypalme/claudemods?country.x=GB&locale)

</div>

<div align="center">

  [![Ko-Fi](https://img.shields.io/badge/Ko--fi-F16061?style=for-the-badge&label=claudemods&color=3399FF&Linux&logo=ko-fi&logoColor=white)](https://ko-fi.com/claudemods)
  [![GitHub Sponsors](https://img.shields.io/badge/sponsor-30363D?style=for-the-badge&label=claudemods&color=A836FF&logo=GitHub-Sponsors&logoColor=#white)](https://github.com/sponsors/claudemods)

</div>

<div align="center">
  <h5 align="center">Hello, welcome to claudebackup linux - a Multi Distro ISO Creator Written in C++!</h5>
</div>

<p align="center"> Sailing the 7 seas like Penguin's Eggs Remastersys, Refracta, Systemback and father Knoppix! </p>

<div align="center">

## 🖥️ claudebackup linux Beta v1.0 🚀

<p align="center">Clone your running system into a bootable live ISO - one menu-driven C++ binary for six distro families</p>

<p align="center">Standard mode builds a fresh live user - Clone mode keeps your users and /home</p>

---

![C++](https://img.shields.io/badge/C++-17-blue) ![Status](https://img.shields.io/badge/status-beta-orange) ![License](https://img.shields.io/badge/license-MIT-green)

</div>

## 🐧 Supported Distributions

Detection reads `ID` and `ID_LIKE` from `/etc/os-release`, so derivatives are picked up automatically.

| Family | Distros | Live initramfs | Status |
|---|---|---|---|
| <img src="https://img.shields.io/badge/-Arch-56b6c2?style=flat&logo=arch-linux&logoColor=white" /> **Arch** | Arch, CachyOS, Archcraft, RebornOS | mkinitcpio + archiso | 🧪 **Beta** |
| <img src="https://img.shields.io/badge/-Arch-56b6c2?style=flat&logo=arch-linux&logoColor=white" /> **Arch (dracut)** | Garuda, EndeavourOS | dracut | 🧪 **Beta** |
| <img src="https://img.shields.io/badge/-Manjaro-35BF5C?style=flat&logo=manjaro&logoColor=white" /> **Manjaro** | Manjaro, BigLinux, BigCommunity | mkinitcpio + miso | 🧪 **Beta** |
| <img src="https://img.shields.io/badge/-Debian-CE0058?style=flat&logo=debian&logoColor=white" /> **Debian** | Debian, Ubuntu, Mint, Pop, Kali, Devuan, openKylin | initramfs-tools | 🧪 **Beta** |
| <img src="https://img.shields.io/badge/-Fedora-51A2DA?style=flat&logo=fedora&logoColor=white" /> **Fedora** | Fedora, Nobara | dracut | 🧪 **Beta** |
| <img src="https://img.shields.io/badge/-openSUSE-73BA25?style=flat&logo=opensuse&logoColor=white" /> **openSUSE** | Tumbleweed, Slowroll, Leap | dracut | 🧪 **Beta** |
| <img src="https://img.shields.io/badge/-Alpine-0D597F?style=flat&logo=alpine-linux&logoColor=white" /> **Alpine** | Alpine | mkinitfs + custom init | 🧪 **Beta** |

> **Beta:** all families are implemented but still being tested. Please open an issue with your distro and the log (`/var/log/claudebackup-linux.log`) if a build fails.

---

## ✨ Features

- 🖥️ **Menu-driven terminal app** - red claudemods banner, live clock, disk usage and a `[✓]` setup checklist
- 🐧 **Six distro families in one binary** - the per-distro boot, initramfs and kernel steps are built in, no extra files to install
- 🚀 Generate bootable ISOs (BIOS + UEFI hybrid GRUB/ISOLINUX)
- 🔄 Uses bind and overlay mounts to snapshot the running system before compression - no copy of `/` needed
- 🗜️ SquashFS compression with zstd (levels 1-22), xz or gzip
- 👤 **Standard mode** builds a clean live user, **Clone mode** keeps your real users, passwords and `/home`
- 🖱️ Autologin set up for SDDM, LightDM, GDM, MDM, LXDM, SLiM and greetd
- 🛠️ Installs the needed tools for your distro from the Setup menu (apt, pacman, dnf, zypper or apk)
- 🧹 Always unmounts everything it mounted - on success, on failure and on Ctrl-C
- 📂 Custom exclude list in `/etc/claudebackup-linux/custom.exclude.list` (Docker, Podman, Snap and more by default)
- 💽 Install ISO to USB with live `dd` progress
- 📊 Disk usage reporting
- 🛠️ Launch Calamares from the menu if it is installed
- ✅ Settings saved to `~/.config/claudebackup-linux/configuration.txt`

## 📋 Requirements

- Linux system (any of the families above)
- GCC compiler (C++17 compatible) and `make`
- Root privileges (the app re-launches itself through `sudo`)
- Host tools: `mksquashfs` `xorriso` `mtools` `dosfstools` `curl` `tar` - the **Setup > Install Needed Tools** option installs them for you
- Internet access once, to download the BIOS/UEFI bootloader files

## 💾 Installation

### Build From Source

```bash
git clone https://github.com/claudemods/claudebackup-linux.git
cd claudebackup-linux
make
sudo make install
```

Then run:

```bash
claudebackup-linux
```

Or run it straight from the build folder with `./claudebackup-linux`.

## 🧭 Usage

1. **Setup > Install Needed Tools** - installs `squashfs-tools`, `xorriso`, `mtools`, `dosfstools` and `curl` for your distro
2. **Setup > Download Bootloaders** - one-off download of the BIOS/UEFI boot files
3. **Setup** - set the ISO name, work and output directories, Standard or Clone mode, compression and live user (all optional, sensible defaults are used)
4. **Create Bootable ISO** - builds the ISO into your output directory
5. **Install ISO To USB** or **Launch Calamares** to install

Use the arrow keys and Enter to move around the menu.

## 📁 Files

| Path | Purpose |
|---|---|
| `~/.config/claudebackup-linux/configuration.txt` | Saved setup settings |
| `/etc/claudebackup-linux/custom.exclude.list` | Paths left out of the ISO |
| `/home/claudebackup/` | Default work directory (build tree and ISO) |
| `/var/log/claudebackup-linux.log` | Build log |

## 📝 Notes

- Builds need free space of roughly twice the size of the finished ISO.
- On Fedora, set SELinux to permissive before building: `sudo setenforce 0`.

---

<div align="center">

**claudemods** - claudemods101@gmail.com

</div>
