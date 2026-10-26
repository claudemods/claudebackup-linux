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
  <a href="https://www.gentoo.org" target="_blank"><img src="https://img.shields.io/badge/DISTRO-Gentoo-54487A?style=for-the-badge&logo=gentoo" /></a>

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
| <img src="https://img.shields.io/badge/-Gentoo-54487A?style=flat&logo=gentoo&logoColor=white" /> **Gentoo** | Gentoo, Calculate, Funtoo, Pentoo | dracut (dmsquash-live) | 🧪 **Beta** |

Gentoo kernels need `CONFIG_SQUASHFS`, `CONFIG_OVERLAY_FS`, `CONFIG_ISO9660_FS` and `CONFIG_BLK_DEV_LOOP` (`=m` or `=y`); the build stops and names any that are missing. `gentoo-kernel` / `gentoo-kernel-bin` have them all.

> **Beta:** all families are implemented but still being tested. Please open an issue with your distro and the log (`/var/log/claudebackup-linux.log`) if a build fails.

---

# 🚀 Claudebackup-Linux Project Roadmap 🚀

## Stage 1 📚 ✅
- [x] Gather knowledge 📖
- [x] Make a new project called **claudebackup-linux** 🔧 from existing functions in **claudemods-multi-iso-konsole-script** ⚙️

## Stage 2 squashfs support 🐧📍✅
Fully support Arch 🟦, Debian 🔴, Ubuntu 🟠, Fedora 🔵, openSUSE 🟢, Alpine 🏔️ and Gentoo 🟣 systems
- [x] Arch 🟦 — [x] boots from tests — [x] installation support ✅
- [x] Debian 🔴 — [x] boots from tests — [x] installation support ✅
- [x] Ubuntu/KDE Neon 🟠 — [x] boots from tests — [x] installation support ✅
- [x] Fedora 🔵 — [x] boots from tests — [x] installation support ✅
- [x] openSUSE 🟢 — [x] boots from tests — [x] installation support ✅
- [x] Alpine 🏔️ — [x] boots from tests — [x] installation support ✅
- [x] Gentoo 🟣 — [x] boots from tests — [x] installation support ✅

## Stage 3 erofs support 🐧CURRENTLY HERE 📍
Fully support Arch 🟦, Debian 🔴, Ubuntu 🟠, Fedora 🔵, openSUSE 🟢, Alpine 🏔️ and Gentoo 🟣 systems
- [x] Arch 🟦 — [x] boots from tests — [x] installation support ✅
- [x] Debian 🔴 — [x] boots from tests — [x] installation support ✅
- [ ] Ubuntu/KDE Neon 🟠 — [ ] boots from tests — [ ] installation support
- [ ] Fedora 🔵 — [ ] boots from tests — [ ] installation support
- [ ] openSUSE 🟢 — [ ] boots from tests — [ ] installation support
- [ ] Alpine 🏔️ — [ ] boots from tests — [ ] installation support
- [ ] Gentoo 🟣 — [ ] boots from tests — [ ] installation support

## Stage 4 🎨
- [ ] Once script fully works as intended 💯, upgrade **claudebackup-linux-qt6** 🖥️ — the GUI version of what will be this finished script ✨
