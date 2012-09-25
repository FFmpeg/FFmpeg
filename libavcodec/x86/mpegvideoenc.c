/*
 * The simplest mpeg encoder (well, it was the simplest!)
 * Copyright (c) 2000,2001 Fabrice Bellard
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

#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/mpegvideo.h"
#include "dsputil_mmx.h"

extern uint16_t ff_inv_zigzag_direct16[64];

#if HAVE_MMX_INLINE
#define COMPILE_TEMPLATE_MMXEXT 0
#define COMPILE_TEMPLATE_SSE2   0
#define COMPILE_TEMPLATE_SSSE3  0
#define RENAME(a) a ## _MMX
#define RENAMEl(a) a ## _mmx
#include "mpegvideoenc_template.c"
#endif /* HAVE_MMX_INLINE */

#if HAVE_MMXEXT_INLINE
#undef COMPILE_TEMPLATE_SSSE3
#undef COMPILE_TEMPLATE_SSE2
#undef COMPILE_TEMPLATE_MMXEXT
#define COMPILE_TEMPLATE_MMXEXT 1
#define COMPILE_TEMPLATE_SSE2   0
#define COMPILE_TEMPLATE_SSSE3  0
#undef RENAME
#undef RENAMEl
#define RENAME(a) a ## _MMX2
#define RENAMEl(a) a ## _mmx2
#include "mpegvideoenc_template.c"
#endif /* HAVE_MMXEXT_INLINE */

#if HAVE_SSE2_INLINE
#undef COMPILE_TEMPLATE_MMXEXT
#undef COMPILE_TEMPLATE_SSE2
#undef COMPILE_TEMPLATE_SSSE3
#define COMPILE_TEMPLATE_MMXEXT 0
#define COMPILE_TEMPLATE_SSE2   1
#define COMPILE_TEMPLATE_SSSE3  0
#undef RENAME
#undef RENAMEl
#define RENAME(a) a ## _SSE2
#define RENAMEl(a) a ## _sse2
#include "mpegvideoenc_template.c"
#endif /* HAVE_SSE2_INLINE */

#if HAVE_SSSE3_INLINE
#undef COMPILE_TEMPLATE_MMXEXT
#undef COMPILE_TEMPLATE_SSE2
#undef COMPILE_TEMPLATE_SSSE3
#define COMPILE_TEMPLATE_MMXEXT 0
#define COMPILE_TEMPLATE_SSE2   1
#define COMPILE_TEMPLATE_SSSE3  1
#undef RENAME
#undef RENAMEl
#define RENAME(a) a ## _SSSE3
#define RENAMEl(a) a ## _sse2
#include "mpegvideoenc_template.c"
#endif /* HAVE_SSSE3_INLINE */

void ff_dct_encode_init_x86(MpegEncContext *s)
{
    int mm_flags = av_get_cpu_flags();
    const int dct_algo = s->avctx->dct_algo;

    if (dct_algo == FF_DCT_AUTO || dct_algo == FF_DCT_MMX) {
#if HAVE_MMX_INLINE
        if (INLINE_MMX(mm_flags))
            s->dct_quantize = dct_quantize_MMX;
#endif
#if HAVE_MMXEXT_INLINE
        if (INLINE_MMXEXT(mm_flags))
            s->dct_quantize = dct_quantize_MMX2;
#endif
#if HAVE_SSE2_INLINE
        if (INLINE_SSE2(mm_flags))
            s->dct_quantize = dct_quantize_SSE2;
#endif
#if HAVE_SSSE3_INLINE
        if (INLINE_SSSE3(mm_flags))
            s->dct_quantize = dct_quantize_SSSE3;
#endif
    }
}
