/* =============================================================================
 * Chimera Operating System — App Bundle Runtime API
 * kernel/include/kernel/chimera_bundle.h
 * ============================================================================= */

#pragma once
#ifndef CHIMERA_BUNDLE_H
#define CHIMERA_BUNDLE_H

#include <kernel/chimera_types.h>

// maximum paths for bundle parsing
#define CHIMERA_BUNDLE_PATH_MAX  1024
#define CHIMERA_BUNDLE_ID_MAX    256
#define CHIMERA_BUNDLE_NAME_MAX  128

/**
 * Represents the parsed contents of a macOS-style .app bundle's Info.plist.
 * Used by launchd-chimera to determine how to spawn the executable.
 */
typedef struct chimera_bundle_info {
    char bundle_path[CHIMERA_BUNDLE_PATH_MAX];    // e.g. /Applications/Terminal.app
    char executable_path[CHIMERA_BUNDLE_PATH_MAX];// e.g. /Applications/Terminal.app/Contents/MacOS/Terminal
    char bundle_identifier[CHIMERA_BUNDLE_ID_MAX];// e.g. com.xiu.Terminal
    char bundle_name[CHIMERA_BUNDLE_NAME_MAX];    // e.g. Terminal
    
    u32  is_background_daemon;
    
    // version info
    u32  version_major;
    u32  version_minor;
} chimera_bundle_info_t;

// system-level wrapper API for user-space to read bundles
#ifdef CHIMERA_USERSPACE
int chimera_bundle_read_info(const char *app_path, chimera_bundle_info_t *out_info);
#endif

#endif /* CHIMERA_BUNDLE_H */
