#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"

// Defaults for a run. The menu (menu.h) overrides these from configuration.txt.
struct Settings {
    std::string workDir = DEFAULT_WORK_DIR;
    std::string compressionAlgo = "zstd";
    int compressionLevel = 3;
    bool ramMode = true;
    std::string imageFormat = "squashfs";   // squashfs | erofs
};

#endif
