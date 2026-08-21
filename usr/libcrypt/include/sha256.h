/* xiu bridge: map FreeBSD <sha256.h> API to ravynos sha2.h SHA2_CTX */
#ifndef _SHA256_H_
#define _SHA256_H_

#include <stdint.h>
#include <stddef.h>
#include "sudo_compat.h"
#include "sha2.h"

/* crypt-sha256.c from ravynos/BSD/lib/libcrypt expects SHA256_CTX and SHA256_* */
typedef SHA2_CTX SHA256_CTX;

#define SHA256_Init(ctx)              SHA256Init(ctx)
#define SHA256_Update(ctx, data, len) SHA256Update((ctx), (const uint8_t *)(data), (len))
#define SHA256_Final(digest, ctx)     SHA256Final((digest), (ctx))

#endif
