/*
 * LZO 1x decompression
 * Copyright (c) 2006 Reimar Doeffinger
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include "common.h"
//! avoid e.g. MPlayers fast_memcpy, it slows things down here
#undef memcpy
#include <string.h>
#include "lzo.h"

//! define if we may write up to 12 bytes beyond the output buffer
#define OUTBUF_PADDED 1
//! define if we may read up to 4 bytes beyond the input buffer
#define INBUF_PADDED 1
typedef struct LZOContext {
    uint8_t *in, *in_end;
    uint8_t *out_start, *out, *out_end;
    int error;
} LZOContext;

/**
 * \brief read one byte from input buffer, avoiding overrun
 * \return byte read
 */
static inline int get_byte(LZOContext *c) {
    if (c->in < c->in_end)
        return *c->in++;
    c->error |= LZO_INPUT_DEPLETED;
    return 1;
}

/**
 * \brief decode a length value in the coding used by lzo
 * \param x previous byte value
 * \param mask bits used from x
 * \return decoded length value
 */
static inline int get_len(LZOContext *c, int x, int mask) {
    int cnt = x & mask;
    if (!cnt) {
        while (!(x = get_byte(c))) cnt += 255;
        cnt += mask + x;
    }
    return cnt;
}

/**
 * \brief copy bytes from input to output buffer with checking
 * \param cnt number of bytes to copy, must be > 0
 */
static inline void copy(LZOContext *c, int cnt) {
    register uint8_t *src = c->in;
    register uint8_t *dst = c->out;
    if (src + cnt > c->in_end) {
        cnt = c->in_end - src;
        c->error |= LZO_INPUT_DEPLETED;
    }
    if (dst + cnt > c->out_end) {
        cnt = c->out_end - dst;
        c->error |= LZO_OUTPUT_FULL;
    }
#if defined(INBUF_PADDED) && defined(OUTBUF_PADDED)
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    src += 4;
    dst += 4;
    cnt -= 4;
    if (cnt > 0)
#endif
        memcpy(dst, src, cnt);
    c->in = src + cnt;
    c->out = dst + cnt;
}

/**
 * \brief copy previously decoded bytes to current position
 * \param back how many bytes back we start
 * \param cnt number of bytes to copy, must be > 0
 *
 * cnt > back is valid, this will copy the bytes we just copied,
 * thus creating a repeating pattern with a period length of back.
 */
static inline void copy_backptr(LZOContext *c, int back, int cnt) {
    register uint8_t *src = &c->out[-back];
    register uint8_t *dst = c->out;
    if (src < c->out_start) {
        c->error |= LZO_INVALID_BACKPTR;
        return;
    }
    if (dst + cnt > c->out_end) {
        cnt = c->out_end - dst;
        c->error |= LZO_OUTPUT_FULL;
    }
    if (back == 1) {
        memset(dst, *src, cnt);
        dst += cnt;
    } else {
#ifdef OUTBUF_PADDED
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
        src += 4;
        dst += 4;
        cnt -= 4;
        if (cnt > 0) {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
            dst[4] = src[4];
            dst[5] = src[5];
            dst[6] = src[6];
            dst[7] = src[7];
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
        dst += cnt;
    }
    c->out = dst;
}

/**
 * \brief decode LZO 1x compressed data
 * \param out output buffer
 * \param outlen size of output buffer, number of bytes left are returned here
 * \param in input buffer
 * \param inlen size of input buffer, number of bytes left are returned here
 * \return 0 on success, otherwise error flags, see lzo.h
 *
 * make sure all buffers are appropriately padded, in must provide
 * LZO_INPUT_PADDING, out must provide LZO_OUTPUT_PADDING additional bytes
 */
int lzo1x_decode(void *out, int *outlen, void *in, int *inlen) {
    enum {COPY, BACKPTR} state = COPY;
    int x;
    LZOContext c;
    c.in = in;
    c.in_end = in + *inlen;
    c.out = c.out_start = out;
    c.out_end = out + * outlen;
    c.error = 0;
    x = get_byte(&c);
    if (x > 17) {
        copy(&c, x - 17);
        x = get_byte(&c);
        if (x < 16) c.error |= LZO_ERROR;
    }
    while (!c.error) {
        int cnt, back;
        if (x >> 4) {
            if (x >> 6) {
                cnt = (x >> 5) - 1;
                back = (get_byte(&c) << 3) + ((x >> 2) & 7) + 1;
            } else if (x >> 5) {
                cnt = get_len(&c, x, 31);
                x = get_byte(&c);
                back = (get_byte(&c) << 6) + (x >> 2) + 1;
            } else {
                cnt = get_len(&c, x, 7);
                back = (1 << 14) + ((x & 8) << 11);
                x = get_byte(&c);
                back += (get_byte(&c) << 6) + (x >> 2);
                if (back == (1 << 14)) {
                    if (cnt != 1)
                        c.error |= LZO_ERROR;
                    break;
                }
            }
        } else
        switch (state) {
            case COPY:
                cnt = get_len(&c, x, 15);
                copy(&c, cnt + 3);
                x = get_byte(&c);
                if (x >> 4)
                    continue;
                cnt = 1;
                back = (1 << 11) + (get_byte(&c) << 2) + (x >> 2) + 1;
                break;
            case BACKPTR:
                cnt = 0;
                back = (get_byte(&c) << 2) + (x >> 2) + 1;
                break;
        }
        copy_backptr(&c, back, cnt + 2);
        cnt = x & 3;
        state = cnt ? BACKPTR : COPY;
        if (cnt)
            copy(&c, cnt);
        x = get_byte(&c);
    }
    *inlen = c.in_end - c.in;
    *outlen = c.out_end - c.out;
    return c.error;
}
