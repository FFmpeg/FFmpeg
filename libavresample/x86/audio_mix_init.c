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
#include "libavutil/x86/cpu.h"
#include "libavresample/audio_mix.h"

extern void ff_mix_2_to_1_fltp_flt_sse(float **src, float **matrix, int len,
                                       int out_ch, int in_ch);
extern void ff_mix_2_to_1_fltp_flt_avx(float **src, float **matrix, int len,
                                       int out_ch, int in_ch);

extern void ff_mix_2_to_1_s16p_flt_sse2(int16_t **src, float **matrix, int len,
                                        int out_ch, int in_ch);
extern void ff_mix_2_to_1_s16p_flt_sse4(int16_t **src, float **matrix, int len,
                                        int out_ch, int in_ch);

extern void ff_mix_2_to_1_s16p_q8_sse2(int16_t **src, int16_t **matrix,
                                       int len, int out_ch, int in_ch);

extern void ff_mix_1_to_2_fltp_flt_sse(float **src, float **matrix, int len,
                                       int out_ch, int in_ch);
extern void ff_mix_1_to_2_fltp_flt_avx(float **src, float **matrix, int len,
                                       int out_ch, int in_ch);

extern void ff_mix_1_to_2_s16p_flt_sse2(int16_t **src, float **matrix, int len,
                                        int out_ch, int in_ch);
extern void ff_mix_1_to_2_s16p_flt_sse4(int16_t **src, float **matrix, int len,
                                        int out_ch, int in_ch);
extern void ff_mix_1_to_2_s16p_flt_avx (int16_t **src, float **matrix, int len,
                                        int out_ch, int in_ch);

#define DEFINE_MIX_3_8_TO_1_2(chan)                                         \
extern void ff_mix_ ## chan ## _to_1_fltp_flt_sse(float **src,              \
                                                  float **matrix, int len,  \
                                                  int out_ch, int in_ch);   \
extern void ff_mix_ ## chan ## _to_2_fltp_flt_sse(float **src,              \
                                                  float **matrix, int len,  \
                                                  int out_ch, int in_ch);   \
                                                                            \
extern void ff_mix_ ## chan ## _to_1_s16p_flt_sse2(int16_t **src,           \
                                                   float **matrix, int len, \
                                                   int out_ch, int in_ch);  \
extern void ff_mix_ ## chan ## _to_2_s16p_flt_sse2(int16_t **src,           \
                                                   float **matrix, int len, \
                                                   int out_ch, int in_ch);  \
                                                                            \
extern void ff_mix_ ## chan ## _to_1_s16p_flt_sse4(int16_t **src,           \
                                                   float **matrix, int len, \
                                                   int out_ch, int in_ch);  \
extern void ff_mix_ ## chan ## _to_2_s16p_flt_sse4(int16_t **src,           \
                                                   float **matrix, int len, \
                                                   int out_ch, int in_ch);  \
                                                                            \
extern void ff_mix_ ## chan ## _to_1_fltp_flt_avx(float **src,              \
                                                  float **matrix, int len,  \
                                                  int out_ch, int in_ch);   \
extern void ff_mix_ ## chan ## _to_2_fltp_flt_avx(float **src,              \
                                                  float **matrix, int len,  \
                                                  int out_ch, int in_ch);   \
                                                                            \
extern void ff_mix_ ## chan ## _to_1_s16p_flt_avx(int16_t **src,            \
                                                  float **matrix, int len,  \
                                                  int out_ch, int in_ch);   \
extern void ff_mix_ ## chan ## _to_2_s16p_flt_avx(int16_t **src,            \
                                                  float **matrix, int len,  \
                                                  int out_ch, int in_ch);   \
                                                                            \
extern void ff_mix_ ## chan ## _to_1_fltp_flt_fma4(float **src,             \
                                                   float **matrix, int len, \
                                                   int out_ch, int in_ch);  \
extern void ff_mix_ ## chan ## _to_2_fltp_flt_fma4(float **src,             \
                                                   float **matrix, int len, \
                                                   int out_ch, int in_ch);  \
                                                                            \
extern void ff_mix_ ## chan ## _to_1_s16p_flt_fma4(int16_t **src,           \
                                                   float **matrix, int len, \
                                                   int out_ch, int in_ch);  \
extern void ff_mix_ ## chan ## _to_2_s16p_flt_fma4(int16_t **src,           \
                                                   float **matrix, int len, \
                                                   int out_ch, int in_ch);

DEFINE_MIX_3_8_TO_1_2(3)
DEFINE_MIX_3_8_TO_1_2(4)
DEFINE_MIX_3_8_TO_1_2(5)
DEFINE_MIX_3_8_TO_1_2(6)
DEFINE_MIX_3_8_TO_1_2(7)
DEFINE_MIX_3_8_TO_1_2(8)

