/*
 * LZO 1x decompression
 * Copyright (c) 2006 Reimar Doeffinger
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

#include "avutil.h"
#include "common.h"
//! Avoid e.g. MPlayers fast_memcpy, it slows things down here.
#undef memcpy
#include <string.h>
#include "lzo.h"

//! Define if we may write up to 12 bytes beyond the output buffer.
#define OUTBUF_PADDED 1
//! Define if we may read up to 8 bytes beyond the input buffer.
#define INBUF_PADDED 1
typedef struct LZOContext {
    const uint8_t *in, *in_end;
    uint8_t *out_start, *out, *out_end;
    int error;
} LZOContext;

/**
 * @brief Reads one byte from the input buffer, avoiding an overrun.
 * @return byte read
 */
static inline int get_byte(LZOContext *c) {
    if (c->in < c->in_end)
        return *c->in++;
    c->error |= AV_LZO_INPUT_DEPLETED;
    return 1;
}

#ifdef INBUF_PADDED
#define GETB(c) (*(c).in++)
#else
#define GETB(c) get_byte(&(c))
#endif

/**
 * @brief Decodes a length value in the coding used by lzo.
 * @param x previous byte value
 * @param mask bits used from x
 * @return decoded length value
 */
static inline int get_len(LZOContext *c, int x, int mask) {
    int cnt = x & mask;
    if (!cnt) {
        while (!(x = get_byte(c))) cnt += 255;
        cnt += mask + x;
    }
    return cnt;
}

//#define UNALIGNED_LOADSTORE
#define BUILTIN_MEMCPY
#ifdef UNALIGNED_LOADSTORE
#define COPY2(d, s) *(uint16_t *)(d) = *(uint16_t *)(s);
#define COPY4(d, s) *(uint32_t *)(d) = *(uint32_t *)(s);
#elif defined(BUILTIN_MEMCPY)
#define COPY2(d, s) memcpy(d, s, 2);
#define COPY4(d, s) memcpy(d, s, 4);
#else
#define COPY2(d, s) (d)[0] = (s)[0]; (d)[1] = (s)[1];
#define COPY4(d, s) (d)[0] = (s)[0]; (d)[1] = (s)[1]; (d)[2] = (s)[2]; (d)[3] = (s)[3];
#endif

/**
 * @brief Copies bytes from input to output buffer with checking.
 * @param cnt number of bytes to copy, must be >= 0
 */
static inline void copy(LZOContext *c, int cnt) {
    register const uint8_t *src = c->in;
    register uint8_t *dst = c->out;
    if (cnt > c->in_end - src) {
        cnt = FFMAX(c->in_end - src, 0);
        c->error |= AV_LZO_INPUT_DEPLETED;
    }
    if (cnt > c->out_end - dst) {
        cnt = FFMAX(c->out_end - dst, 0);
        c->error |= AV_LZO_OUTPUT_FULL;
    }
#if defined(INBUF_PADDED) && defined(OUTBUF_PADDED)
    COPY4(dst, src);
    src += 4;
    dst += 4;
    cnt -= 4;
    if (cnt > 0)
#endif
        memcpy(dst, src, cnt);
    c->in = src + cnt;
    c->out = dst + cnt;
}

static inline void memcpy_backptr(uint8_t *dst, int back, int cnt);

/**
 * @brief Copies previously decoded bytes to current position.
 * @param back how many bytes back we start
 * @param cnt number of bytes to copy, must be >= 0
 *
 * cnt > back is valid, this will copy the bytes we just copied,
 * thus creating a repeating pattern with a period length of back.
 */
static inline void copy_backptr(LZOContext *c, int back, int cnt) {
    register const uint8_t *src = &c->out[-back];
    register uint8_t *dst = c->out;
    if (src < c->out_start || src > dst) {
        c->error |= AV_LZO_INVALID_BACKPTR;
        return;
    }
    if (cnt > c->out_end - dst) {
        cnt = FFMAX(c->out_end - dst, 0);
        c->error |= AV_LZO_OUTPUT_FULL;
    }
    memcpy_backptr(dst, back, cnt);
    c->out = dst + cnt;
}

static inline void memcpy_backptr(uint8_t *dst, int back, int cnt) {
    const uint8_t *src = &dst[-back];
    if (back == 1) {
        memset(dst, *src, cnt);
    } else {
#ifdef OUTBUF_PADDED
        COPY2(dst, src);
        COPY2(dst + 2, src + 2);
        src += 4;
        dst += 4;
        cnt -= 4;
        if (cnt > 0) {
            COPY2(dst, src);
            COPY2(dst + 2, src + 2);
            COPY2(dst + 4, src + 4);
            COPY2(dst + 6, src + 6);
            src += 8;
            dst += 8;
            cnt -= 8;
        }
#endif
        if (cnt > 0) {
            int blocklen = back;
            while (cnt > blocklen) {
                memcpy(dst, src, blocklen);
                dst += blocklen;
                cnt -= blocklen;
                blocklen <<= 1;
            }
            memcpy(dst, src, cnt);
        }
    }
}

