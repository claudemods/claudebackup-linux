#!/bin/bash
# Builds and installs claudebackup linux (Qt6) on Arch / CachyOS.
set -e

cd "$(dirname "$0")"

sudo pacman -S --needed --noconfirm qt6-base cmake gcc make unzip

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

sudo install -Dm755 build/claudebackup /usr/bin/claudebackup
echo "Installed /usr/bin/claudebackup"
