/* Standard RFC 1320 MD4 implementation for libcrypt */
#include <string.h>
#include "include/md4.h"

#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))

#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define FF(a, b, c, d, x, s) { \
    (a) += F((b), (c), (d)) + (x); \
    (a) = ROTL((a), (s)); \
}
#define GG(a, b, c, d, x, s) { \
    (a) += G((b), (c), (d)) + (x) + 0x5a827999; \
    (a) = ROTL((a), (s)); \
}
#define HH(a, b, c, d, x, s) { \
    (a) += H((b), (c), (d)) + (x) + 0x6ed9eba1; \
    (a) = ROTL((a), (s)); \
}

static void md4_transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t x[16];
    int i;
    for (i = 0; i < 16; i++) {
        x[i] = (uint32_t)block[i*4] | ((uint32_t)block[i*4+1] << 8) |
               ((uint32_t)block[i*4+2] << 16) | ((uint32_t)block[i*4+3] << 24);
    }

    FF(a, b, c, d, x[ 0],  3); FF(d, a, b, c, x[ 1],  7);
    FF(c, d, a, b, x[ 2], 11); FF(b, c, d, a, x[ 3], 19);
    FF(a, b, c, d, x[ 4],  3); FF(d, a, b, c, x[ 5],  7);
    FF(c, d, a, b, x[ 6], 11); FF(b, c, d, a, x[ 7], 19);
    FF(a, b, c, d, x[ 8],  3); FF(d, a, b, c, x[ 9],  7);
    FF(c, d, a, b, x[10], 11); FF(b, c, d, a, x[11], 19);
    FF(a, b, c, d, x[12],  3); FF(d, a, b, c, x[13],  7);
    FF(c, d, a, b, x[14], 11); FF(b, c, d, a, x[15], 19);

    GG(a, b, c, d, x[ 0],  3); GG(d, a, b, c, x[ 4],  5);
    GG(c, d, a, b, x[ 8],  9); GG(b, c, d, a, x[12], 13);
    GG(a, b, c, d, x[ 1],  3); GG(d, a, b, c, x[ 5],  5);
    GG(c, d, a, b, x[ 9],  9); GG(b, c, d, a, x[13], 13);
    GG(a, b, c, d, x[ 2],  3); GG(d, a, b, c, x[ 6],  5);
    GG(c, d, a, b, x[10],  9); GG(b, c, d, a, x[14], 13);
    GG(a, b, c, d, x[ 3],  3); GG(d, a, b, c, x[ 7],  5);
    GG(c, d, a, b, x[11],  9); GG(b, c, d, a, x[15], 13);

    HH(a, b, c, d, x[ 0],  3); HH(d, a, b, c, x[ 8],  9);
    HH(c, d, a, b, x[ 4], 11); HH(b, c, d, a, x[12], 15);
    HH(a, b, c, d, x[ 2],  3); HH(d, a, b, c, x[10],  9);
    HH(c, d, a, b, x[ 6], 11); HH(b, c, d, a, x[14], 15);
    HH(a, b, c, d, x[ 1],  3); HH(d, a, b, c, x[ 9],  9);
    HH(c, d, a, b, x[ 5], 11); HH(b, c, d, a, x[13], 15);
    HH(a, b, c, d, x[ 3],  3); HH(d, a, b, c, x[11],  9);
    HH(c, d, a, b, x[ 7], 11); HH(b, c, d, a, x[15], 15);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void MD4Init(MD4_CTX *ctx) {
    ctx->count[0] = ctx->count[1] = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

void MD4Update(MD4_CTX *ctx, const unsigned char *input, size_t len) {
    size_t i, index, partLen;

    index = (size_t)((ctx->count[0] >> 3) & 0x3F);
    if ((ctx->count[0] += ((uint32_t)len << 3)) < ((uint32_t)len << 3))
        ctx->count[1]++;
    ctx->count[1] += ((uint32_t)len >> 29);

    partLen = 64 - index;
    if (len >= partLen) {
        memcpy(&ctx->buffer[index], input, partLen);
        md4_transform(ctx->state, ctx->buffer);
        for (i = partLen; i + 63 < len; i += 64)
            md4_transform(ctx->state, &input[i]);
        index = 0;
    } else {
        i = 0;
    }
    memcpy(&ctx->buffer[index], &input[i], len - i);
}

void MD4Final(unsigned char digest[16], MD4_CTX *ctx) {
    static const uint8_t PADDING[64] = { 0x80, 0 };
    uint8_t bits[8];
    size_t index, padLen;
    int i;

    for (i = 0; i < 4; i++) {
        bits[i] = (uint8_t)((ctx->count[0] >> (i * 8)) & 0xFF);
        bits[i + 4] = (uint8_t)((ctx->count[1] >> (i * 8)) & 0xFF);
    }

    index = (size_t)((ctx->count[0] >> 3) & 0x3F);
    padLen = (index < 56) ? (56 - index) : (120 - index);
    MD4Update(ctx, PADDING, padLen);
    MD4Update(ctx, bits, 8);

    for (i = 0; i < 4; i++) {
        digest[i*4]   = (uint8_t)(ctx->state[i] & 0xFF);
        digest[i*4+1] = (uint8_t)((ctx->state[i] >> 8) & 0xFF);
        digest[i*4+2] = (uint8_t)((ctx->state[i] >> 16) & 0xFF);
        digest[i*4+3] = (uint8_t)((ctx->state[i] >> 24) & 0xFF);
    }
    memset(ctx, 0, sizeof(*ctx));
}
