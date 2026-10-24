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

<p align="center">Clone your running system into a bootable live ISO - one menu-driven C++ binary for five distro families</p>

<p align="center">A full system clone - your users, passwords, settings and /home are copied exactly as they are, no live account is added</p>

---

![C++](https://img.shields.io/badge/C++-17-blue) ![Status](https://img.shields.io/badge/status-beta-orange) ![License](https://img.shields.io/badge/license-MIT-green)

</div>

## 🐧 Supported Distributions

Detection reads `ID` and `ID_LIKE` from `/etc/os-release`, so derivatives are picked up automatically.

| Family | Distros | Live initramfs | Status |
|---|---|---|---|
| <img src="https://img.shields.io/badge/-Arch-56b6c2?style=flat&logo=arch-linux&logoColor=white" /> **Arch** | Arch, CachyOS, Archcraft, RebornOS, Garuda, EndeavourOS | mkinitcpio + claudemods btrfs/ext4 hooks (embedded zip) | 🧪 **Beta** |
| <img src="https://img.shields.io/badge/-Debian-CE0058?style=flat&logo=debian&logoColor=white" /> **Debian** | Debian, Ubuntu, Mint, Pop, Kali, Devuan, openKylin | initramfs-tools | 🧪 **Beta** |
| <img src="https://img.shields.io/badge/-Fedora-51A2DA?style=flat&logo=fedora&logoColor=white" /> **Fedora** | Fedora, Nobara | dracut | 🧪 **Beta** |
| <img src="https://img.shields.io/badge/-openSUSE-73BA25?style=flat&logo=opensuse&logoColor=white" /> **openSUSE** | Tumbleweed, Slowroll, Leap | dracut | 🧪 **Beta** |
| <img src="https://img.shields.io/badge/-Alpine-0D597F?style=flat&logo=alpine-linux&logoColor=white" /> **Alpine** | Alpine | mkinitfs + custom init | 🧪 **Beta** |

Manjaro and its derivatives (BigLinux, BigCommunity) are not supported.

> **Beta:** all families are implemented but still being tested. Please open an issue with your distro and the log (`/var/log/claudebackup-linux.log`) if a build fails.

---

## ✨ Features

- 🖥️ **Menu-driven terminal app** - red claudemods banner, live clock, disk usage and a `[✓]` setup checklist
- 🐧 **Five distro families in one binary** - the per-distro boot, initramfs and kernel steps are built in, no extra files to install
- 🚀 Generate bootable ISOs (BIOS + UEFI hybrid GRUB/ISOLINUX)
- 📦 **Arch / CachyOS: the claudemods zip method** - the GRUB BIOS/UEFI tree, theme and btrfs/ext4 mkinitcpio hooks from `build-image-arch-img.zip` are embedded in the binary; the system goes into `LiveOS/rootfs.img`
- 🧩 **Arch: Setup > Install Calamares** - claudemods Calamares packages, config and branding from the embedded zips, with an ext4 or btrfs `compress=zstd:22` choice and the kernel preset set to your running kernel (e.g. `linux-cachyos`)
- 🔄 Uses bind and overlay mounts to snapshot the running system before compression - no copy of `/` needed
- 🗜️ SquashFS compression with zstd (levels 1-22), xz or gzip
- 👤 **Full system clone** - keeps your real users, passwords and `/home`; no live user is created and nothing is added to the clone
- 🛠️ Installs the needed tools for your distro from the Setup menu (apt, pacman, dnf, zypper or apk)
- 🧹 Always unmounts everything it mounted - on success, on failure and on Ctrl-C
- 📂 Optional exclude list in `/etc/claudebackup-linux/custom.exclude.list` - empty by default, uncomment lines to leave things out (Docker, Snap, package caches...)
- 💽 Install ISO to USB with live `dd` progress
- 📊 Disk usage reporting
- 🛠️ Launch Calamares from the menu if it is installed
- ✅ Settings saved to `~/.config/claudebackup-linux/configuration.txt`

## 📋 Requirements

- Linux system (any of the families above)
- GCC compiler (C++17 compatible), `make` and `ld` (binutils - embeds the zips from `dev-release/resources/`)
- Root privileges (the app re-launches itself through `sudo`)
- Host tools: `mksquashfs` `xorriso` `mtools` `dosfstools` `curl` `tar` (Arch: `unzip` `mkinitcpio` `device-mapper` `gnupg` instead of `curl`) - the **Setup > Install Needed Tools** option installs them for you
- Internet access once, to download the BIOS/UEFI bootloader files (not needed on Arch)

## 💾 Installation

### Build From Source

```bash
git clone https://github.com/claudemods/claudebackup-linux.git
cd claudebackup-linux/dev-release
make
sudo make install
```

Then run:

```bash
claudebackup-linux
```

Or run it straight from the build folder with `./claudebackup-linux`.

## 🧭 Usage

Use the arrow keys and Enter. The top of every screen shows *Current Configuration*: 🟢 `[✓]` = ready, 🔴 `[ ]` = still to do. The full guide is in the app (**Guide**).

| Step | Menu | What to do |
|---|---|---|
| 1 | **Setup → Install Needed Tools** | Installs `squashfs-tools`, `xorriso`, `mtools`, `dosfstools` and `curl` for your distro |
| 2 | **Setup → Download Bootloaders** | One-off download of the BIOS/UEFI boot files (needs internet) |
| 3 | **Setup → ISO Name / Work Directory / Output Directory** | Optional - blank ISO name is generated for you, blank output directory uses the work directory |
| 4 | **Setup → Compression / Level** | `zstd` (levels 1-22, default 3), `xz` or `gzip` - higher level = smaller but slower |
| 5 | **Setup → RAM Mode** | Optional `toram` boot entry (`copytoram=y` on Arch) |
| 6 | **Setup → Install Calamares** *(Arch)* | Ext4 or btrfs zstd:22 - run before building so the ISO contains it |
| 7 | **Detected System And Build Steps** | Check your distro and family say *Support: yes* and see every step that will run |
| 8 | **Create Bootable ISO** | Mount → bootloaders → EFI image → boot menus → initramfs → kernel → squashfs → xorriso (Arch: mount → claudemods ISO tree → kernels.cfg → initramfs → kernel → LiveOS/rootfs.img → xorriso). Your system is cloned as it is. Everything is unmounted at the end, even on failure or Ctrl+C |
| 9 | **Install ISO To USB** / **Launch Calamares** | Write the ISO to a drive (erases it!) or install from the live session. Test in a VM (UEFI) first |

If a build fails the message names the step - details are in `/var/log/claudebackup-linux.log`.
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
