/* xiu bridge: map FreeBSD <sha512.h> API to ravynos sha2.h SHA2_CTX */
#ifndef _SHA512_H_
#define _SHA512_H_

#include <stdint.h>
#include <stddef.h>
#include "sudo_compat.h"
#include "sha2.h"

/* crypt-sha512.c from ravynos/BSD/lib/libcrypt expects SHA512_CTX and SHA512_* */
typedef SHA2_CTX SHA512_CTX;

#define SHA512_Init(ctx)              SHA512Init(ctx)
#define SHA512_Update(ctx, data, len) SHA512Update((ctx), (const uint8_t *)(data), (len))
#define SHA512_Final(digest, ctx)     SHA512Final((digest), (ctx))

#endif
