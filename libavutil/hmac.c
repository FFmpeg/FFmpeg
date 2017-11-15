/*
 * Copyright (C) 2012 Martin Storsjo
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "attributes.h"
#include "hmac.h"
#include "md5.h"
#include "sha.h"
#include "sha512.h"
#include "mem.h"
#include "version.h"

#define MAX_HASHLEN 64
#define MAX_BLOCKLEN 128

typedef void (*hmac_final)(void *ctx, uint8_t *dst);
#if FF_API_CRYPTO_SIZE_T
typedef void (*hmac_update)(void *ctx, const uint8_t *src, int len);
#else
typedef void (*hmac_update)(void *ctx, const uint8_t *src, size_t len);
#endif
typedef void (*hmac_init)(void *ctx);

struct AVHMAC {
    void *hash;
    int blocklen, hashlen;
    hmac_final  final;
    hmac_update update;
    hmac_init   init;
    uint8_t key[MAX_BLOCKLEN];
    int keylen;
};

#define DEFINE_SHA(bits)                           \
static av_cold void sha ## bits ##_init(void *ctx) \
{                                                  \
    av_sha_init(ctx, bits);                        \
}

#define DEFINE_SHA512(bits)                        \
static av_cold void sha ## bits ##_init(void *ctx) \
{                                                  \
    av_sha512_init(ctx, bits);                     \
}

DEFINE_SHA(160)
DEFINE_SHA(224)
DEFINE_SHA(256)
DEFINE_SHA512(384)
DEFINE_SHA512(512)

AVHMAC *av_hmac_alloc(enum AVHMACType type)
{
    AVHMAC *c = av_mallocz(sizeof(*c));
    if (!c)
        return NULL;
    switch (type) {
    case AV_HMAC_MD5:
        c->blocklen = 64;
        c->hashlen  = 16;
        c->init     = (hmac_init) av_md5_init;
        c->update   = (hmac_update) av_md5_update;
        c->final    = (hmac_final) av_md5_final;
        c->hash     = av_md5_alloc();
        break;
    case AV_HMAC_SHA1:
        c->blocklen = 64;
        c->hashlen  = 20;
        c->init     = sha160_init;
        c->update   = (hmac_update) av_sha_update;
        c->final    = (hmac_final) av_sha_final;
        c->hash     = av_sha_alloc();
        break;
    case AV_HMAC_SHA224:
        c->blocklen = 64;
        c->hashlen  = 28;
        c->init     = sha224_init;
        c->update   = (hmac_update) av_sha_update;
        c->final    = (hmac_final) av_sha_final;
        c->hash     = av_sha_alloc();
        break;
    case AV_HMAC_SHA256:
        c->blocklen = 64;
        c->hashlen  = 32;
        c->init     = sha256_init;
        c->update   = (hmac_update) av_sha_update;
        c->final    = (hmac_final) av_sha_final;
        c->hash     = av_sha_alloc();
        break;
    case AV_HMAC_SHA384:
        c->blocklen = 128;
        c->hashlen  = 48;
        c->init     = sha384_init;
        c->update   = (hmac_update) av_sha512_update;
        c->final    = (hmac_final) av_sha512_final;
        c->hash     = av_sha512_alloc();
        break;
    case AV_HMAC_SHA512:
        c->blocklen = 128;
        c->hashlen  = 64;
        c->init     = sha512_init;
        c->update   = (hmac_update) av_sha512_update;
        c->final    = (hmac_final) av_sha512_final;
        c->hash     = av_sha512_alloc();
        break;
    default:
        av_free(c);
        return NULL;
    }
    if (!c->hash) {
        av_free(c);
        return NULL;
    }
    return c;
}

void av_hmac_free(AVHMAC *c)
{
    if (!c)
        return;
    av_freep(&c->hash);
    av_free(c);
}

void av_hmac_init(AVHMAC *c, const uint8_t *key, unsigned int keylen)
{
    int i;
    uint8_t block[MAX_BLOCKLEN];
    if (keylen > c->blocklen) {
        c->init(c->hash);
        c->update(c->hash, key, keylen);
        c->final(c->hash, c->key);
        c->keylen = c->hashlen;
    } else {
        memcpy(c->key, key, keylen);
        c->keylen = keylen;
    }
    c->init(c->hash);
    for (i = 0; i < c->keylen; i++)
        block[i] = c->key[i] ^ 0x36;
    for (i = c->keylen; i < c->blocklen; i++)
        block[i] = 0x36;
    c->update(c->hash, block, c->blocklen);
}

void av_hmac_update(AVHMAC *c, const uint8_t *data, unsigned int len)
{
    c->update(c->hash, data, len);
}

int av_hmac_final(AVHMAC *c, uint8_t *out, unsigned int outlen)
{
    uint8_t block[MAX_BLOCKLEN];
    int i;
    if (outlen < c->hashlen)
        return AVERROR(EINVAL);
    c->final(c->hash, out);
    c->init(c->hash);
    for (i = 0; i < c->keylen; i++)
        block[i] = c->key[i] ^ 0x5C;
    for (i = c->keylen; i < c->blocklen; i++)
        block[i] = 0x5C;
    c->update(c->hash, block, c->blocklen);
    c->update(c->hash, out, c->hashlen);
    c->final(c->hash, out);
    return c->hashlen;
}

int av_hmac_calc(AVHMAC *c, const uint8_t *data, unsigned int len,
                 const uint8_t *key, unsigned int keylen,
                 uint8_t *out, unsigned int outlen)
{
    av_hmac_init(c, key, keylen);
    av_hmac_update(c, data, len);
    return av_hmac_final(c, out, outlen);
}
