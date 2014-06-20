/*
 * MMX optimized DSP utils
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
 */

#include "config.h"
#include "libavutil/avassert.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavcodec/pixels.h"
#include "libavcodec/videodsp.h"
#include "constants.h"
#include "dsputil_x86.h"
#include "inline_asm.h"

#if HAVE_INLINE_ASM

void ff_put_pixels_clamped_mmx(const int16_t *block, uint8_t *pixels,
                               int line_size)
{
    const int16_t *p;
    uint8_t *pix;

    /* read the pixels */
    p   = block;
    pix = pixels;
    /* unrolled loop */
    __asm__ volatile (
        "movq      (%3), %%mm0          \n\t"
        "movq     8(%3), %%mm1          \n\t"
        "movq    16(%3), %%mm2          \n\t"
        "movq    24(%3), %%mm3          \n\t"
        "movq    32(%3), %%mm4          \n\t"
        "movq    40(%3), %%mm5          \n\t"
        "movq    48(%3), %%mm6          \n\t"
        "movq    56(%3), %%mm7          \n\t"
        "packuswb %%mm1, %%mm0          \n\t"
        "packuswb %%mm3, %%mm2          \n\t"
        "packuswb %%mm5, %%mm4          \n\t"
        "packuswb %%mm7, %%mm6          \n\t"
        "movq     %%mm0, (%0)           \n\t"
        "movq     %%mm2, (%0, %1)       \n\t"
        "movq     %%mm4, (%0, %1, 2)    \n\t"
        "movq     %%mm6, (%0, %2)       \n\t"
        :: "r" (pix), "r" ((x86_reg) line_size), "r" ((x86_reg) line_size * 3),
           "r" (p)
        : "memory");
    pix += line_size * 4;
    p   += 32;

    // if here would be an exact copy of the code above
    // compiler would generate some very strange code
    // thus using "r"
    __asm__ volatile (
        "movq       (%3), %%mm0         \n\t"
        "movq      8(%3), %%mm1         \n\t"
        "movq     16(%3), %%mm2         \n\t"
        "movq     24(%3), %%mm3         \n\t"
        "movq     32(%3), %%mm4         \n\t"
        "movq     40(%3), %%mm5         \n\t"
        "movq     48(%3), %%mm6         \n\t"
        "movq     56(%3), %%mm7         \n\t"
        "packuswb  %%mm1, %%mm0         \n\t"
        "packuswb  %%mm3, %%mm2         \n\t"
        "packuswb  %%mm5, %%mm4         \n\t"
        "packuswb  %%mm7, %%mm6         \n\t"
        "movq      %%mm0, (%0)          \n\t"
        "movq      %%mm2, (%0, %1)      \n\t"
        "movq      %%mm4, (%0, %1, 2)   \n\t"
        "movq      %%mm6, (%0, %2)      \n\t"
        :: "r" (pix), "r" ((x86_reg) line_size), "r" ((x86_reg) line_size * 3),
           "r" (p)
        : "memory");
}

void ff_add_pixels_clamped_mmx(const int16_t *block, uint8_t *pixels,
                               int line_size)
{
    const int16_t *p;
    uint8_t *pix;
    int i;

    /* read the pixels */
    p   = block;
    pix = pixels;
    MOVQ_ZERO(mm7);
    i = 4;
    do {
        __asm__ volatile (
            "movq        (%2), %%mm0    \n\t"
            "movq       8(%2), %%mm1    \n\t"
            "movq      16(%2), %%mm2    \n\t"
            "movq      24(%2), %%mm3    \n\t"
            "movq          %0, %%mm4    \n\t"
            "movq          %1, %%mm6    \n\t"
            "movq       %%mm4, %%mm5    \n\t"
            "punpcklbw  %%mm7, %%mm4    \n\t"
            "punpckhbw  %%mm7, %%mm5    \n\t"
            "paddsw     %%mm4, %%mm0    \n\t"
            "paddsw     %%mm5, %%mm1    \n\t"
            "movq       %%mm6, %%mm5    \n\t"
            "punpcklbw  %%mm7, %%mm6    \n\t"
            "punpckhbw  %%mm7, %%mm5    \n\t"
            "paddsw     %%mm6, %%mm2    \n\t"
            "paddsw     %%mm5, %%mm3    \n\t"
            "packuswb   %%mm1, %%mm0    \n\t"
            "packuswb   %%mm3, %%mm2    \n\t"
            "movq       %%mm0, %0       \n\t"
            "movq       %%mm2, %1       \n\t"
            : "+m" (*pix), "+m" (*(pix + line_size))
            : "r" (p)
            : "memory");
        pix += line_size * 2;
        p   += 16;
    } while (--i);
}

