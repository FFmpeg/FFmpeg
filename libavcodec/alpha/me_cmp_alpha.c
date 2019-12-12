/*
 * Alpha optimized DSP utils
 * Copyright (c) 2002 Falk Hueffner <falk@debian.org>
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

#include "libavutil/attributes.h"
#include "libavcodec/me_cmp.h"
#include "asm.h"

int pix_abs16x16_mvi_asm(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);

static inline uint64_t avg2(uint64_t a, uint64_t b)
{
    return (a | b) - (((a ^ b) & BYTE_VEC(0xfe)) >> 1);
}

static inline uint64_t avg4(uint64_t l1, uint64_t l2, uint64_t l3, uint64_t l4)
{
    uint64_t r1 = ((l1 & ~BYTE_VEC(0x03)) >> 2)
                + ((l2 & ~BYTE_VEC(0x03)) >> 2)
                + ((l3 & ~BYTE_VEC(0x03)) >> 2)
                + ((l4 & ~BYTE_VEC(0x03)) >> 2);
    uint64_t r2 = ((  (l1 & BYTE_VEC(0x03))
                    + (l2 & BYTE_VEC(0x03))
                    + (l3 & BYTE_VEC(0x03))
                    + (l4 & BYTE_VEC(0x03))
                    + BYTE_VEC(0x02)) >> 2) & BYTE_VEC(0x03);
    return r1 + r2;
}

static int pix_abs8x8_mvi(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int result = 0;

    if ((size_t) pix2 & 0x7) {
        /* works only when pix2 is actually unaligned */
        do {                    /* do 8 pixel a time */
            uint64_t p1, p2;

            p1  = ldq(pix1);
            p2  = uldq(pix2);
            result += perr(p1, p2);

            pix1 += line_size;
            pix2 += line_size;
        } while (--h);
    } else {
        do {
            uint64_t p1, p2;

            p1 = ldq(pix1);
            p2 = ldq(pix2);
            result += perr(p1, p2);

            pix1 += line_size;
            pix2 += line_size;
        } while (--h);
    }

    return result;
}

#if 0                           /* now done in assembly */
int pix_abs16x16_mvi(uint8_t *pix1, uint8_t *pix2, int line_size)
{
    int result = 0;
    int h = 16;

    if ((size_t) pix2 & 0x7) {
        /* works only when pix2 is actually unaligned */
        do {                    /* do 16 pixel a time */
            uint64_t p1_l, p1_r, p2_l, p2_r;
            uint64_t t;

            p1_l  = ldq(pix1);
            p1_r  = ldq(pix1 + 8);
            t     = ldq_u(pix2 + 8);
            p2_l  = extql(ldq_u(pix2), pix2) | extqh(t, pix2);
            p2_r  = extql(t, pix2) | extqh(ldq_u(pix2 + 16), pix2);
            pix1 += line_size;
            pix2 += line_size;

            result += perr(p1_l, p2_l)
                    + perr(p1_r, p2_r);
        } while (--h);
    } else {
        do {
            uint64_t p1_l, p1_r, p2_l, p2_r;

            p1_l = ldq(pix1);
            p1_r = ldq(pix1 + 8);
            p2_l = ldq(pix2);
            p2_r = ldq(pix2 + 8);
            pix1 += line_size;
            pix2 += line_size;

            result += perr(p1_l, p2_l)
                    + perr(p1_r, p2_r);
        } while (--h);
    }

    return result;
}
#endif

static int pix_abs16x16_x2_mvi(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int result = 0;
    uint64_t disalign = (size_t) pix2 & 0x7;

    switch (disalign) {
    case 0:
        do {
            uint64_t p1_l, p1_r, p2_l, p2_r;
            uint64_t l, r;

            p1_l = ldq(pix1);
            p1_r = ldq(pix1 + 8);
            l    = ldq(pix2);
            r    = ldq(pix2 + 8);
            p2_l = avg2(l, (l >> 8) | ((uint64_t) r << 56));
            p2_r = avg2(r, (r >> 8) | ((uint64_t) pix2[16] << 56));
            pix1 += line_size;
            pix2 += line_size;

            result += perr(p1_l, p2_l)
                    + perr(p1_r, p2_r);
        } while (--h);
        break;
    case 7:
        /* |.......l|lllllllr|rrrrrrr*|
           This case is special because disalign1 would be 8, which
           gets treated as 0 by extqh.  At least it is a bit faster
           that way :)  */
        do {
            uint64_t p1_l, p1_r, p2_l, p2_r;
            uint64_t l, m, r;

            p1_l = ldq(pix1);
            p1_r = ldq(pix1 + 8);
            l     = ldq_u(pix2);
            m     = ldq_u(pix2 + 8);
            r     = ldq_u(pix2 + 16);
            p2_l  = avg2(extql(l, disalign) | extqh(m, disalign), m);
            p2_r  = avg2(extql(m, disalign) | extqh(r, disalign), r);
            pix1 += line_size;
            pix2 += line_size;

            result += perr(p1_l, p2_l)
                    + perr(p1_r, p2_r);
        } while (--h);
        break;
    default:
        do {
            uint64_t disalign1 = disalign + 1;
            uint64_t p1_l, p1_r, p2_l, p2_r;
            uint64_t l, m, r;

            p1_l  = ldq(pix1);
            p1_r  = ldq(pix1 + 8);
            l     = ldq_u(pix2);
            m     = ldq_u(pix2 + 8);
            r     = ldq_u(pix2 + 16);
            p2_l  = avg2(extql(l, disalign) | extqh(m, disalign),
                         extql(l, disalign1) | extqh(m, disalign1));
            p2_r  = avg2(extql(m, disalign) | extqh(r, disalign),
                         extql(m, disalign1) | extqh(r, disalign1));
            pix1 += line_size;
            pix2 += line_size;

            result += perr(p1_l, p2_l)
                    + perr(p1_r, p2_r);
        } while (--h);
        break;
    }
    return result;
}

