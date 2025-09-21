/*
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

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/mpegvideoencdsp.h"

int ff_pix_sum16_sse2(const uint8_t *pix, ptrdiff_t line_size);
int ff_pix_sum16_xop(const uint8_t *pix, ptrdiff_t line_size);
int ff_pix_norm1_sse2(const uint8_t *pix, ptrdiff_t line_size);

#if HAVE_INLINE_ASM
#if HAVE_SSSE3_INLINE
#define SCALE_OFFSET -1

/*
 * pmulhrsw: dst[0 - 15] = (src[0 - 15] * dst[0 - 15] + 0x4000)[15 - 30]
 */
#define PMULHRW(x, y, s, o)                     \
    "pmulhrsw " #s ", " #x "            \n\t"   \
    "pmulhrsw " #s ", " #y "            \n\t"

#define MAX_ABS 512

static int try_8x8basis_ssse3(const int16_t rem[64], const int16_t weight[64], const int16_t basis[64], int scale)
{
    x86_reg i=0;

    av_assert2(FFABS(scale) < MAX_ABS);
    scale <<= 16 + SCALE_OFFSET - BASIS_SHIFT + RECON_SHIFT;

    __asm__ volatile(
        "pxor %%mm7, %%mm7              \n\t"
        "movd  %4, %%mm5                \n\t"
        "punpcklwd %%mm5, %%mm5         \n\t"
        "punpcklwd %%mm5, %%mm5         \n\t"
        ".p2align 4                     \n\t"
        "1:                             \n\t"
        "movq  (%1, %0), %%mm0          \n\t"
        "movq  8(%1, %0), %%mm1         \n\t"
        PMULHRW(%%mm0, %%mm1, %%mm5, %%mm6)
        "paddw (%2, %0), %%mm0          \n\t"
        "paddw 8(%2, %0), %%mm1         \n\t"
        "psraw $6, %%mm0                \n\t"
        "psraw $6, %%mm1                \n\t"
        "pmullw (%3, %0), %%mm0         \n\t"
        "pmullw 8(%3, %0), %%mm1        \n\t"
        "pmaddwd %%mm0, %%mm0           \n\t"
        "pmaddwd %%mm1, %%mm1           \n\t"
        "paddd %%mm1, %%mm0             \n\t"
        "psrld $4, %%mm0                \n\t"
        "paddd %%mm0, %%mm7             \n\t"
        "add $16, %0                    \n\t"
        "cmp $128, %0                   \n\t" //FIXME optimize & bench
        " jb 1b                         \n\t"
        "pshufw $0x0E, %%mm7, %%mm6     \n\t"
        "paddd %%mm6, %%mm7             \n\t" // faster than phaddd on core2
        "psrld $2, %%mm7                \n\t"
        "movd %%mm7, %0                 \n\t"

        : "+r" (i)
        : "r"(basis), "r"(rem), "r"(weight), "g"(scale)
    );
    return i;
}

static void add_8x8basis_ssse3(int16_t rem[64], const int16_t basis[64], int scale)
{
    x86_reg i=0;

    if (FFABS(scale) < MAX_ABS) {
        scale <<= 16 + SCALE_OFFSET - BASIS_SHIFT + RECON_SHIFT;
        __asm__ volatile(
                "movd  %3, %%mm5        \n\t"
                "punpcklwd %%mm5, %%mm5 \n\t"
                "punpcklwd %%mm5, %%mm5 \n\t"
                ".p2align 4             \n\t"
                "1:                     \n\t"
                "movq  (%1, %0), %%mm0  \n\t"
                "movq  8(%1, %0), %%mm1 \n\t"
                PMULHRW(%%mm0, %%mm1, %%mm5, %%mm6)
                "paddw (%2, %0), %%mm0  \n\t"
                "paddw 8(%2, %0), %%mm1 \n\t"
                "movq %%mm0, (%2, %0)   \n\t"
                "movq %%mm1, 8(%2, %0)  \n\t"
                "add $16, %0            \n\t"
                "cmp $128, %0           \n\t" // FIXME optimize & bench
                " jb 1b                 \n\t"

                : "+r" (i)
                : "r"(basis), "r"(rem), "g"(scale)
        );
    } else {
        for (i=0; i<8*8; i++) {
            rem[i] += (basis[i]*scale + (1<<(BASIS_SHIFT - RECON_SHIFT-1)))>>(BASIS_SHIFT - RECON_SHIFT);
        }
    }
}

#endif /* HAVE_SSSE3_INLINE */

/* Draw the edges of width 'w' of an image of size width, height */
static void draw_edges_mmx(uint8_t *buf, ptrdiff_t wrap, int width, int height,
                           int w, int h, int sides)
{
    uint8_t *ptr, *last_line;
    int i;

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
            "jnz               1b           \n\t"
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
            "jnz               1b               \n\t"
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
            "jnz               1b           \n\t"
            : "+r" (ptr)
            : "r" ((x86_reg) wrap), "r" ((x86_reg) width),
              "r" (ptr + wrap * height));
    }

    /* top and bottom + corners */
    buf -= w;
    last_line = buf + (height - 1) * wrap;
    if (sides & EDGE_TOP)
        for (i = 0; i < h; i++)
            // top
            memcpy(buf - (i + 1) * wrap, buf, width + w + w);
    if (sides & EDGE_BOTTOM)
        for (i = 0; i < h; i++)
            // bottom
            memcpy(last_line + (i + 1) * wrap, last_line, width + w + w);
}

#endif /* HAVE_INLINE_ASM */

av_cold void ff_mpegvideoencdsp_init_x86(MpegvideoEncDSPContext *c,
                                         AVCodecContext *avctx)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->pix_sum     = ff_pix_sum16_sse2;
        c->pix_norm1   = ff_pix_norm1_sse2;
    }

    if (EXTERNAL_XOP(cpu_flags)) {
        c->pix_sum     = ff_pix_sum16_xop;
    }

#if HAVE_INLINE_ASM

    if (INLINE_MMX(cpu_flags)) {
        if (avctx->bits_per_raw_sample <= 8) {
            c->draw_edges = draw_edges_mmx;
        }
    }

#if HAVE_SSSE3_INLINE
    if (INLINE_SSSE3(cpu_flags)) {
        if (!(avctx->flags & AV_CODEC_FLAG_BITEXACT)) {
            c->try_8x8basis = try_8x8basis_ssse3;
        }
        c->add_8x8basis = add_8x8basis_ssse3;
    }
#endif /* HAVE_SSSE3_INLINE */

#endif /* HAVE_INLINE_ASM */
}
