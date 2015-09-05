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

#include <string.h>

#include "attributes.h"
#include "hmac.h"
#include "md5.h"
#include "sha.h"
#include "sha512.h"
#include "mem.h"

#define MAX_HASHLEN 64
#define MAX_BLOCKLEN 128

struct AVHMAC {
    void *hash;
    int blocklen, hashlen;
    void (*final)(void*, uint8_t*);
    void (*update)(void*, const uint8_t*, int len);
    void (*init)(void*);
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
        c->init     = (void*)av_md5_init;
        c->update   = (void*)av_md5_update;
        c->final    = (void*)av_md5_final;
        c->hash     = av_md5_alloc();
        break;
    case AV_HMAC_SHA1:
        c->blocklen = 64;
        c->hashlen  = 20;
        c->init     = sha160_init;
        c->update   = (void*)av_sha_update;
        c->final    = (void*)av_sha_final;
        c->hash     = av_sha_alloc();
        break;
    case AV_HMAC_SHA224:
        c->blocklen = 64;
        c->hashlen  = 28;
        c->init     = sha224_init;
        c->update   = (void*)av_sha_update;
        c->final    = (void*)av_sha_final;
        c->hash     = av_sha_alloc();
        break;
    case AV_HMAC_SHA256:
        c->blocklen = 64;
        c->hashlen  = 32;
        c->init     = sha256_init;
        c->update   = (void*)av_sha_update;
        c->final    = (void*)av_sha_final;
        c->hash     = av_sha_alloc();
        break;
    case AV_HMAC_SHA384:
        c->blocklen = 128;
        c->hashlen  = 48;
        c->init     = sha384_init;
        c->update   = (void*)av_sha512_update;
        c->final    = (void*)av_sha512_final;
        c->hash     = av_sha512_alloc();
        break;
    case AV_HMAC_SHA512:
        c->blocklen = 128;
        c->hashlen  = 64;
        c->init     = sha512_init;
        c->update   = (void*)av_sha512_update;
        c->final    = (void*)av_sha512_final;
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

#ifdef TEST
#include <stdio.h>

static void test(AVHMAC *hmac, const uint8_t *key, int keylen,
                 const uint8_t *data, int datalen)
{
    uint8_t buf[MAX_HASHLEN];
    int out, i;
    // Some of the test vectors are strings, where sizeof() includes the
    // trailing null byte - remove that.
    if (!key[keylen - 1])
        keylen--;
    if (!data[datalen - 1])
        datalen--;
    out = av_hmac_calc(hmac, data, datalen, key, keylen, buf, sizeof(buf));
    for (i = 0; i < out; i++)
        printf("%02x", buf[i]);
    printf("\n");
}

int main(void)
{
    uint8_t key1[20], key3[131], data3[50];
    AVHMAC *hmac;
    enum AVHMACType i;
    static const uint8_t key2[]  = "Jefe";
    static const uint8_t data1[] = "Hi There";
    static const uint8_t data2[] = "what do ya want for nothing?";
    static const uint8_t data4[] = "Test Using Larger Than Block-Size Key - Hash Key First";
    static const uint8_t data5[] = "Test Using Larger Than Block-Size Key and Larger Than One Block-Size Data";
    static const uint8_t data6[] = "This is a test using a larger than block-size key and a larger "
                            "than block-size data. The key needs to be hashed before being used"
                            " by the HMAC algorithm.";
    memset(key1, 0x0b, sizeof(key1));
    memset(key3, 0xaa, sizeof(key3));
    memset(data3, 0xdd, sizeof(data3));

    /* MD5, SHA-1 */
    for (i = AV_HMAC_MD5; i <= AV_HMAC_SHA1; i++) {
        hmac = av_hmac_alloc(i);
        if (!hmac)
            return 1;
        // RFC 2202 test vectors
        test(hmac, key1, hmac->hashlen, data1, sizeof(data1));
        test(hmac, key2, sizeof(key2),  data2, sizeof(data2));
        test(hmac, key3, hmac->hashlen, data3, sizeof(data3));
        test(hmac, key3, 80,            data4, sizeof(data4));
        test(hmac, key3, 80,            data5, sizeof(data5));
        av_hmac_free(hmac);
    }

    /* SHA-2 */
    for (i = AV_HMAC_SHA224; i <= AV_HMAC_SHA256; i++) {
        hmac = av_hmac_alloc(i);
        if (!hmac)
            return 1;
        // RFC 4231 test vectors
        test(hmac, key1, sizeof(key1), data1, sizeof(data1));
        test(hmac, key2, sizeof(key2), data2, sizeof(data2));
        test(hmac, key3, 20,           data3, sizeof(data3));
        test(hmac, key3, sizeof(key3), data4, sizeof(data4));
        test(hmac, key3, sizeof(key3), data6, sizeof(data6));
        av_hmac_free(hmac);
    }

    for (i = AV_HMAC_SHA384; i <= AV_HMAC_SHA512; i++) {
        hmac = av_hmac_alloc(i);
        if (!hmac)
            return 1;
        // RFC 4231 test vectors
        test(hmac, key1, sizeof(key1), data1, sizeof(data1));
        test(hmac, key2, sizeof(key2), data2, sizeof(data2));
        test(hmac, key3, 20, data3, sizeof(data3));
        test(hmac, key3, sizeof(key3), data4, sizeof(data4));
        test(hmac, key3, sizeof(key3), data6, sizeof(data6));
        av_hmac_free(hmac);
    }
    return 0;
}
#endif /* TEST */
