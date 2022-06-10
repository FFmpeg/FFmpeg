/*
 * Copyright (C) 2012 Michael Niedermayer (michaelni@gmx.at)
 *
 * This file is part of libswresample
 *
 * libswresample is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libswresample is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libswresample; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/attributes.h"
#include "libavutil/x86/cpu.h"
#include "libswresample/swresample_internal.h"

#define D(type, simd) \
mix_1_1_func_type ff_mix_1_1_a_## type ## _ ## simd;\
mix_2_1_func_type ff_mix_2_1_a_## type ## _ ## simd;

D(float, sse)
D(float, avx)
D(int16, sse2)

av_cold int swri_rematrix_init_x86(struct SwrContext *s){
#if HAVE_X86ASM
    int mm_flags = av_get_cpu_flags();
    int nb_in  = s->used_ch_count;
    int nb_out = s->out.ch_count;
    int num    = nb_in * nb_out;
    int i,j;

    s->mix_1_1_simd = NULL;
    s->mix_2_1_simd = NULL;

    if (s->midbuf.fmt == AV_SAMPLE_FMT_S16P){
        if(EXTERNAL_SSE2(mm_flags)) {
            s->mix_1_1_simd = ff_mix_1_1_a_int16_sse2;
            s->mix_2_1_simd = ff_mix_2_1_a_int16_sse2;
        }
        s->native_simd_matrix = av_calloc(num,  2 * sizeof(int16_t));
        s->native_simd_one    = av_mallocz(2 * sizeof(int16_t));
        if (!s->native_simd_matrix || !s->native_simd_one)
            return AVERROR(ENOMEM);

        for(i=0; i<nb_out; i++){
            int sh = 0;
            for(j=0; j<nb_in; j++)
                sh = FFMAX(sh, FFABS(((int*)s->native_matrix)[i * nb_in + j]));
            sh = FFMAX(av_log2(sh) - 14, 0);
            for(j=0; j<nb_in; j++) {
                ((int16_t*)s->native_simd_matrix)[2*(i * nb_in + j)+1] = 15 - sh;
                ((int16_t*)s->native_simd_matrix)[2*(i * nb_in + j)] =
                    ((((int*)s->native_matrix)[i * nb_in + j]) + (1<<sh>>1)) >> sh;
            }
        }
        ((int16_t*)s->native_simd_one)[1] = 14;
        ((int16_t*)s->native_simd_one)[0] = 16384;
    } else if(s->midbuf.fmt == AV_SAMPLE_FMT_FLTP){
        if(EXTERNAL_SSE(mm_flags)) {
            s->mix_1_1_simd = ff_mix_1_1_a_float_sse;
            s->mix_2_1_simd = ff_mix_2_1_a_float_sse;
        }
        if(EXTERNAL_AVX_FAST(mm_flags)) {
            s->mix_1_1_simd = ff_mix_1_1_a_float_avx;
            s->mix_2_1_simd = ff_mix_2_1_a_float_avx;
        }
        s->native_simd_matrix = av_calloc(num, sizeof(float));
        s->native_simd_one = av_mallocz(sizeof(float));
        if (!s->native_simd_matrix || !s->native_simd_one)
            return AVERROR(ENOMEM);
        memcpy(s->native_simd_matrix, s->native_matrix, num * sizeof(float));
        memcpy(s->native_simd_one, s->native_one, sizeof(float));
    }
#endif

    return 0;
}
