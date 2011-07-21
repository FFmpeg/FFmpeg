/*
 * Alpha optimized DSP utils
 * Copyright (c) 2002 Falk Hueffner <falk@debian.org>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavcodec/dsputil.h"
#include "dsputil_alpha.h"
#include "asm.h"

void (*put_pixels_clamped_axp_p)(const DCTELEM *block, uint8_t *pixels,
                                 int line_size);
void (*add_pixels_clamped_axp_p)(const DCTELEM *block, uint8_t *pixels,
                                 int line_size);

#if 0
/* These functions were the base for the optimized assembler routines,
   and remain here for documentation purposes.  */
static void put_pixels_clamped_mvi(const DCTELEM *block, uint8_t *pixels,
                                   int line_size)
{
    int i = 8;
    uint64_t clampmask = zap(-1, 0xaa); /* 0x00ff00ff00ff00ff */

    do {
        uint64_t shorts0, shorts1;

        shorts0 = ldq(block);
        shorts0 = maxsw4(shorts0, 0);
        shorts0 = minsw4(shorts0, clampmask);
        stl(pkwb(shorts0), pixels);

        shorts1 = ldq(block + 4);
        shorts1 = maxsw4(shorts1, 0);
        shorts1 = minsw4(shorts1, clampmask);
        stl(pkwb(shorts1), pixels + 4);

        pixels += line_size;
        block += 8;
    } while (--i);
}

void add_pixels_clamped_mvi(const DCTELEM *block, uint8_t *pixels,
                            int line_size)
{
    int h = 8;
    /* Keep this function a leaf function by generating the constants
       manually (mainly for the hack value ;-).  */
    uint64_t clampmask = zap(-1, 0xaa); /* 0x00ff00ff00ff00ff */
    uint64_t signmask  = zap(-1, 0x33);
    signmask ^= signmask >> 1;  /* 0x8000800080008000 */

    do {
        uint64_t shorts0, pix0, signs0;
        uint64_t shorts1, pix1, signs1;

        shorts0 = ldq(block);
        shorts1 = ldq(block + 4);

        pix0    = unpkbw(ldl(pixels));
        /* Signed subword add (MMX paddw).  */
        signs0  = shorts0 & signmask;
        shorts0 &= ~signmask;
        shorts0 += pix0;
        shorts0 ^= signs0;
        /* Clamp. */
        shorts0 = maxsw4(shorts0, 0);
        shorts0 = minsw4(shorts0, clampmask);

        /* Next 4.  */
        pix1    = unpkbw(ldl(pixels + 4));
        signs1  = shorts1 & signmask;
        shorts1 &= ~signmask;
        shorts1 += pix1;
        shorts1 ^= signs1;
        shorts1 = maxsw4(shorts1, 0);
        shorts1 = minsw4(shorts1, clampmask);

        stl(pkwb(shorts0), pixels);
        stl(pkwb(shorts1), pixels + 4);

        pixels += line_size;
        block += 8;
    } while (--h);
}
#endif

static void clear_blocks_axp(DCTELEM *blocks) {
    uint64_t *p = (uint64_t *) blocks;
    int n = sizeof(DCTELEM) * 6 * 64;

    do {
        p[0] = 0;
        p[1] = 0;
        p[2] = 0;
        p[3] = 0;
        p[4] = 0;
        p[5] = 0;
        p[6] = 0;
        p[7] = 0;
        p += 8;
        n -= 8 * 8;
    } while (n);
}

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
         int line_size, int h)                                              \
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
         int line_size, int h)                                              \
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
                                 int line_size, int h)
{
    put_pixels_axp_asm(block,     pixels,     line_size, h);
    put_pixels_axp_asm(block + 8, pixels + 8, line_size, h);
}

void dsputil_init_alpha(DSPContext* c, AVCodecContext *avctx)
{
    const int high_bit_depth = avctx->bits_per_raw_sample > 8;

    if (!high_bit_depth) {
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

    c->avg_no_rnd_pixels_tab[0][0] = avg_no_rnd_pixels16_axp;
    c->avg_no_rnd_pixels_tab[0][1] = avg_no_rnd_pixels16_x2_axp;
    c->avg_no_rnd_pixels_tab[0][2] = avg_no_rnd_pixels16_y2_axp;
    c->avg_no_rnd_pixels_tab[0][3] = avg_no_rnd_pixels16_xy2_axp;

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

    c->avg_no_rnd_pixels_tab[1][0] = avg_no_rnd_pixels_axp;
    c->avg_no_rnd_pixels_tab[1][1] = avg_no_rnd_pixels_x2_axp;
    c->avg_no_rnd_pixels_tab[1][2] = avg_no_rnd_pixels_y2_axp;
    c->avg_no_rnd_pixels_tab[1][3] = avg_no_rnd_pixels_xy2_axp;

    c->clear_blocks = clear_blocks_axp;
    }

    /* amask clears all bits that correspond to present features.  */
    if (amask(AMASK_MVI) == 0) {
        c->put_pixels_clamped = put_pixels_clamped_mvi_asm;
        c->add_pixels_clamped = add_pixels_clamped_mvi_asm;

        if (!high_bit_depth)
            c->get_pixels   = get_pixels_mvi;
        c->diff_pixels      = diff_pixels_mvi;
        c->sad[0]           = pix_abs16x16_mvi_asm;
        c->sad[1]           = pix_abs8x8_mvi;
        c->pix_abs[0][0]    = pix_abs16x16_mvi_asm;
        c->pix_abs[1][0]    = pix_abs8x8_mvi;
        c->pix_abs[0][1]    = pix_abs16x16_x2_mvi;
        c->pix_abs[0][2]    = pix_abs16x16_y2_mvi;
        c->pix_abs[0][3]    = pix_abs16x16_xy2_mvi;
    }

    put_pixels_clamped_axp_p = c->put_pixels_clamped;
    add_pixels_clamped_axp_p = c->add_pixels_clamped;

    if (!avctx->lowres && avctx->bits_per_raw_sample <= 8 &&
        (avctx->idct_algo == FF_IDCT_AUTO ||
         avctx->idct_algo == FF_IDCT_SIMPLEALPHA)) {
        c->idct_put = ff_simple_idct_put_axp;
        c->idct_add = ff_simple_idct_add_axp;
        c->idct =     ff_simple_idct_axp;
    }
}