#define SET_MIX_3_8_TO_1_2(chan)                                            \
    if (EXTERNAL_SSE(mm_flags)) {                                           \
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,\
                              chan, 1, 16, 4, "SSE",                        \
                              ff_mix_ ## chan ## _to_1_fltp_flt_sse);       \
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,\
                              chan, 2, 16, 4, "SSE",                        \
                              ff_mix_## chan ##_to_2_fltp_flt_sse);         \
    }                                                                       \
    if (EXTERNAL_SSE2(mm_flags)) {                                          \
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_FLT,\
                              chan, 1, 16, 8, "SSE2",                       \
                              ff_mix_ ## chan ## _to_1_s16p_flt_sse2);      \
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_FLT,\
                              chan, 2, 16, 8, "SSE2",                       \
                              ff_mix_ ## chan ## _to_2_s16p_flt_sse2);      \
    }                                                                       \
    if (EXTERNAL_SSE4(mm_flags)) {                                          \
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_FLT,\
                              chan, 1, 16, 8, "SSE4",                       \
                              ff_mix_ ## chan ## _to_1_s16p_flt_sse4);      \
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_FLT,\
                              chan, 2, 16, 8, "SSE4",                       \
                              ff_mix_ ## chan ## _to_2_s16p_flt_sse4);      \
    }                                                                       \
    if (EXTERNAL_AVX(mm_flags)) {                                           \
        int ptr_align = 32;                                                 \
        int smp_align = 8;                                                  \
        if (ARCH_X86_32 || chan >= 6) {                                     \
            ptr_align = 16;                                                 \
            smp_align = 4;                                                  \
        }                                                                   \
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,\
                              chan, 1, ptr_align, smp_align, "AVX",         \
                              ff_mix_ ## chan ## _to_1_fltp_flt_avx);       \
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,\
                              chan, 2, ptr_align, smp_align, "AVX",         \
                              ff_mix_ ## chan ## _to_2_fltp_flt_avx);       \
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_FLT,\
                              chan, 1, 16, 8, "AVX",                        \
                              ff_mix_ ## chan ## _to_1_s16p_flt_avx);       \
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_FLT,\
                              chan, 2, 16, 8, "AVX",                        \
                              ff_mix_ ## chan ## _to_2_s16p_flt_avx);       \
    }                                                                       \
    if (EXTERNAL_FMA4(mm_flags)) {                                          \
        int ptr_align = 32;                                                 \
        int smp_align = 8;                                                  \
        if (ARCH_X86_32 || chan >= 6) {                                     \
            ptr_align = 16;                                                 \
            smp_align = 4;                                                  \
        }                                                                   \
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,\
                              chan, 1, ptr_align, smp_align, "FMA4",        \
                              ff_mix_ ## chan ## _to_1_fltp_flt_fma4);      \
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,\
                              chan, 2, ptr_align, smp_align, "FMA4",        \
                              ff_mix_ ## chan ## _to_2_fltp_flt_fma4);      \
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_FLT,\
                              chan, 1, 16, 8, "FMA4",                       \
                              ff_mix_ ## chan ## _to_1_s16p_flt_fma4);      \
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_FLT,\
                              chan, 2, 16, 8, "FMA4",                       \
                              ff_mix_ ## chan ## _to_2_s16p_flt_fma4);      \
    }

av_cold void ff_audio_mix_init_x86(AudioMix *am)
{
#if HAVE_YASM
    int mm_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE(mm_flags)) {
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,
                              2, 1, 16, 8, "SSE", ff_mix_2_to_1_fltp_flt_sse);
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,
                              1, 2, 16, 4, "SSE", ff_mix_1_to_2_fltp_flt_sse);
    }
    if (EXTERNAL_SSE2(mm_flags)) {
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_FLT,
                              2, 1, 16, 8, "SSE2", ff_mix_2_to_1_s16p_flt_sse2);
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_Q8,
                              2, 1, 16, 8, "SSE2", ff_mix_2_to_1_s16p_q8_sse2);
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_FLT,
                              1, 2, 16, 8, "SSE2", ff_mix_1_to_2_s16p_flt_sse2);
    }
    if (EXTERNAL_SSE4(mm_flags)) {
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_FLT,
                              2, 1, 16, 8, "SSE4", ff_mix_2_to_1_s16p_flt_sse4);
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_FLT,
                              1, 2, 16, 8, "SSE4", ff_mix_1_to_2_s16p_flt_sse4);
    }
    if (EXTERNAL_AVX(mm_flags)) {
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,
                              2, 1, 32, 16, "AVX", ff_mix_2_to_1_fltp_flt_avx);
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,
                              1, 2, 32, 8, "AVX", ff_mix_1_to_2_fltp_flt_avx);
        ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_FLT,
                              1, 2, 16, 8, "AVX", ff_mix_1_to_2_s16p_flt_avx);
    }

    SET_MIX_3_8_TO_1_2(3)
    SET_MIX_3_8_TO_1_2(4)
    SET_MIX_3_8_TO_1_2(5)
    SET_MIX_3_8_TO_1_2(6)
    SET_MIX_3_8_TO_1_2(7)
    SET_MIX_3_8_TO_1_2(8)
#endif /* HAVE_YASM */
}
