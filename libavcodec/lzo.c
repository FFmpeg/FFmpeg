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
#include "lzo.h"

typedef struct LZOContext {
    uint8_t *in, *in_end;
    uint8_t *out, *out_end;
    int out_size;
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
    if (c->in + cnt > c->in_end) {
        cnt = c->in_end - c->in;
        c->error |= LZO_INPUT_DEPLETED;
    }
    if (c->out + cnt > c->out_end) {
        cnt = c->out_end - c->out;
        c->error |= LZO_OUTPUT_FULL;
    }
    do {
        *c->out++ = *c->in++;
    } while (--cnt);
}

/**
 * \brief copy previously decoded bytes to current position
 * \param back how many bytes back we start
 * \param cnt number of bytes to copy, must be > 0
 *
 * cnt > back is valid, this will copy the bytes we just copied.
 */
static inline void copy_backptr(LZOContext *c, int back, int cnt) {
    if (c->out - back < c->out_end - c->out_size) {
        c->error |= LZO_INVALID_BACKPTR;
        return;
    }
    if (c->out + cnt > c->out_end) {
        cnt = c->out_end - c->out;
        c->error |= LZO_OUTPUT_FULL;
    }
    do {
        *c->out++ = c->out[-back];
    } while (--cnt);
}

/**
 * \brief decode LZO 1x compressed data
 * \param out output buffer
 * \param outlen size of output buffer, number of bytes left are returned here
 * \param in input buffer
 * \param inlen size of input buffer, number of bytes left are returned here
 * \return 0 on success, otherwise error flags, see lzo.h
 */
int lzo1x_decode(void *out, int *outlen, void *in, int *inlen) {
    enum {COPY, BACKPTR} state = COPY;
    int x;
    LZOContext c;
    c.in = in;
    c.in_end = in + *inlen;
    c.out = out;
    c.out_end = out + * outlen;
    c.out_size = *outlen;
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
