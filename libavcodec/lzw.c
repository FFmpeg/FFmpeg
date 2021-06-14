/*
 * LZW decoder
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Konstantin Shishkov
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

/**
 * @file
 * @brief LZW decoding routines
 * @author Fabrice Bellard
 * @author modified for use in TIFF by Konstantin Shishkov
 */

#include "libavutil/attributes.h"
#include "bytestream.h"
#include "lzw.h"
#include "libavutil/mem.h"

#define LZW_MAXBITS                 12
#define LZW_SIZTABLE                (1<<LZW_MAXBITS)

static const uint16_t mask[17] =
{
    0x0000, 0x0001, 0x0003, 0x0007,
    0x000F, 0x001F, 0x003F, 0x007F,
    0x00FF, 0x01FF, 0x03FF, 0x07FF,
    0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF
};

struct LZWState {
    GetByteContext gb;
    int bbits;
    unsigned int bbuf;

    int mode;                   ///< Decoder mode
    int cursize;                ///< The current code size
    int curmask;
    int codesize;
    int clear_code;
    int end_code;
    int newcodes;               ///< First available code
    int top_slot;               ///< Highest code for current size
    int extra_slot;
    int slot;                   ///< Last read code
    int fc, oc;
    uint8_t *sp;
    uint8_t stack[LZW_SIZTABLE];
    uint8_t suffix[LZW_SIZTABLE];
    uint16_t prefix[LZW_SIZTABLE];
    int bs;                     ///< current buffer size for GIF
};

/* get one code from stream */
static int lzw_get_code(struct LZWState * s)
{
    int c;

    if (s->bbits < s->cursize && bytestream2_get_bytes_left(&s->gb) <= 0)
        return s->end_code;

    if(s->mode == FF_LZW_GIF) {
        while (s->bbits < s->cursize) {
            if (!s->bs) {
                s->bs = bytestream2_get_byte(&s->gb);
            }
            s->bbuf |= bytestream2_get_byte(&s->gb) << s->bbits;
            s->bbits += 8;
            s->bs--;
        }
        c = s->bbuf;
        s->bbuf >>= s->cursize;
    } else { // TIFF
        while (s->bbits < s->cursize) {
            s->bbuf = (s->bbuf << 8) | bytestream2_get_byte(&s->gb);
            s->bbits += 8;
        }
        c = s->bbuf >> (s->bbits - s->cursize);
    }
    s->bbits -= s->cursize;
    return c & s->curmask;
}

int ff_lzw_decode_tail(LZWState *p)
{
    struct LZWState *s = (struct LZWState *)p;

    if(s->mode == FF_LZW_GIF) {
        while (s->bs > 0 && bytestream2_get_bytes_left(&s->gb)) {
            bytestream2_skip(&s->gb, s->bs);
            s->bs = bytestream2_get_byte(&s->gb);
        }
    }else
        bytestream2_skip(&s->gb, bytestream2_get_bytes_left(&s->gb));
    return bytestream2_tell(&s->gb);
}

av_cold void ff_lzw_decode_open(LZWState **p)
{
    *p = av_mallocz(sizeof(struct LZWState));
}

av_cold void ff_lzw_decode_close(LZWState **p)
{
    av_freep(p);
}

/**
 * Initialize LZW decoder
 * @param p LZW context
 * @param csize initial code size in bits
 * @param buf input data
 * @param buf_size input data size
 * @param mode decoder working mode - either GIF or TIFF
 */
int ff_lzw_decode_init(LZWState *p, int csize, const uint8_t *buf, int buf_size, int mode)
{
    struct LZWState *s = (struct LZWState *)p;

    if(csize < 1 || csize >= LZW_MAXBITS)
        return -1;
    /* read buffer */
    bytestream2_init(&s->gb, buf, buf_size);
    s->bbuf = 0;
    s->bbits = 0;
    s->bs = 0;

    /* decoder */
    s->codesize = csize;
    s->cursize = s->codesize + 1;
    s->curmask = mask[s->cursize];
    s->top_slot = 1 << s->cursize;
    s->clear_code = 1 << s->codesize;
    s->end_code = s->clear_code + 1;
    s->slot = s->newcodes = s->clear_code + 2;
    s->oc = s->fc = -1;
    s->sp = s->stack;

    s->mode = mode;
    s->extra_slot = s->mode == FF_LZW_TIFF;
    return 0;
}

/**
 * Decode given number of bytes
 * NOTE: the algorithm here is inspired from the LZW GIF decoder
 *  written by Steven A. Bennett in 1987.
 *
 * @param p LZW context
 * @param buf output buffer
 * @param len number of bytes to decode
 * @return number of bytes decoded
 */
int ff_lzw_decode(LZWState *p, uint8_t *buf, int len){
    int l, c, code, oc, fc;
    uint8_t *sp;
    struct LZWState *s = (struct LZWState *)p;

    if (s->end_code < 0)
        return 0;

    l = len;
    sp = s->sp;
    oc = s->oc;
    fc = s->fc;

    for (;;) {
        while (sp > s->stack) {
            *buf++ = *(--sp);
            if ((--l) == 0)
                goto the_end;
        }
        c = lzw_get_code(s);
        if (c == s->end_code) {
            break;
        } else if (c == s->clear_code) {
            s->cursize = s->codesize + 1;
            s->curmask = mask[s->cursize];
            s->slot = s->newcodes;
            s->top_slot = 1 << s->cursize;
            fc= oc= -1;
        } else {
            code = c;
            if (code == s->slot && fc>=0) {
                *sp++ = fc;
                code = oc;
            }else if(code >= s->slot)
                break;
            while (code >= s->newcodes) {
                *sp++ = s->suffix[code];
                code = s->prefix[code];
            }
            *sp++ = code;
            if (s->slot < s->top_slot && oc>=0) {
                s->suffix[s->slot] = code;
                s->prefix[s->slot++] = oc;
            }
            fc = code;
            oc = c;
            if (s->slot >= s->top_slot - s->extra_slot) {
                if (s->cursize < LZW_MAXBITS) {
                    s->top_slot <<= 1;
                    s->curmask = mask[++s->cursize];
                }
            }
        }
    }
    s->end_code = -1;
  the_end:
    s->sp = sp;
    s->oc = oc;
    s->fc = fc;
    return len - l;
}
