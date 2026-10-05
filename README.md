<p align="center">
<img src="https://i.postimg.cc/JhMRf2RZ/claudemods-03-17-2025.gif">

<div align="center">

  <a href="https://www.linux.org" target="_blank"><img src="https://img.shields.io/badge/OS-Linux-e06c75?style=for-the-badge&logo=linux" /></a>

  <a href="https://archlinux.org" target="_blank"><img src="https://img.shields.io/badge/DISTRO-Arch-56b6c2?style=for-the-badge&logo=arch-linux" /></a>
  <a href="https://ubuntu.com/" target="_blank"><img src="https://img.shields.io/badge/DISTRO-Ubuntu-E95420?style=for-the-badge&logo=Ubuntu" /></a>
  <a href="https://www.debian.org" target="_blank"><img src="https://img.shields.io/badge/DISTRO-Debian-CE0058?style=for-the-badge&logo=Debian" /></a>

  <a href="https://chat.deepseek.com/" target="_blank">
    <img src="https://img.shields.io/badge/Built_Using-DeepSeek-4D6BFE?style=for-the-badge&logo=deepseek&logoColor=4D6BFE" alt="Built Using DeepSeek">
    <img src="https://i.postimg.cc/ydBbyvRt/Deepseek.jpg" alt="DeepSeek Logo" style="height: 30px; vertical-align: middle;">
  </a>

  ## [ Guide ](https://github.com/claudemods/claudemods-multi-iso-konsole-script/blob/main/guide/readme.md)

  ## [ Support Me ](https://www.paypal.com/paypalme/claudemods?country.x=GB&locale)

</div>