static int pix_abs16x16_y2_mvi(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int result = 0;

    if ((size_t) pix2 & 0x7) {
        uint64_t t, p2_l, p2_r;
        t     = ldq_u(pix2 + 8);
        p2_l  = extql(ldq_u(pix2), pix2) | extqh(t, pix2);
        p2_r  = extql(t, pix2) | extqh(ldq_u(pix2 + 16), pix2);

        do {
            uint64_t p1_l, p1_r, np2_l, np2_r;
            uint64_t t;

            p1_l  = ldq(pix1);
            p1_r  = ldq(pix1 + 8);
            pix2 += line_size;
            t     = ldq_u(pix2 + 8);
            np2_l = extql(ldq_u(pix2), pix2) | extqh(t, pix2);
            np2_r = extql(t, pix2) | extqh(ldq_u(pix2 + 16), pix2);

            result += perr(p1_l, avg2(p2_l, np2_l))
                    + perr(p1_r, avg2(p2_r, np2_r));

            pix1 += line_size;
            p2_l  = np2_l;
            p2_r  = np2_r;

        } while (--h);
    } else {
        uint64_t p2_l, p2_r;
        p2_l = ldq(pix2);
        p2_r = ldq(pix2 + 8);
        do {
            uint64_t p1_l, p1_r, np2_l, np2_r;

            p1_l = ldq(pix1);
            p1_r = ldq(pix1 + 8);
            pix2 += line_size;
            np2_l = ldq(pix2);
            np2_r = ldq(pix2 + 8);

            result += perr(p1_l, avg2(p2_l, np2_l))
                    + perr(p1_r, avg2(p2_r, np2_r));

            pix1 += line_size;
            p2_l  = np2_l;
            p2_r  = np2_r;
        } while (--h);
    }
    return result;
}

static int pix_abs16x16_xy2_mvi(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int result = 0;

    uint64_t p1_l, p1_r;
    uint64_t p2_l, p2_r, p2_x;

    p1_l = ldq(pix1);
    p1_r = ldq(pix1 + 8);

    if ((size_t) pix2 & 0x7) { /* could be optimized a lot */
        p2_l = uldq(pix2);
        p2_r = uldq(pix2 + 8);
        p2_x = (uint64_t) pix2[16] << 56;
    } else {
        p2_l = ldq(pix2);
        p2_r = ldq(pix2 + 8);
        p2_x = ldq(pix2 + 16) << 56;
    }

    do {
        uint64_t np1_l, np1_r;
        uint64_t np2_l, np2_r, np2_x;

        pix1 += line_size;
        pix2 += line_size;

        np1_l = ldq(pix1);
        np1_r = ldq(pix1 + 8);

        if ((size_t) pix2 & 0x7) { /* could be optimized a lot */
            np2_l = uldq(pix2);
            np2_r = uldq(pix2 + 8);
            np2_x = (uint64_t) pix2[16] << 56;
        } else {
            np2_l = ldq(pix2);
            np2_r = ldq(pix2 + 8);
            np2_x = ldq(pix2 + 16) << 56;
        }

        result += perr(p1_l,
                       avg4( p2_l, ( p2_l >> 8) | ((uint64_t)  p2_r << 56),
                            np2_l, (np2_l >> 8) | ((uint64_t) np2_r << 56)))
                + perr(p1_r,
                       avg4( p2_r, ( p2_r >> 8) | ((uint64_t)  p2_x),
                            np2_r, (np2_r >> 8) | ((uint64_t) np2_x)));

        p1_l = np1_l;
        p1_r = np1_r;
        p2_l = np2_l;
        p2_r = np2_r;
        p2_x = np2_x;
    } while (--h);

    return result;
}

av_cold void ff_me_cmp_init_alpha(MECmpContext *c, AVCodecContext *avctx)
{
    /* amask clears all bits that correspond to present features.  */
    if (amask(AMASK_MVI) == 0) {
        c->sad[0]           = pix_abs16x16_mvi_asm;
        c->sad[1]           = pix_abs8x8_mvi;
        c->pix_abs[0][0]    = pix_abs16x16_mvi_asm;
        c->pix_abs[1][0]    = pix_abs8x8_mvi;
        c->pix_abs[0][1]    = pix_abs16x16_x2_mvi;
        c->pix_abs[0][2]    = pix_abs16x16_y2_mvi;
        c->pix_abs[0][3]    = pix_abs16x16_xy2_mvi;
    }
}
