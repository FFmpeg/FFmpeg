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

#include "config.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/h264chroma.h"

void ff_put_h264_chroma_mc8_rnd_mmx  (uint8_t *dst, uint8_t *src,
                                      int stride, int h, int x, int y);
void ff_avg_h264_chroma_mc8_rnd_mmxext(uint8_t *dst, uint8_t *src,
                                       int stride, int h, int x, int y);
void ff_avg_h264_chroma_mc8_rnd_3dnow(uint8_t *dst, uint8_t *src,
                                      int stride, int h, int x, int y);

void ff_put_h264_chroma_mc4_mmx      (uint8_t *dst, uint8_t *src,
                                      int stride, int h, int x, int y);
void ff_avg_h264_chroma_mc4_mmxext   (uint8_t *dst, uint8_t *src,
                                      int stride, int h, int x, int y);
void ff_avg_h264_chroma_mc4_3dnow    (uint8_t *dst, uint8_t *src,
                                      int stride, int h, int x, int y);

void ff_put_h264_chroma_mc2_mmxext   (uint8_t *dst, uint8_t *src,
                                      int stride, int h, int x, int y);
void ff_avg_h264_chroma_mc2_mmxext   (uint8_t *dst, uint8_t *src,
                                      int stride, int h, int x, int y);

void ff_put_h264_chroma_mc8_rnd_ssse3(uint8_t *dst, uint8_t *src,
                                      int stride, int h, int x, int y);
void ff_put_h264_chroma_mc4_ssse3    (uint8_t *dst, uint8_t *src,
                                      int stride, int h, int x, int y);

void ff_avg_h264_chroma_mc8_rnd_ssse3(uint8_t *dst, uint8_t *src,
                                      int stride, int h, int x, int y);
void ff_avg_h264_chroma_mc4_ssse3    (uint8_t *dst, uint8_t *src,
                                      int stride, int h, int x, int y);

#define CHROMA_MC(OP, NUM, DEPTH, OPT)                                  \
void ff_ ## OP ## _h264_chroma_mc ## NUM ## _ ## DEPTH ## _ ## OPT      \
                                      (uint8_t *dst, uint8_t *src,      \
                                       int stride, int h, int x, int y);

CHROMA_MC(put, 2, 10, mmxext)
CHROMA_MC(avg, 2, 10, mmxext)
CHROMA_MC(put, 4, 10, mmxext)
CHROMA_MC(avg, 4, 10, mmxext)
CHROMA_MC(put, 8, 10, sse2)
CHROMA_MC(avg, 8, 10, sse2)
CHROMA_MC(put, 8, 10, avx)
CHROMA_MC(avg, 8, 10, avx)

void ff_h264chroma_init_x86(H264ChromaContext *c, int bit_depth)
{
#if HAVE_YASM
    int high_bit_depth = bit_depth > 8;
    int mm_flags       = av_get_cpu_flags();

    if (EXTERNAL_MMX(mm_flags) && !high_bit_depth) {
        c->put_h264_chroma_pixels_tab[0] = ff_put_h264_chroma_mc8_rnd_mmx;
        c->put_h264_chroma_pixels_tab[1] = ff_put_h264_chroma_mc4_mmx;
    }

    if (EXTERNAL_AMD3DNOW(mm_flags) && !high_bit_depth) {
        c->avg_h264_chroma_pixels_tab[0] = ff_avg_h264_chroma_mc8_rnd_3dnow;
        c->avg_h264_chroma_pixels_tab[1] = ff_avg_h264_chroma_mc4_3dnow;
    }

    if (EXTERNAL_MMXEXT(mm_flags) && !high_bit_depth) {
        c->avg_h264_chroma_pixels_tab[0] = ff_avg_h264_chroma_mc8_rnd_mmxext;
        c->avg_h264_chroma_pixels_tab[1] = ff_avg_h264_chroma_mc4_mmxext;
        c->avg_h264_chroma_pixels_tab[2] = ff_avg_h264_chroma_mc2_mmxext;
        c->put_h264_chroma_pixels_tab[2] = ff_put_h264_chroma_mc2_mmxext;
    }

    if (EXTERNAL_MMXEXT(mm_flags) && bit_depth > 8 && bit_depth <= 10) {
        c->put_h264_chroma_pixels_tab[2] = ff_put_h264_chroma_mc2_10_mmxext;
        c->avg_h264_chroma_pixels_tab[2] = ff_avg_h264_chroma_mc2_10_mmxext;
        c->put_h264_chroma_pixels_tab[1] = ff_put_h264_chroma_mc4_10_mmxext;
        c->avg_h264_chroma_pixels_tab[1] = ff_avg_h264_chroma_mc4_10_mmxext;
    }

    if (EXTERNAL_SSE2(mm_flags) && bit_depth > 8 && bit_depth <= 10) {
        c->put_h264_chroma_pixels_tab[0] = ff_put_h264_chroma_mc8_10_sse2;
        c->avg_h264_chroma_pixels_tab[0] = ff_avg_h264_chroma_mc8_10_sse2;
    }

    if (EXTERNAL_SSSE3(mm_flags) && !high_bit_depth) {
        c->put_h264_chroma_pixels_tab[0] = ff_put_h264_chroma_mc8_rnd_ssse3;
        c->avg_h264_chroma_pixels_tab[0] = ff_avg_h264_chroma_mc8_rnd_ssse3;
        c->put_h264_chroma_pixels_tab[1] = ff_put_h264_chroma_mc4_ssse3;
        c->avg_h264_chroma_pixels_tab[1] = ff_avg_h264_chroma_mc4_ssse3;
    }

    if (EXTERNAL_AVX(mm_flags) && bit_depth > 8 && bit_depth <= 10) {
        // AVX implies !cache64.
        // TODO: Port cache(32|64) detection from x264.
        c->put_h264_chroma_pixels_tab[0] = ff_put_h264_chroma_mc8_10_avx;
        c->avg_h264_chroma_pixels_tab[0] = ff_avg_h264_chroma_mc8_10_avx;
    }
#endif
}