<div align="center">

  [![Ko-Fi](https://img.shields.io/badge/Ko--fi-F16061?style=for-the-badge&label=claudemods&color=3399FF&Linux&logo=ko-fi&logoColor=white)](https://ko-fi.com/claudemods)
  [![GitHub Sponsors](https://img.shields.io/badge/sponsor-30363D?style=for-the-badge&label=claudemods&color=A836FF&logo=GitHub-Sponsors&logoColor=#white)](https://github.com/sponsors/claudemods)

</div>

<div align="center">
  <h5 align="center">Hello, welcome to claudebackup linux - a Qt6 Multi ISO Creator Written in C++!</h5>
</div>

<p align="center"> Sailing the 7 seas like Penguin's Eggs Remastersys, Refracta, Systemback and father Knoppix! </p>

<div align="center">

## 🖥️ claudebackup linux Beta v3.0 05-10-2026 🚀

<p align="center">Clone your running system into a bootable live ISO - now with a full Qt6 desktop app</p>

<p align="center">For UEFI Ext4/Btrfs Systems Without Separate Swap Or Home</p>

---

![C++](https://img.shields.io/badge/C++-20-blue) ![Qt](https://img.shields.io/badge/Qt-6-41CD52) ![License](https://img.shields.io/badge/license-MIT-green)

</div>

## 🐧 Supported Distributions

| Distro | Status | Readme |
|---|---|---|
| <img src="https://img.shields.io/badge/-Arch-56b6c2?style=flat&logo=arch-linux&logoColor=white" /> **Arch / CachyOS** | ✅ **Available** | [Arch Readme](https://github.com/claudemods/claudebackup-linux/tree/main/arch/release) |
| <img src="https://img.shields.io/badge/-Ubuntu-E95420?style=flat&logo=ubuntu&logoColor=white" /> **Ubuntu** | ⏸️ **On Hold** | Coming later |
| <img src="https://img.shields.io/badge/-Debian-CE0058?style=flat&logo=debian&logoColor=white" /> **Debian** | ⏸️ **On Hold** | Coming later |

> **Ubuntu and Debian support is on hold.** Arch Linux and CachyOS are the only supported systems for now.

---

## ✨ Features

- 🖥️ **Qt6 desktop app** with a dark blue theme and the red claudemods banner
- 🔐 **Asks for your sudo password once** - kept in memory only while the app is open, never saved to disk
- 🚀 Generate bootable ISOs (BIOS + UEFI hybrid GRUB) with custom configurations
- 🛠️ Installs the latest Calamares with options for ext4 or btrfs, so you can install your system after cloning and booting
- 🛠️ Customizable Calamares branding
- 🤖 Kernel selection and initramfs generation (needed for the ISO)
- 🔄 Uses bind mount to bind the system to a folder before compression
- 🖼️ Create compressed system images, squashfs or erofs (Recommended Option: **Erofs Lzma Level 109**)
- 🗜️ Slow SquashFS compression options with xz/zstd support (zstd supports compression levels 1-22)
- 🗜️ Erofs compression with Lz4hc/Lzma support (Lz4hc levels 1-12, Lzma levels 1-109)
- 🔍 SHA512 checksum generation
- 💽 Install ISO to USB with live `dd` progress
- 📊 Disk usage reporting
- ✏️ Built-in editor for GRUB config, boot text and Calamares config files
- 💬 Pop-up dialogs answer script questions (e.g. the Calamares ext4/btrfs and squashfs/erofs choices)
- ⏱️ Live output log - animated progress bars, cyan command output, auto-expands while a job runs
- ✅ Setup checklist saved to `~/.config/cmi/configuration.txt`
- 📱 Works on small screens and handhelds - menus scroll instead of squashing

## 📋 Requirements

- Linux system (Arch / CachyOS - Ubuntu and Debian on hold)
- GCC compiler (C++20 compatible), CMake, Qt6 (`qt6-base`)
- Root privileges (sudo access)
- Base Arch Packages: `rsync` `squashfs-tools` `xorriso` `grub` `dosfstools` `unzip` `arch-install-scripts` `bash-completion` `erofs-utils` `findutils` `jq` `libarchive` `libisoburn` `lsb-release` `lvm2` `mkinitcpio-archiso` `mkinitcpio-nfs-utils` `mtools` `nbd` `pacman-contrib` `nano` `wget` `parted` `procps-ng` `pv` `python` `sshfs` `syslinux` `xdg-utils` `zsh-completions` `kernel-modules-hook` `virt-manager` `qt6-base` `cmake` `gcc`

## 💾 Installation

### Arch / CachyOS

See the **[Arch Readme](https://github.com/claudemods/claudebackup-linux/tree/main/arch/release)** for the install command.

### Build From Source

The three zip files (`build-image-arch-img.zip`, `calamares-files.zip`, `claudemods.zip`) must sit in the folder **above** `qt6app`:

```
├── build-image-arch-img.zip
├── calamares-files.zip
├── claudemods.zip
├── guide/readme.txt        (optional - built-in guide)
└── qt6app/
    ├── CMakeLists.txt
    ├── build.sh
    └── src/
```

```bash
cd qt6app
bash build.sh
```

This installs the build tools, compiles the app and installs it to `/usr/bin/claudebackup`.

Then run:

```bash
claudebackup
```

### Ubuntu / Debian

⏸️ On hold - coming later.

## 🧭 Usage

1. **Setup Scripts** - work through every step until all boxes in *Current Configuration* are green
2. **Create System Images** - clone your system (zstd / xz squashfs, or lz4hc / lzma erofs)
3. **Generate Bootable Isos** - builds the ISO into your output directory
4. **Install ISO To USB** or **Launch Calamares** to install

## 📁 Files

| Path | Purpose |
|---|---|
| `~/.config/cmi/configuration.txt` | Saved setup settings |
| `~/.config/cmi/build-image-arch-img/` | ISO build tree |
| `~/.config/cmi/build-image-arch-img/LiveOS/rootfs.img` | Cloned system image |
| `~/.config/cmi/calamares-files/` | Calamares packages and config |

## 📝 Notes

- If settings don't save because of permissions, fix the folder's owner once:

```bash
sudo chown -R $USER:$USER ~/.config/cmi
```

---

<div align="center">

**claudemods** - claudemods101@gmail.com

</div>