/* Draw the edges of width 'w' of an image of size width, height
 * this MMX version can only handle w == 8 || w == 16. */
void ff_draw_edges_mmx(uint8_t *buf, int wrap, int width, int height,
                       int w, int h, int sides)
{
    uint8_t *ptr, *last_line;
    int i;

    last_line = buf + (height - 1) * wrap;
    /* left and right */
    ptr = buf;
    if (w == 8) {
        __asm__ volatile (
            "1:                             \n\t"
            "movd            (%0), %%mm0    \n\t"
            "punpcklbw      %%mm0, %%mm0    \n\t"
            "punpcklwd      %%mm0, %%mm0    \n\t"
            "punpckldq      %%mm0, %%mm0    \n\t"
            "movq           %%mm0, -8(%0)   \n\t"
            "movq      -8(%0, %2), %%mm1    \n\t"
            "punpckhbw      %%mm1, %%mm1    \n\t"
            "punpckhwd      %%mm1, %%mm1    \n\t"
            "punpckhdq      %%mm1, %%mm1    \n\t"
            "movq           %%mm1, (%0, %2) \n\t"
            "add               %1, %0       \n\t"
            "cmp               %3, %0       \n\t"
            "jb                1b           \n\t"
            : "+r" (ptr)
            : "r" ((x86_reg) wrap), "r" ((x86_reg) width),
              "r" (ptr + wrap * height));
    } else if (w == 16) {
        __asm__ volatile (
            "1:                                 \n\t"
            "movd            (%0), %%mm0        \n\t"
            "punpcklbw      %%mm0, %%mm0        \n\t"
            "punpcklwd      %%mm0, %%mm0        \n\t"
            "punpckldq      %%mm0, %%mm0        \n\t"
            "movq           %%mm0, -8(%0)       \n\t"
            "movq           %%mm0, -16(%0)      \n\t"
            "movq      -8(%0, %2), %%mm1        \n\t"
            "punpckhbw      %%mm1, %%mm1        \n\t"
            "punpckhwd      %%mm1, %%mm1        \n\t"
            "punpckhdq      %%mm1, %%mm1        \n\t"
            "movq           %%mm1,  (%0, %2)    \n\t"
            "movq           %%mm1, 8(%0, %2)    \n\t"
            "add               %1, %0           \n\t"
            "cmp               %3, %0           \n\t"
            "jb                1b               \n\t"
            : "+r"(ptr)
            : "r"((x86_reg)wrap), "r"((x86_reg)width), "r"(ptr + wrap * height)
            );
    } else {
        av_assert1(w == 4);
        __asm__ volatile (
            "1:                             \n\t"
            "movd            (%0), %%mm0    \n\t"
            "punpcklbw      %%mm0, %%mm0    \n\t"
            "punpcklwd      %%mm0, %%mm0    \n\t"
            "movd           %%mm0, -4(%0)   \n\t"
            "movd      -4(%0, %2), %%mm1    \n\t"
            "punpcklbw      %%mm1, %%mm1    \n\t"
            "punpckhwd      %%mm1, %%mm1    \n\t"
            "punpckhdq      %%mm1, %%mm1    \n\t"
            "movd           %%mm1, (%0, %2) \n\t"
            "add               %1, %0       \n\t"
            "cmp               %3, %0       \n\t"
            "jb                1b           \n\t"
            : "+r" (ptr)
            : "r" ((x86_reg) wrap), "r" ((x86_reg) width),
              "r" (ptr + wrap * height));
    }

    /* top and bottom (and hopefully also the corners) */
    if (sides & EDGE_TOP) {
        for (i = 0; i < h; i += 4) {
            ptr = buf - (i + 1) * wrap - w;
            __asm__ volatile (
                "1:                             \n\t"
                "movq (%1, %0), %%mm0           \n\t"
                "movq    %%mm0, (%0)            \n\t"
                "movq    %%mm0, (%0, %2)        \n\t"
                "movq    %%mm0, (%0, %2, 2)     \n\t"
                "movq    %%mm0, (%0, %3)        \n\t"
                "add        $8, %0              \n\t"
                "cmp        %4, %0              \n\t"
                "jb         1b                  \n\t"
                : "+r" (ptr)
                : "r" ((x86_reg) buf - (x86_reg) ptr - w),
                  "r" ((x86_reg) - wrap), "r" ((x86_reg) - wrap * 3),
                  "r" (ptr + width + 2 * w));
        }
    }

    if (sides & EDGE_BOTTOM) {
        for (i = 0; i < h; i += 4) {
            ptr = last_line + (i + 1) * wrap - w;
            __asm__ volatile (
                "1:                             \n\t"
                "movq (%1, %0), %%mm0           \n\t"
                "movq    %%mm0, (%0)            \n\t"
                "movq    %%mm0, (%0, %2)        \n\t"
                "movq    %%mm0, (%0, %2, 2)     \n\t"
                "movq    %%mm0, (%0, %3)        \n\t"
                "add        $8, %0              \n\t"
                "cmp        %4, %0              \n\t"
                "jb         1b                  \n\t"
                : "+r" (ptr)
                : "r" ((x86_reg) last_line - (x86_reg) ptr - w),
                  "r" ((x86_reg) wrap), "r" ((x86_reg) wrap * 3),
                  "r" (ptr + width + 2 * w));
        }
    }
}

