# claudebackup linux

**claudebackup linux Beta v1.0 05-10-2026**

A Qt6 desktop app for cloning your running Arch Linux / CachyOS system into a bootable live ISO, with a Calamares installer included.

It is the graphical version of the cmiadvanced terminal script, and it runs the same commands.

*Sailing the 7 seas like Penguin's Eggs Remastersys, Refracta, Systemback and father Knoppix!*

---

## Features

- **Clone your running system** into a SquashFS image (zstd or xz) or an EROFS image (lz4hc or lzma).
- **Generate a bootable ISO** with xorriso. It boots on both BIOS and UEFI machines (hybrid GRUB).
- **Write the ISO to a USB drive** with `dd`, with a live progress readout.
- **Calamares installer**: install and configure it with the claudemods branding, then launch it.
- **CmiAdvancedInstaller**: a custom ext4/btrfs installer for squashfs and erofs images.
- **Built-in editor** for the GRUB config, the boot text and the Calamares config files.
- **Asks for your sudo password once.** It is kept in memory only while the app is open, and is never saved to disk.
- **Live output panel.** Command output is shown in cyan, and progress bars animate as they would in a terminal.
- **The output panel expands automatically** while a job runs, so you can see the whole log.
- **Pop-up dialogs answer script questions**, for example the Calamares configuration choices.
- **Setup checklist.** It shows which steps are done and is saved to `~/.config/cmi/configuration.txt`.
- **Dark blue theme** with the red claudemods banner.
- **Fits small screens**, including handhelds; the menus scroll.

---

## Requirements

- **Arch Linux** or **CachyOS**
- Qt6 (`qt6-base`), CMake, GCC
- Tools used by the app:

```bash
sudo pacman -S --needed git rsync squashfs-tools xorriso grub dosfstools unzip nano arch-install-scripts erofs-utils mkinitcpio-archiso mtools parted pv
```

---

## Building

The three zip files that get embedded in the app must sit in the folder **above** `qt6app`:

```
dev-branch/
├── build-image-arch-img.zip
├── calamares-files.zip
├── claudemods.zip
├── guide/readme.txt        (optional - built-in guide)
└── qt6app/
    ├── CMakeLists.txt
    ├── build.sh
    └── src/
```

### Quick build and install

```bash
cd qt6app
bash build.sh
```

This installs the build tools, compiles the app and installs it to `/usr/bin/claudebackup`.

### Manual build

```bash
cd qt6app
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo install -Dm755 build/claudebackup /usr/bin/claudebackup
```

If the zips are somewhere else:

```bash
cmake -S . -B build -DCMI_DATA_DIR=/path/to/folder/with/zips
```

---

## Usage

```bash
claudebackup
```

Enter your sudo password when it asks, then follow the menus.

### 1. Setup Scripts

Work through each step until every box in **Current Configuration** is green:

| Step | What it does |
|---|---|
| Extract Needed Files | Unpacks the build files and Calamares files to `~/.config/cmi`, then runs `extrainstalls.sh` |
| Set Clone Directory | Folder where `clone_system_temp` is created (the bind mount) |
| Set ISO Tag | ISO volume label, e.g. `2026` |
| Set ISO Name | ISO file name, e.g. `claudemods.iso` |
| Set Output Directory | Folder the ISO is saved to |
| Select vmlinuz | The kernel to boot the ISO with |
| mkinitcpio Config | Installs `11-dm-initramfs.rules` |
| Generate mkinitcpio | Builds the live initramfs |
| Edit GRUB Config | `grub.cfg` for the ISO |
| Edit Boot Text | `kernels.cfg` boot menu entries |
| Edit Calamares Branding | `branding.desc` |
| Edit Calamares 1st / 2nd initcpio.conf | Calamares initcpio module configs |

### 2. Create System Images

| Option | Compression |
|---|---|
| Clone Current System (zstd) | SquashFS, zstd level 1-22 |
| Clone Current System (xz) | SquashFS, xz |
| Clone Current System (Lz4hc) | EROFS, lz4hc level 1-12 (fast) |
| Clone Current System (Lzma) | EROFS, lzma level 1-109 |

The image is saved as `~/.config/cmi/build-image-arch-img/LiveOS/rootfs.img`, with a `.sha512` checksum next to it.

### 3. Generate Bootable Isos

Builds the ISO into your output directory and changes its owner to your user.

### Other menu options

- **Guide**: shows the readme.
- **Check Disk Usage**: runs `df -h`.
- **Install ISO To USB**: pick an ISO and a drive, then it is written with `dd`. **This erases the drive.**
- **CmiAdvancedInstaller**: opens the installer in your terminal app.
- **Launch Calamares**: starts the Calamares installer.
- **Update Script**: downloads and runs the latest installer from GitHub.

---

## Files

| Path | Purpose |
|---|---|
| `~/.config/cmi/configuration.txt` | Saved setup settings |
| `~/.config/cmi/build-image-arch-img/` | ISO build tree |
| `~/.config/cmi/build-image-arch-img/LiveOS/rootfs.img` | Cloned system image |
| `~/.config/cmi/calamares-files/` | Calamares packages and config |
| `~/.config/cmi/readme.txt` | Guide (optional) |

---

## Notes

- Commands run as root through sudo. `$USER` and `$HOME` still point to your own account.
- If settings don't save because of permissions, fix the folder's owner once:

```bash
sudo chown -R $USER:$USER ~/.config/cmi
```

- Only Arch Linux and CachyOS are supported.

---

## Credits

**claudemods** - claudemods101@gmail.com
