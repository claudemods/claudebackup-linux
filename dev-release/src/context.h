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

    std::string mode = "standard";   // standard | clone
    std::string workDir;             // e.g. /home/claudebackup
    std::string isoDir;              // workDir/isodir
    std::string liveRoot;            // workDir/liveroot
    std::string isoPath;             // final .iso file
    std::string symlinkIsoPath;      // set when target-dir differs from workDir
    std::string excludeList;         // generated exclude list for mksquashfs
    std::string stopAfter;           // stop after this step name
    bool debugPlan = false;
    bool isGitHubAction = false;
    bool hasCalamares = false;
    bool ramModeEnabled = true;

    std::string liveUser() const { return settings.user.empty() ? "live" : settings.user; }
};

#endif