typedef void emulated_edge_mc_func(uint8_t *dst, const uint8_t *src,
                                   ptrdiff_t dst_stride,
                                   ptrdiff_t src_linesize,
                                   int block_w, int block_h,
                                   int src_x, int src_y, int w, int h);

static av_always_inline void gmc(uint8_t *dst, uint8_t *src,
                                 int stride, int h, int ox, int oy,
                                 int dxx, int dxy, int dyx, int dyy,
                                 int shift, int r, int width, int height,
                                 emulated_edge_mc_func *emu_edge_fn)
{
    const int w    = 8;
    const int ix   = ox  >> (16 + shift);
    const int iy   = oy  >> (16 + shift);
    const int oxs  = ox  >> 4;
    const int oys  = oy  >> 4;
    const int dxxs = dxx >> 4;
    const int dxys = dxy >> 4;
    const int dyxs = dyx >> 4;
    const int dyys = dyy >> 4;
    const uint16_t r4[4]   = { r, r, r, r };
    const uint16_t dxy4[4] = { dxys, dxys, dxys, dxys };
    const uint16_t dyy4[4] = { dyys, dyys, dyys, dyys };
    const uint64_t shift2  = 2 * shift;
#define MAX_STRIDE 4096U
#define MAX_H 8U
    uint8_t edge_buf[(MAX_H + 1) * MAX_STRIDE];
    int x, y;

    const int dxw = (dxx - (1 << (16 + shift))) * (w - 1);
    const int dyh = (dyy - (1 << (16 + shift))) * (h - 1);
    const int dxh = dxy * (h - 1);
    const int dyw = dyx * (w - 1);
    int need_emu  =  (unsigned) ix >= width  - w ||
                     (unsigned) iy >= height - h;

    if ( // non-constant fullpel offset (3% of blocks)
        ((ox ^ (ox + dxw)) | (ox ^ (ox + dxh)) | (ox ^ (ox + dxw + dxh)) |
         (oy ^ (oy + dyw)) | (oy ^ (oy + dyh)) | (oy ^ (oy + dyw + dyh))) >> (16 + shift) ||
        // uses more than 16 bits of subpel mv (only at huge resolution)
        (dxx | dxy | dyx | dyy) & 15 ||
        (need_emu && (h > MAX_H || stride > MAX_STRIDE))) {
        // FIXME could still use mmx for some of the rows
        ff_gmc_c(dst, src, stride, h, ox, oy, dxx, dxy, dyx, dyy,
                 shift, r, width, height);
        return;
    }

    src += ix + iy * stride;
    if (need_emu) {
        emu_edge_fn(edge_buf, src, stride, stride, w + 1, h + 1, ix, iy, width, height);
        src = edge_buf;
    }

    __asm__ volatile (
        "movd         %0, %%mm6         \n\t"
        "pxor      %%mm7, %%mm7         \n\t"
        "punpcklwd %%mm6, %%mm6         \n\t"
        "punpcklwd %%mm6, %%mm6         \n\t"
        :: "r" (1 << shift));

    for (x = 0; x < w; x += 4) {
        uint16_t dx4[4] = { oxs - dxys + dxxs * (x + 0),
                            oxs - dxys + dxxs * (x + 1),
                            oxs - dxys + dxxs * (x + 2),
                            oxs - dxys + dxxs * (x + 3) };
        uint16_t dy4[4] = { oys - dyys + dyxs * (x + 0),
                            oys - dyys + dyxs * (x + 1),
                            oys - dyys + dyxs * (x + 2),
                            oys - dyys + dyxs * (x + 3) };

        for (y = 0; y < h; y++) {
            __asm__ volatile (
                "movq      %0, %%mm4    \n\t"
                "movq      %1, %%mm5    \n\t"
                "paddw     %2, %%mm4    \n\t"
                "paddw     %3, %%mm5    \n\t"
                "movq   %%mm4, %0       \n\t"
                "movq   %%mm5, %1       \n\t"
                "psrlw    $12, %%mm4    \n\t"
                "psrlw    $12, %%mm5    \n\t"
                : "+m" (*dx4), "+m" (*dy4)
                : "m" (*dxy4), "m" (*dyy4));

            __asm__ volatile (
                "movq      %%mm6, %%mm2 \n\t"
                "movq      %%mm6, %%mm1 \n\t"
                "psubw     %%mm4, %%mm2 \n\t"
                "psubw     %%mm5, %%mm1 \n\t"
                "movq      %%mm2, %%mm0 \n\t"
                "movq      %%mm4, %%mm3 \n\t"
                "pmullw    %%mm1, %%mm0 \n\t" // (s - dx) * (s - dy)
                "pmullw    %%mm5, %%mm3 \n\t" // dx * dy
                "pmullw    %%mm5, %%mm2 \n\t" // (s - dx) * dy
                "pmullw    %%mm4, %%mm1 \n\t" // dx * (s - dy)

                "movd         %4, %%mm5 \n\t"
                "movd         %3, %%mm4 \n\t"
                "punpcklbw %%mm7, %%mm5 \n\t"
                "punpcklbw %%mm7, %%mm4 \n\t"
                "pmullw    %%mm5, %%mm3 \n\t" // src[1, 1] * dx * dy
                "pmullw    %%mm4, %%mm2 \n\t" // src[0, 1] * (s - dx) * dy

                "movd         %2, %%mm5 \n\t"
                "movd         %1, %%mm4 \n\t"
                "punpcklbw %%mm7, %%mm5 \n\t"
                "punpcklbw %%mm7, %%mm4 \n\t"
                "pmullw    %%mm5, %%mm1 \n\t" // src[1, 0] * dx * (s - dy)
                "pmullw    %%mm4, %%mm0 \n\t" // src[0, 0] * (s - dx) * (s - dy)
                "paddw        %5, %%mm1 \n\t"
                "paddw     %%mm3, %%mm2 \n\t"
                "paddw     %%mm1, %%mm0 \n\t"
                "paddw     %%mm2, %%mm0 \n\t"

                "psrlw        %6, %%mm0 \n\t"
                "packuswb  %%mm0, %%mm0 \n\t"
                "movd      %%mm0, %0    \n\t"

                : "=m" (dst[x + y * stride])
                : "m" (src[0]), "m" (src[1]),
                  "m" (src[stride]), "m" (src[stride + 1]),
                  "m" (*r4), "m" (shift2));
            src += stride;
        }
        src += 4 - h * stride;
    }
}

