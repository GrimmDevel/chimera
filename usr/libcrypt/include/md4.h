#ifndef _MD4_H_
#define _MD4_H_

#include <sys/types.h>
#include <stdint.h>

#define MD4_BLOCK_LENGTH  64
#define MD4_DIGEST_LENGTH 16

typedef struct MD4Context {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t buffer[64];
} MD4_CTX;

void MD4Init(MD4_CTX *ctx);
void MD4Update(MD4_CTX *ctx, const unsigned char *input, size_t len);
void MD4Final(unsigned char digest[16], MD4_CTX *ctx);

#endif