void av_memcpy_backptr(uint8_t *dst, int back, int cnt) {
    memcpy_backptr(dst, back, cnt);
}

int av_lzo1x_decode(void *out, int *outlen, const void *in, int *inlen) {
    int state= 0;
    int x;
    LZOContext c;
    if (*outlen <= 0 || *inlen <= 0) {
        int res = 0;
        if (*outlen <= 0)
            res |= AV_LZO_OUTPUT_FULL;
        if (*inlen <= 0)
            res |= AV_LZO_INPUT_DEPLETED;
        return res;
    }
    c.in = in;
    c.in_end = (const uint8_t *)in + *inlen;
    c.out = c.out_start = out;
    c.out_end = (uint8_t *)out + * outlen;
    c.error = 0;
    x = GETB(c);
    if (x > 17) {
        copy(&c, x - 17);
        x = GETB(c);
        if (x < 16) c.error |= AV_LZO_ERROR;
    }
    if (c.in > c.in_end)
        c.error |= AV_LZO_INPUT_DEPLETED;
    while (!c.error) {
        int cnt, back;
        if (x > 15) {
            if (x > 63) {
                cnt = (x >> 5) - 1;
                back = (GETB(c) << 3) + ((x >> 2) & 7) + 1;
            } else if (x > 31) {
                cnt = get_len(&c, x, 31);
                x = GETB(c);
                back = (GETB(c) << 6) + (x >> 2) + 1;
            } else {
                cnt = get_len(&c, x, 7);
                back = (1 << 14) + ((x & 8) << 11);
                x = GETB(c);
                back += (GETB(c) << 6) + (x >> 2);
                if (back == (1 << 14)) {
                    if (cnt != 1)
                        c.error |= AV_LZO_ERROR;
                    break;
                }
            }
        } else if(!state){
                cnt = get_len(&c, x, 15);
                copy(&c, cnt + 3);
                x = GETB(c);
                if (x > 15)
                    continue;
                cnt = 1;
                back = (1 << 11) + (GETB(c) << 2) + (x >> 2) + 1;
        } else {
                cnt = 0;
                back = (GETB(c) << 2) + (x >> 2) + 1;
        }
        copy_backptr(&c, back, cnt + 2);
        state=
        cnt = x & 3;
        copy(&c, cnt);
        x = GETB(c);
    }
    *inlen = c.in_end - c.in;
    if (c.in > c.in_end)
        *inlen = 0;
    *outlen = c.out_end - c.out;
    return c.error;
}

#ifdef TEST
#include <stdio.h>
#include <lzo/lzo1x.h>
#include "log.h"
#define MAXSZ (10*1024*1024)

/* Define one of these to 1 if you wish to benchmark liblzo
 * instead of our native implementation. */
#define BENCHMARK_LIBLZO_SAFE   0
#define BENCHMARK_LIBLZO_UNSAFE 0

int main(int argc, char *argv[]) {
    FILE *in = fopen(argv[1], "rb");
    uint8_t *orig = av_malloc(MAXSZ + 16);
    uint8_t *comp = av_malloc(2*MAXSZ + 16);
    uint8_t *decomp = av_malloc(MAXSZ + 16);
    size_t s = fread(orig, 1, MAXSZ, in);
    lzo_uint clen = 0;
    long tmp[LZO1X_MEM_COMPRESS];
    int inlen, outlen;
    int i;
    av_log_set_level(AV_LOG_DEBUG);
    lzo1x_999_compress(orig, s, comp, &clen, tmp);
    for (i = 0; i < 300; i++) {
START_TIMER
        inlen = clen; outlen = MAXSZ;
#if BENCHMARK_LIBLZO_SAFE
        if (lzo1x_decompress_safe(comp, inlen, decomp, &outlen, NULL))
#elif BENCHMARK_LIBLZO_UNSAFE
        if (lzo1x_decompress(comp, inlen, decomp, &outlen, NULL))
#else
        if (av_lzo1x_decode(decomp, &outlen, comp, &inlen))
#endif
            av_log(NULL, AV_LOG_ERROR, "decompression error\n");
STOP_TIMER("lzod")
    }
    if (memcmp(orig, decomp, s))
        av_log(NULL, AV_LOG_ERROR, "decompression incorrect\n");
    else
        av_log(NULL, AV_LOG_ERROR, "decompression OK\n");
    return 0;
}
#endif
