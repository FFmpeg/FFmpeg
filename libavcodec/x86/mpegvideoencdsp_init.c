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
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/mpegvideoencdsp.h"

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

#endif /* HAVE_INLINE_ASM */

av_cold void ff_mpegvideoencdsp_init_x86(MpegvideoEncDSPContext *c,
                                         AVCodecContext *avctx)
{
#if HAVE_INLINE_ASM
    int cpu_flags = av_get_cpu_flags();

    if (INLINE_MMX(cpu_flags)) {
        if (!(avctx->flags & CODEC_FLAG_BITEXACT)) {
            c->try_8x8basis = try_8x8basis_mmx;
        }
        c->add_8x8basis = add_8x8basis_mmx;
    }

    if (INLINE_AMD3DNOW(cpu_flags)) {
        if (!(avctx->flags & CODEC_FLAG_BITEXACT)) {
            c->try_8x8basis = try_8x8basis_3dnow;
        }
        c->add_8x8basis = add_8x8basis_3dnow;
    }

#if HAVE_SSSE3_INLINE
    if (INLINE_SSSE3(cpu_flags)) {
        if (!(avctx->flags & CODEC_FLAG_BITEXACT)) {
            c->try_8x8basis = try_8x8basis_ssse3;
        }
        c->add_8x8basis = add_8x8basis_ssse3;
    }
#endif /* HAVE_SSSE3_INLINE */

#endif /* HAVE_INLINE_ASM */
}
