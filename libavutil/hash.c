/*
 * Copyright (C) 2013 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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
#include <stdint.h>
#include "hash.h"

#include "adler32.h"
#include "crc.h"
#include "md5.h"
#include "murmur3.h"
#include "ripemd.h"
#include "sha.h"
#include "sha512.h"

#include "avstring.h"
#include "base64.h"
#include "error.h"
#include "intreadwrite.h"
#include "mem.h"

enum hashtype {
    MD5,
    MURMUR3,
    RIPEMD128,
    RIPEMD160,
    RIPEMD256,
    RIPEMD320,
    SHA160,
    SHA224,
    SHA256,
    SHA512_224,
    SHA512_256,
    SHA384,
    SHA512,
    CRC32,
    ADLER32,
    NUM_HASHES
};

typedef struct AVHashContext {
    void *ctx;
    enum hashtype type;
    const AVCRC *crctab;
    uint32_t crc;
} AVHashContext;

struct {
    const char *name;
    int size;
} hashdesc[] = {
    [MD5]     = {"MD5",     16},
    [MURMUR3] = {"murmur3", 16},
    [RIPEMD128] = {"RIPEMD128", 16},
    [RIPEMD160] = {"RIPEMD160", 20},
    [RIPEMD256] = {"RIPEMD256", 32},
    [RIPEMD320] = {"RIPEMD320", 40},
    [SHA160]  = {"SHA160",  20},
    [SHA224]  = {"SHA224",  28},
    [SHA256]  = {"SHA256",  32},
    [SHA512_224]  = {"SHA512/224",  28},
    [SHA512_256]  = {"SHA512/256",  32},
    [SHA384]  = {"SHA384",  48},
    [SHA512]  = {"SHA512",  64},
    [CRC32]   = {"CRC32",    4},
    [ADLER32] = {"adler32",  4},
};

const char *av_hash_names(int i)
{
    if (i < 0 || i >= NUM_HASHES) return NULL;
    return hashdesc[i].name;
}

const char *av_hash_get_name(const AVHashContext *ctx)
{
    return hashdesc[ctx->type].name;
}

int av_hash_get_size(const AVHashContext *ctx)
{
    return hashdesc[ctx->type].size;
}

int av_hash_alloc(AVHashContext **ctx, const char *name)
{
    AVHashContext *res;
    int i;
    *ctx = NULL;
    for (i = 0; i < NUM_HASHES; i++)
        if (av_strcasecmp(name, hashdesc[i].name) == 0)
            break;
    if (i >= NUM_HASHES) return AVERROR(EINVAL);
    res = av_mallocz(sizeof(*res));
    if (!res) return AVERROR(ENOMEM);
    res->type = i;
    switch (i) {
    case MD5:     res->ctx = av_md5_alloc(); break;
    case MURMUR3: res->ctx = av_murmur3_alloc(); break;
    case RIPEMD128:
    case RIPEMD160:
    case RIPEMD256:
    case RIPEMD320: res->ctx = av_ripemd_alloc(); break;
    case SHA160:
    case SHA224:
    case SHA256:  res->ctx = av_sha_alloc(); break;
    case SHA512_224:
    case SHA512_256:
    case SHA384:
    case SHA512:  res->ctx = av_sha512_alloc(); break;
    case CRC32:   res->crctab = av_crc_get_table(AV_CRC_32_IEEE_LE); break;
    case ADLER32: break;
    }
    if (i != ADLER32 && i != CRC32 && !res->ctx) {
        av_free(res);
        return AVERROR(ENOMEM);
    }
    *ctx = res;
    return 0;
}

void av_hash_init(AVHashContext *ctx)
{
    switch (ctx->type) {
    case MD5:     av_md5_init(ctx->ctx); break;
    case MURMUR3: av_murmur3_init(ctx->ctx); break;
    case RIPEMD128: av_ripemd_init(ctx->ctx, 128); break;
    case RIPEMD160: av_ripemd_init(ctx->ctx, 160); break;
    case RIPEMD256: av_ripemd_init(ctx->ctx, 256); break;
    case RIPEMD320: av_ripemd_init(ctx->ctx, 320); break;
    case SHA160:  av_sha_init(ctx->ctx, 160); break;
    case SHA224:  av_sha_init(ctx->ctx, 224); break;
    case SHA256:  av_sha_init(ctx->ctx, 256); break;
    case SHA512_224:  av_sha512_init(ctx->ctx, 224); break;
    case SHA512_256:  av_sha512_init(ctx->ctx, 256); break;
    case SHA384:  av_sha512_init(ctx->ctx, 384); break;
    case SHA512:  av_sha512_init(ctx->ctx, 512); break;
    case CRC32:   ctx->crc = UINT32_MAX; break;
    case ADLER32: ctx->crc = 1; break;
    }
}

void av_hash_update(AVHashContext *ctx, const uint8_t *src, int len)
{
    switch (ctx->type) {
    case MD5:     av_md5_update(ctx->ctx, src, len); break;
    case MURMUR3: av_murmur3_update(ctx->ctx, src, len); break;
    case RIPEMD128:
    case RIPEMD160:
    case RIPEMD256:
    case RIPEMD320: av_ripemd_update(ctx->ctx, src, len); break;
    case SHA160:
    case SHA224:
    case SHA256:  av_sha_update(ctx->ctx, src, len); break;
    case SHA512_224:
    case SHA512_256:
    case SHA384:
    case SHA512:  av_sha512_update(ctx->ctx, src, len); break;
    case CRC32:   ctx->crc = av_crc(ctx->crctab, ctx->crc, src, len); break;
    case ADLER32: ctx->crc = av_adler32_update(ctx->crc, src, len); break;
    }
}

void av_hash_final(AVHashContext *ctx, uint8_t *dst)
{
    switch (ctx->type) {
    case MD5:     av_md5_final(ctx->ctx, dst); break;
    case MURMUR3: av_murmur3_final(ctx->ctx, dst); break;
    case RIPEMD128:
    case RIPEMD160:
    case RIPEMD256:
    case RIPEMD320: av_ripemd_final(ctx->ctx, dst); break;
    case SHA160:
    case SHA224:
    case SHA256:  av_sha_final(ctx->ctx, dst); break;
    case SHA512_224:
    case SHA512_256:
    case SHA384:
    case SHA512:  av_sha512_final(ctx->ctx, dst); break;
    case CRC32:   AV_WB32(dst, ctx->crc ^ UINT32_MAX); break;
    case ADLER32: AV_WB32(dst, ctx->crc); break;
    }
}

void av_hash_final_bin(struct AVHashContext *ctx, uint8_t *dst, int size)
{
    uint8_t buf[AV_HASH_MAX_SIZE];
    unsigned rsize = av_hash_get_size(ctx);

    av_hash_final(ctx, buf);
    memcpy(dst, buf, FFMIN(size, rsize));
    if (size > rsize)
        memset(dst + rsize, 0, size - rsize);
}

void av_hash_final_hex(struct AVHashContext *ctx, uint8_t *dst, int size)
{
    uint8_t buf[AV_HASH_MAX_SIZE];
    unsigned rsize = av_hash_get_size(ctx), i;

    av_hash_final(ctx, buf);
    for (i = 0; i < FFMIN(rsize, size / 2); i++)
        snprintf(dst + i * 2, size - i * 2, "%02x", buf[i]);
}

void av_hash_final_b64(struct AVHashContext *ctx, uint8_t *dst, int size)
{
    uint8_t buf[AV_HASH_MAX_SIZE], b64[AV_BASE64_SIZE(AV_HASH_MAX_SIZE)];
    unsigned rsize = av_hash_get_size(ctx), osize;

    av_hash_final(ctx, buf);
    av_base64_encode(b64, sizeof(b64), buf, rsize);
    osize = AV_BASE64_SIZE(rsize);
    memcpy(dst, b64, FFMIN(osize, size));
    if (size < osize)
        dst[size - 1] = 0;
}

void av_hash_freep(AVHashContext **ctx)
{
    if (*ctx)
        av_freep(&(*ctx)->ctx);
    av_freep(ctx);
}
