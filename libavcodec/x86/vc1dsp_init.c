/*
 * VC-1 and WMV3 - DSP functions MMX-optimized
 * Copyright (c) 2007 Christophe GISQUET <christophe.gisquet@free.fr>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/vc1dsp.h"
#include "dsputil_mmx.h"
#include "vc1dsp.h"
#include "config.h"

#define LOOP_FILTER(EXT) \
void ff_vc1_v_loop_filter4_ ## EXT(uint8_t *src, int stride, int pq); \
void ff_vc1_h_loop_filter4_ ## EXT(uint8_t *src, int stride, int pq); \
void ff_vc1_v_loop_filter8_ ## EXT(uint8_t *src, int stride, int pq); \
void ff_vc1_h_loop_filter8_ ## EXT(uint8_t *src, int stride, int pq); \
\
static void vc1_v_loop_filter16_ ## EXT(uint8_t *src, int stride, int pq) \
{ \
    ff_vc1_v_loop_filter8_ ## EXT(src,   stride, pq); \
    ff_vc1_v_loop_filter8_ ## EXT(src+8, stride, pq); \
} \
\
static void vc1_h_loop_filter16_ ## EXT(uint8_t *src, int stride, int pq) \
{ \
    ff_vc1_h_loop_filter8_ ## EXT(src,          stride, pq); \
    ff_vc1_h_loop_filter8_ ## EXT(src+8*stride, stride, pq); \
}

#if HAVE_YASM
LOOP_FILTER(mmxext)
LOOP_FILTER(sse2)
LOOP_FILTER(ssse3)

void ff_vc1_h_loop_filter8_sse4(uint8_t *src, int stride, int pq);

static void vc1_h_loop_filter16_sse4(uint8_t *src, int stride, int pq)
{
    ff_vc1_h_loop_filter8_sse4(src,          stride, pq);
    ff_vc1_h_loop_filter8_sse4(src+8*stride, stride, pq);
}

static void avg_vc1_mspel_mc00_mmxext(uint8_t *dst, const uint8_t *src,
                                      ptrdiff_t stride, int rnd)
{
    ff_avg_pixels8_mmxext(dst, src, stride, 8);
}
#endif /* HAVE_YASM */

void ff_put_vc1_chroma_mc8_nornd_mmx  (uint8_t *dst, uint8_t *src,
                                       int stride, int h, int x, int y);
void ff_avg_vc1_chroma_mc8_nornd_mmxext(uint8_t *dst, uint8_t *src,
                                        int stride, int h, int x, int y);
void ff_avg_vc1_chroma_mc8_nornd_3dnow(uint8_t *dst, uint8_t *src,
                                       int stride, int h, int x, int y);
void ff_put_vc1_chroma_mc8_nornd_ssse3(uint8_t *dst, uint8_t *src,
                                       int stride, int h, int x, int y);
void ff_avg_vc1_chroma_mc8_nornd_ssse3(uint8_t *dst, uint8_t *src,
                                       int stride, int h, int x, int y);


av_cold void ff_vc1dsp_init_x86(VC1DSPContext *dsp)
{
    int mm_flags = av_get_cpu_flags();

    if (INLINE_MMX(mm_flags))
        ff_vc1dsp_init_mmx(dsp);

    if (INLINE_MMXEXT(mm_flags))
        ff_vc1dsp_init_mmxext(dsp);

#define ASSIGN_LF(EXT) \
        dsp->vc1_v_loop_filter4  = ff_vc1_v_loop_filter4_ ## EXT; \
        dsp->vc1_h_loop_filter4  = ff_vc1_h_loop_filter4_ ## EXT; \
        dsp->vc1_v_loop_filter8  = ff_vc1_v_loop_filter8_ ## EXT; \
        dsp->vc1_h_loop_filter8  = ff_vc1_h_loop_filter8_ ## EXT; \
        dsp->vc1_v_loop_filter16 = vc1_v_loop_filter16_ ## EXT; \
        dsp->vc1_h_loop_filter16 = vc1_h_loop_filter16_ ## EXT

#if HAVE_YASM
    if (mm_flags & AV_CPU_FLAG_MMX) {
        dsp->put_no_rnd_vc1_chroma_pixels_tab[0] = ff_put_vc1_chroma_mc8_nornd_mmx;
    }

    if (mm_flags & AV_CPU_FLAG_MMXEXT) {
        ASSIGN_LF(mmxext);
        dsp->avg_no_rnd_vc1_chroma_pixels_tab[0] = ff_avg_vc1_chroma_mc8_nornd_mmxext;

        dsp->avg_vc1_mspel_pixels_tab[0]         = avg_vc1_mspel_mc00_mmxext;
    } else if (mm_flags & AV_CPU_FLAG_3DNOW) {
        dsp->avg_no_rnd_vc1_chroma_pixels_tab[0] = ff_avg_vc1_chroma_mc8_nornd_3dnow;
    }

    if (mm_flags & AV_CPU_FLAG_SSE2) {
        dsp->vc1_v_loop_filter8  = ff_vc1_v_loop_filter8_sse2;
        dsp->vc1_h_loop_filter8  = ff_vc1_h_loop_filter8_sse2;
        dsp->vc1_v_loop_filter16 = vc1_v_loop_filter16_sse2;
        dsp->vc1_h_loop_filter16 = vc1_h_loop_filter16_sse2;
    }
    if (mm_flags & AV_CPU_FLAG_SSSE3) {
        ASSIGN_LF(ssse3);
        dsp->put_no_rnd_vc1_chroma_pixels_tab[0] = ff_put_vc1_chroma_mc8_nornd_ssse3;
        dsp->avg_no_rnd_vc1_chroma_pixels_tab[0] = ff_avg_vc1_chroma_mc8_nornd_ssse3;
    }
    if (mm_flags & AV_CPU_FLAG_SSE4) {
        dsp->vc1_h_loop_filter8  = ff_vc1_h_loop_filter8_sse4;
        dsp->vc1_h_loop_filter16 = vc1_h_loop_filter16_sse4;
    }
#endif /* HAVE_YASM */
}
