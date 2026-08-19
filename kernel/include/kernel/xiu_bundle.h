/* =============================================================================
 * XIU Operating System — App Bundle Runtime API
 * kernel/include/kernel/xiu_bundle.h
 * ============================================================================= */

#pragma once
#ifndef XIU_BUNDLE_H
#define XIU_BUNDLE_H

#include <kernel/xiu_types.h>

// maximum paths for bundle parsing
#define XIU_BUNDLE_PATH_MAX  1024
#define XIU_BUNDLE_ID_MAX    256
#define XIU_BUNDLE_NAME_MAX  128

/**
 * Represents the parsed contents of a macOS-style .app bundle's Info.plist.
 * Used by launchd-xiu to determine how to spawn the executable.
 */
typedef struct xiu_bundle_info {
    char bundle_path[XIU_BUNDLE_PATH_MAX];    // e.g. /Applications/Terminal.app
    char executable_path[XIU_BUNDLE_PATH_MAX];// e.g. /Applications/Terminal.app/Contents/MacOS/Terminal
    char bundle_identifier[XIU_BUNDLE_ID_MAX];// e.g. com.xiu.Terminal
    char bundle_name[XIU_BUNDLE_NAME_MAX];    // e.g. Terminal
    
    u32  is_background_daemon;
    
    // version info
    u32  version_major;
    u32  version_minor;
} xiu_bundle_info_t;

// system-level wrapper API for user-space to read bundles
#ifdef XIU_USERSPACE
int xiu_bundle_read_info(const char *app_path, xiu_bundle_info_t *out_info);
#endif

#endif /* XIU_BUNDLE_H */