#if CONFIG_VIDEODSP
#if HAVE_YASM
#if ARCH_X86_32
void ff_gmc_mmx(uint8_t *dst, uint8_t *src,
                int stride, int h, int ox, int oy,
                int dxx, int dxy, int dyx, int dyy,
                int shift, int r, int width, int height)
{
    gmc(dst, src, stride, h, ox, oy, dxx, dxy, dyx, dyy, shift, r,
        width, height, &ff_emulated_edge_mc_8);
}
#endif
void ff_gmc_sse(uint8_t *dst, uint8_t *src,
                int stride, int h, int ox, int oy,
                int dxx, int dxy, int dyx, int dyy,
                int shift, int r, int width, int height)
{
    gmc(dst, src, stride, h, ox, oy, dxx, dxy, dyx, dyy, shift, r,
        width, height, &ff_emulated_edge_mc_8);
}
#else
void ff_gmc_mmx(uint8_t *dst, uint8_t *src,
                int stride, int h, int ox, int oy,
                int dxx, int dxy, int dyx, int dyy,
                int shift, int r, int width, int height)
{
    gmc(dst, src, stride, h, ox, oy, dxx, dxy, dyx, dyy, shift, r,
        width, height, &ff_emulated_edge_mc_8);
}
#endif
#endif
#endif /* HAVE_INLINE_ASM */
