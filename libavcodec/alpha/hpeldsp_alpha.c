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
#include "libavcodec/hpeldsp.h"
#include "hpeldsp_alpha.h"
#include "asm.h"

static inline uint64_t avg2_no_rnd(uint64_t a, uint64_t b)
{
    return (a & b) + (((a ^ b) & BYTE_VEC(0xfe)) >> 1);
}

static inline uint64_t avg2(uint64_t a, uint64_t b)
{
    return (a | b) - (((a ^ b) & BYTE_VEC(0xfe)) >> 1);
}

#if 0
/* The XY2 routines basically utilize this scheme, but reuse parts in
   each iteration.  */
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
#endif

#define OP(LOAD, STORE)                         \
    do {                                        \
        STORE(LOAD(pixels), block);             \
        pixels += line_size;                    \
        block += line_size;                     \
    } while (--h)

#define OP_X2(LOAD, STORE)                                      \
    do {                                                        \
        uint64_t pix1, pix2;                                    \
                                                                \
        pix1 = LOAD(pixels);                                    \
        pix2 = pix1 >> 8 | ((uint64_t) pixels[8] << 56);        \
        STORE(AVG2(pix1, pix2), block);                         \
        pixels += line_size;                                    \
        block += line_size;                                     \
    } while (--h)

#define OP_Y2(LOAD, STORE)                      \
    do {                                        \
        uint64_t pix = LOAD(pixels);            \
        do {                                    \
            uint64_t next_pix;                  \
                                                \
            pixels += line_size;                \
            next_pix = LOAD(pixels);            \
            STORE(AVG2(pix, next_pix), block);  \
            block += line_size;                 \
            pix = next_pix;                     \
        } while (--h);                          \
    } while (0)

#define OP_XY2(LOAD, STORE)                                                 \
    do {                                                                    \
        uint64_t pix1 = LOAD(pixels);                                       \
        uint64_t pix2 = pix1 >> 8 | ((uint64_t) pixels[8] << 56);           \
        uint64_t pix_l = (pix1 & BYTE_VEC(0x03))                            \
                       + (pix2 & BYTE_VEC(0x03));                           \
        uint64_t pix_h = ((pix1 & ~BYTE_VEC(0x03)) >> 2)                    \
                       + ((pix2 & ~BYTE_VEC(0x03)) >> 2);                   \
                                                                            \
        do {                                                                \
            uint64_t npix1, npix2;                                          \
            uint64_t npix_l, npix_h;                                        \
            uint64_t avg;                                                   \
                                                                            \
            pixels += line_size;                                            \
            npix1 = LOAD(pixels);                                           \
            npix2 = npix1 >> 8 | ((uint64_t) pixels[8] << 56);              \
            npix_l = (npix1 & BYTE_VEC(0x03))                               \
                   + (npix2 & BYTE_VEC(0x03));                              \
            npix_h = ((npix1 & ~BYTE_VEC(0x03)) >> 2)                       \
                   + ((npix2 & ~BYTE_VEC(0x03)) >> 2);                      \
            avg = (((pix_l + npix_l + AVG4_ROUNDER) >> 2) & BYTE_VEC(0x03)) \
                + pix_h + npix_h;                                           \
            STORE(avg, block);                                              \
                                                                            \
            block += line_size;                                             \
            pix_l = npix_l;                                                 \
            pix_h = npix_h;                                                 \
        } while (--h);                                                      \
    } while (0)

#define MAKE_OP(OPNAME, SUFF, OPKIND, STORE)                                \
static void OPNAME ## _pixels ## SUFF ## _axp                               \
        (uint8_t *restrict block, const uint8_t *restrict pixels,           \
         ptrdiff_t line_size, int h)                                        \
{                                                                           \
    if ((size_t) pixels & 0x7) {                                            \
        OPKIND(uldq, STORE);                                                \
    } else {                                                                \
        OPKIND(ldq, STORE);                                                 \
    }                                                                       \
}                                                                           \
                                                                            \
static void OPNAME ## _pixels16 ## SUFF ## _axp                             \
        (uint8_t *restrict block, const uint8_t *restrict pixels,           \
         ptrdiff_t line_size, int h)                                        \
{                                                                           \
    OPNAME ## _pixels ## SUFF ## _axp(block,     pixels,     line_size, h); \
    OPNAME ## _pixels ## SUFF ## _axp(block + 8, pixels + 8, line_size, h); \
}

