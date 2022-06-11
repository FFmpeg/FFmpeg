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

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/mpegvideoencdsp.h"

int ff_pix_sum16_sse2(uint8_t *pix, int line_size);
int ff_pix_sum16_xop(uint8_t *pix, int line_size);
int ff_pix_norm1_sse2(uint8_t *pix, int line_size);

#if HAVE_INLINE_ASM

#define PHADDD(a, t)                            \
    "movq  " #a ", " #t "               \n\t"   \
    "psrlq    $32, " #a "               \n\t"   \
    "paddd " #t ", " #a "               \n\t"

/*
 * pmulhw:   dst[0 - 15] = (src[0 - 15] * dst[0 - 15])[16 - 31]
 * pmulhrw:  dst[0 - 15] = (src[0 - 15] * dst[0 - 15] + 0x8000)[16 - 31]
 * pmulhrsw: dst[0 - 15] = (src[0 - 15] * dst[0 - 15] + 0x4000)[15 - 30]
 */
#define PMULHRW(x, y, s, o)                     \
    "pmulhw " #s ", " #x "              \n\t"   \
    "pmulhw " #s ", " #y "              \n\t"   \
    "paddw  " #o ", " #x "              \n\t"   \
    "paddw  " #o ", " #y "              \n\t"   \
    "psraw      $1, " #x "              \n\t"   \
    "psraw      $1, " #y "              \n\t"
#define DEF(x) x ## _mmx
#define SET_RND MOVQ_WONE
#define SCALE_OFFSET 1

#include "mpegvideoenc_qns_template.c"

#undef DEF
#undef SET_RND
#undef SCALE_OFFSET
#undef PMULHRW

#define DEF(x) x ## _3dnow
#define SET_RND(x)
#define SCALE_OFFSET 0
#define PMULHRW(x, y, s, o)                     \
    "pmulhrw " #s ", " #x "             \n\t"   \
    "pmulhrw " #s ", " #y "             \n\t"

#include "mpegvideoenc_qns_template.c"

#undef DEF
#undef SET_RND
#undef SCALE_OFFSET
#undef PMULHRW

#if HAVE_SSSE3_INLINE
#undef PHADDD
#define DEF(x) x ## _ssse3
#define SET_RND(x)
#define SCALE_OFFSET -1

#define PHADDD(a, t)                            \
    "pshufw $0x0E, " #a ", " #t "       \n\t"   \
    /* faster than phaddd on core2 */           \
    "paddd " #t ", " #a "               \n\t"

#define PMULHRW(x, y, s, o)                     \
    "pmulhrsw " #s ", " #x "            \n\t"   \
    "pmulhrsw " #s ", " #y "            \n\t"

#include "mpegvideoenc_qns_template.c"

#undef DEF
#undef SET_RND
#undef SCALE_OFFSET
#undef PMULHRW
#undef PHADDD
#endif /* HAVE_SSSE3_INLINE */

/* Draw the edges of width 'w' of an image of size width, height
 * this MMX version can only handle w == 8 || w == 16. */
static void draw_edges_mmx(uint8_t *buf, int wrap, int width, int height,
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
        if (!(avctx->flags & AV_CODEC_FLAG_BITEXACT)) {
            c->try_8x8basis = try_8x8basis_mmx;
        }
        c->add_8x8basis = add_8x8basis_mmx;

        if (avctx->bits_per_raw_sample <= 8) {
            c->draw_edges = draw_edges_mmx;
        }
    }

    if (INLINE_AMD3DNOW(cpu_flags)) {
        if (!(avctx->flags & AV_CODEC_FLAG_BITEXACT)) {
            c->try_8x8basis = try_8x8basis_3dnow;
        }
        c->add_8x8basis = add_8x8basis_3dnow;
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
