/*
 * Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
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

#include "config.h"
#include "libavutil/cpu.h"
#include "libavresample/audio_convert.h"

/* flat conversions */

extern void ff_conv_s16_to_s32_sse2(int16_t *dst, const int32_t *src, int len);

extern void ff_conv_s16_to_flt_sse2(float *dst, const int16_t *src, int len);
extern void ff_conv_s16_to_flt_sse4(float *dst, const int16_t *src, int len);

extern void ff_conv_s32_to_s16_mmx (int16_t *dst, const int32_t *src, int len);
extern void ff_conv_s32_to_s16_sse2(int16_t *dst, const int32_t *src, int len);

extern void ff_conv_s32_to_flt_sse2(float *dst, const int32_t *src, int len);
extern void ff_conv_s32_to_flt_avx (float *dst, const int32_t *src, int len);

extern void ff_conv_flt_to_s16_sse2(int16_t *dst, const float *src, int len);

extern void ff_conv_flt_to_s32_sse2(int32_t *dst, const float *src, int len);
extern void ff_conv_flt_to_s32_avx (int32_t *dst, const float *src, int len);

/* interleave conversions */

extern void ff_conv_s16p_to_s16_2ch_sse2(int16_t *dst, int16_t *const *src,
                                         int len, int channels);
extern void ff_conv_s16p_to_s16_2ch_avx (int16_t *dst, int16_t *const *src,
                                         int len, int channels);

extern void ff_conv_s16p_to_s16_6ch_sse2(int16_t *dst, int16_t *const *src,
                                         int len, int channels);
extern void ff_conv_s16p_to_s16_6ch_sse2slow(int16_t *dst, int16_t *const *src,
                                             int len, int channels);
extern void ff_conv_s16p_to_s16_6ch_avx (int16_t *dst, int16_t *const *src,
                                         int len, int channels);

extern void ff_conv_fltp_to_flt_6ch_mmx (float *dst, float *const *src, int len,
                                         int channels);
extern void ff_conv_fltp_to_flt_6ch_sse4(float *dst, float *const *src, int len,
                                         int channels);
extern void ff_conv_fltp_to_flt_6ch_avx (float *dst, float *const *src, int len,
                                         int channels);

av_cold void ff_audio_convert_init_x86(AudioConvert *ac)
{
#if HAVE_YASM
    int mm_flags = av_get_cpu_flags();

    if (mm_flags & AV_CPU_FLAG_MMX && HAVE_MMX) {
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S32,
                                  0, 1, 8, "MMX", ff_conv_s32_to_s16_mmx);
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
                                  6, 1, 4, "MMX", ff_conv_fltp_to_flt_6ch_mmx);
    }
    if (mm_flags & AV_CPU_FLAG_SSE2 && HAVE_SSE) {
        if (!(mm_flags & AV_CPU_FLAG_SSE2SLOW)) {
            ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S32,
                                      0, 16, 16, "SSE2", ff_conv_s32_to_s16_sse2);
            ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P,
                                      6, 16, 8, "SSE2", ff_conv_s16p_to_s16_6ch_sse2);
        } else {
            ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P,
                                      6, 1, 4, "SSE2SLOW", ff_conv_s16p_to_s16_6ch_sse2slow);
        }
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S16,
                                  0, 16, 8, "SSE2", ff_conv_s16_to_s32_sse2);
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S16,
                                  0, 16, 8, "SSE2", ff_conv_s16_to_flt_sse2);
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S32,
                                  0, 16, 8, "SSE2", ff_conv_s32_to_flt_sse2);
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_FLT,
                                  0, 16, 16, "SSE2", ff_conv_flt_to_s16_sse2);
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_FLT,
                                  0, 16, 16, "SSE2", ff_conv_flt_to_s32_sse2);
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P,
                                  2, 16, 16, "SSE2", ff_conv_s16p_to_s16_2ch_sse2);
    }
    if (mm_flags & AV_CPU_FLAG_SSE4 && HAVE_SSE) {
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S16,
                                  0, 16, 8, "SSE4", ff_conv_s16_to_flt_sse4);
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
                                  6, 16, 4, "SSE4", ff_conv_fltp_to_flt_6ch_sse4);
    }
    if (mm_flags & AV_CPU_FLAG_AVX && HAVE_AVX) {
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S32,
                                  0, 32, 16, "AVX", ff_conv_s32_to_flt_avx);
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_FLT,
                                  0, 32, 32, "AVX", ff_conv_flt_to_s32_avx);
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P,
                                  2, 16, 16, "AVX", ff_conv_s16p_to_s16_2ch_avx);
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P,
                                  6, 16, 8, "AVX", ff_conv_s16p_to_s16_6ch_avx);
        ff_audio_convert_set_func(ac, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
                                  6, 16, 4, "AVX", ff_conv_fltp_to_flt_6ch_avx);
    }
#endif
}