#define PIXOP(OPNAME, STORE)                    \
    MAKE_OP(OPNAME, ,     OP,     STORE)        \
    MAKE_OP(OPNAME, _x2,  OP_X2,  STORE)        \
    MAKE_OP(OPNAME, _y2,  OP_Y2,  STORE)        \
    MAKE_OP(OPNAME, _xy2, OP_XY2, STORE)

/* Rounding primitives.  */
#define AVG2 avg2
#define AVG4 avg4
#define AVG4_ROUNDER BYTE_VEC(0x02)
#define STORE(l, b) stq(l, b)
PIXOP(put, STORE);

#undef STORE
#define STORE(l, b) stq(AVG2(l, ldq(b)), b);
PIXOP(avg, STORE);

/* Not rounding primitives.  */
#undef AVG2
#undef AVG4
#undef AVG4_ROUNDER
#undef STORE
#define AVG2 avg2_no_rnd
#define AVG4 avg4_no_rnd
#define AVG4_ROUNDER BYTE_VEC(0x01)
#define STORE(l, b) stq(l, b)
PIXOP(put_no_rnd, STORE);

#undef STORE
#define STORE(l, b) stq(AVG2(l, ldq(b)), b);
PIXOP(avg_no_rnd, STORE);

static void put_pixels16_axp_asm(uint8_t *block, const uint8_t *pixels,
                                 ptrdiff_t line_size, int h)
{
    put_pixels_axp_asm(block,     pixels,     line_size, h);
    put_pixels_axp_asm(block + 8, pixels + 8, line_size, h);
}

av_cold void ff_hpeldsp_init_alpha(HpelDSPContext *c, int flags)
{
    c->put_pixels_tab[0][0] = put_pixels16_axp_asm;
    c->put_pixels_tab[0][1] = put_pixels16_x2_axp;
    c->put_pixels_tab[0][2] = put_pixels16_y2_axp;
    c->put_pixels_tab[0][3] = put_pixels16_xy2_axp;

    c->put_no_rnd_pixels_tab[0][0] = put_pixels16_axp_asm;
    c->put_no_rnd_pixels_tab[0][1] = put_no_rnd_pixels16_x2_axp;
    c->put_no_rnd_pixels_tab[0][2] = put_no_rnd_pixels16_y2_axp;
    c->put_no_rnd_pixels_tab[0][3] = put_no_rnd_pixels16_xy2_axp;

    c->avg_pixels_tab[0][0] = avg_pixels16_axp;
    c->avg_pixels_tab[0][1] = avg_pixels16_x2_axp;
    c->avg_pixels_tab[0][2] = avg_pixels16_y2_axp;
    c->avg_pixels_tab[0][3] = avg_pixels16_xy2_axp;

    c->avg_no_rnd_pixels_tab[0] = avg_no_rnd_pixels16_axp;
    c->avg_no_rnd_pixels_tab[1] = avg_no_rnd_pixels16_x2_axp;
    c->avg_no_rnd_pixels_tab[2] = avg_no_rnd_pixels16_y2_axp;
    c->avg_no_rnd_pixels_tab[3] = avg_no_rnd_pixels16_xy2_axp;

    c->put_pixels_tab[1][0] = put_pixels_axp_asm;
    c->put_pixels_tab[1][1] = put_pixels_x2_axp;
    c->put_pixels_tab[1][2] = put_pixels_y2_axp;
    c->put_pixels_tab[1][3] = put_pixels_xy2_axp;

    c->put_no_rnd_pixels_tab[1][0] = put_pixels_axp_asm;
    c->put_no_rnd_pixels_tab[1][1] = put_no_rnd_pixels_x2_axp;
    c->put_no_rnd_pixels_tab[1][2] = put_no_rnd_pixels_y2_axp;
    c->put_no_rnd_pixels_tab[1][3] = put_no_rnd_pixels_xy2_axp;

    c->avg_pixels_tab[1][0] = avg_pixels_axp;
    c->avg_pixels_tab[1][1] = avg_pixels_x2_axp;
    c->avg_pixels_tab[1][2] = avg_pixels_y2_axp;
    c->avg_pixels_tab[1][3] = avg_pixels_xy2_axp;
}
