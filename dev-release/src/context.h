#ifndef CONTEXT_H
#define CONTEXT_H

#include "common.h"
#include "distro.h"
#include "config.h"

// Everything a step needs to know about the current remaster run.
// Replaces the Go TemplateContext (Family, DistroID, IsoDir, LiveRoot, ...).
struct Context {
    Distro distro;
    Settings settings;
    std::string workDir;             // e.g. /home/claudebackup
    std::string isoDir;              // workDir/isodir
    std::string liveRoot;            // workDir/liveroot
    std::string isoPath;             // final .iso file
    std::string symlinkIsoPath;      // set when target-dir differs from workDir
    std::string excludeList;         // generated exclude list for mksquashfs
    std::string stopAfter;           // stop after this step name
    std::string cloneDir;            // "/" is bind-mounted here for mksquashfs (Setup > Clone Directory)
    std::string isoTag = "2026";     // Arch: ISO volume label, kernels.cfg label= (cmiadvanced ISO Tag)
    std::string customDir;           // the user's edited copies (Setup > Edit Configs): grub.cfg, kernels.cfg, ...
    bool cleanCache = true;          // leave package caches (/var/cache/pacman/pkg, ...) out of the image
    bool debugPlan = false;
    bool isGitHubAction = false;
    bool ramModeEnabled = true;
};

#endif
