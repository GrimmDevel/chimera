/* =============================================================================
 * XIU Operating System — Apple XNU Boot Parameters Header
 * kernel/include/kernel/boot_args_xnu.h
 * Derived from XNU pexpert/pexpert/boot_args.h
 * =============================================================================
 */

#ifndef XIU_BOOT_ARGS_XNU_H
#define XIU_BOOT_ARGS_XNU_H

#include <kernel/xiu_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_LINE_LENGTH 256

typedef struct {
  u16 Revision;
  u16 Version;
  u64 virtBase;
  u64 physBase;
  u64 memSize;
  u64 topOfKernelData;
  u32 Video;
  u32 machineType;
  void *deviceTreeP;
  u32 deviceTreeLength;
  char commandLine[BOOT_LINE_LENGTH];
  u32 flags;
} boot_args_t;

#define BOOT_FLAGS_VERBOSE 0x0001
#define BOOT_FLAGS_SAFE_MODE 0x0002
#define BOOT_FLAGS_SINGLE_USER 0x0004

#ifdef __cplusplus
}
#endif

#endif /* XIU_BOOT_ARGS_XNU_H */
