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

#include "libswresample/swresample_internal.h"
#include "libswresample/audioconvert.h"

#define PROTO(pre, in, out, cap) void ff ## pre ## _ ##in## _to_ ##out## _a_ ##cap(uint8_t **dst, const uint8_t **src, int len);
#define PROTO2(pre, out, cap) PROTO(pre, int16, out, cap) PROTO(pre, int32, out, cap) PROTO(pre, float, out, cap)
#define PROTO3(pre, cap) PROTO2(pre, int16, cap) PROTO2(pre, int32, cap) PROTO2(pre, float, cap)
#define PROTO4(pre) PROTO3(pre, mmx) PROTO3(pre, sse) PROTO3(pre, sse2) PROTO3(pre, ssse3) PROTO3(pre, sse4) PROTO3(pre, avx)
PROTO4()
PROTO4(_pack_2ch)
PROTO4(_pack_6ch)
PROTO4(_unpack_2ch)

av_cold void swri_audio_convert_init_x86(struct AudioConvert *ac,
                                 enum AVSampleFormat out_fmt,
                                 enum AVSampleFormat in_fmt,
                                 int channels){
    int mm_flags = av_get_cpu_flags();

    ac->simd_f= NULL;

//FIXME add memcpy case

#define MULTI_CAPS_FUNC(flag, cap) \
    if (mm_flags & flag) {\
        if(   out_fmt == AV_SAMPLE_FMT_S32  && in_fmt == AV_SAMPLE_FMT_S16 || out_fmt == AV_SAMPLE_FMT_S32P && in_fmt == AV_SAMPLE_FMT_S16P)\
            ac->simd_f =  ff_int16_to_int32_a_ ## cap;\
        if(   out_fmt == AV_SAMPLE_FMT_S16  && in_fmt == AV_SAMPLE_FMT_S32 || out_fmt == AV_SAMPLE_FMT_S16P && in_fmt == AV_SAMPLE_FMT_S32P)\
            ac->simd_f =  ff_int32_to_int16_a_ ## cap;\
    }

MULTI_CAPS_FUNC(AV_CPU_FLAG_MMX, mmx)
MULTI_CAPS_FUNC(AV_CPU_FLAG_SSE2, sse2)

    if(mm_flags & AV_CPU_FLAG_MMX) {
        if(channels == 6) {
            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_FLTP || out_fmt == AV_SAMPLE_FMT_S32 && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_6ch_float_to_float_a_mmx;
        }
    }

    if(mm_flags & AV_CPU_FLAG_SSE2) {
        if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_S32 || out_fmt == AV_SAMPLE_FMT_FLTP && in_fmt == AV_SAMPLE_FMT_S32P)
            ac->simd_f =  ff_int32_to_float_a_sse2;
        if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_S16 || out_fmt == AV_SAMPLE_FMT_FLTP && in_fmt == AV_SAMPLE_FMT_S16P)
            ac->simd_f =  ff_int16_to_float_a_sse2;
        if(   out_fmt == AV_SAMPLE_FMT_S32  && in_fmt == AV_SAMPLE_FMT_FLT || out_fmt == AV_SAMPLE_FMT_S32P && in_fmt == AV_SAMPLE_FMT_FLTP)
            ac->simd_f =  ff_float_to_int32_a_sse2;
        if(   out_fmt == AV_SAMPLE_FMT_S16  && in_fmt == AV_SAMPLE_FMT_FLT || out_fmt == AV_SAMPLE_FMT_S16P && in_fmt == AV_SAMPLE_FMT_FLTP)
            ac->simd_f =  ff_float_to_int16_a_sse2;

        if(channels == 2) {
            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_FLTP || out_fmt == AV_SAMPLE_FMT_S32 && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_2ch_int32_to_int32_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_S16  && in_fmt == AV_SAMPLE_FMT_S16P)
                ac->simd_f =  ff_pack_2ch_int16_to_int16_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_S32  && in_fmt == AV_SAMPLE_FMT_S16P)
                ac->simd_f =  ff_pack_2ch_int16_to_int32_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_S16  && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_2ch_int32_to_int16_a_sse2;

            if(   out_fmt == AV_SAMPLE_FMT_FLTP  && in_fmt == AV_SAMPLE_FMT_FLT || out_fmt == AV_SAMPLE_FMT_S32P && in_fmt == AV_SAMPLE_FMT_S32)
                ac->simd_f =  ff_unpack_2ch_int32_to_int32_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_S16P  && in_fmt == AV_SAMPLE_FMT_S16)
                ac->simd_f =  ff_unpack_2ch_int16_to_int16_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_S32P  && in_fmt == AV_SAMPLE_FMT_S16)
                ac->simd_f =  ff_unpack_2ch_int16_to_int32_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_S16P  && in_fmt == AV_SAMPLE_FMT_S32)
                ac->simd_f =  ff_unpack_2ch_int32_to_int16_a_sse2;

            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_2ch_int32_to_float_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_S32  && in_fmt == AV_SAMPLE_FMT_FLTP)
                ac->simd_f =  ff_pack_2ch_float_to_int32_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_S16P)
                ac->simd_f =  ff_pack_2ch_int16_to_float_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_S16  && in_fmt == AV_SAMPLE_FMT_FLTP)
                ac->simd_f =  ff_pack_2ch_float_to_int16_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_FLTP  && in_fmt == AV_SAMPLE_FMT_S32)
                ac->simd_f =  ff_unpack_2ch_int32_to_float_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_S32P  && in_fmt == AV_SAMPLE_FMT_FLT)
                ac->simd_f =  ff_unpack_2ch_float_to_int32_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_FLTP  && in_fmt == AV_SAMPLE_FMT_S16)
                ac->simd_f =  ff_unpack_2ch_int16_to_float_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_S16P  && in_fmt == AV_SAMPLE_FMT_FLT)
                ac->simd_f =  ff_unpack_2ch_float_to_int16_a_sse2;
        }
    }
    if(mm_flags & AV_CPU_FLAG_SSSE3) {
        if(channels == 2) {
            if(   out_fmt == AV_SAMPLE_FMT_S16P  && in_fmt == AV_SAMPLE_FMT_S16)
                ac->simd_f =  ff_unpack_2ch_int16_to_int16_a_ssse3;
            if(   out_fmt == AV_SAMPLE_FMT_S32P  && in_fmt == AV_SAMPLE_FMT_S16)
                ac->simd_f =  ff_unpack_2ch_int16_to_int32_a_ssse3;
            if(   out_fmt == AV_SAMPLE_FMT_FLTP  && in_fmt == AV_SAMPLE_FMT_S16)
                ac->simd_f =  ff_unpack_2ch_int16_to_float_a_ssse3;
        }
    }
    if(mm_flags & AV_CPU_FLAG_SSE4) {
        if(channels == 6) {
            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_FLTP || out_fmt == AV_SAMPLE_FMT_S32 && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_6ch_float_to_float_a_sse4;
            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_6ch_int32_to_float_a_sse4;
            if(   out_fmt == AV_SAMPLE_FMT_S32  && in_fmt == AV_SAMPLE_FMT_FLTP)
                ac->simd_f =  ff_pack_6ch_float_to_int32_a_sse4;
        }
    }
    if(HAVE_AVX_EXTERNAL && mm_flags & AV_CPU_FLAG_AVX) {
        if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_S32 || out_fmt == AV_SAMPLE_FMT_FLTP && in_fmt == AV_SAMPLE_FMT_S32P)
            ac->simd_f =  ff_int32_to_float_a_avx;
        if(channels == 6) {
            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_FLTP || out_fmt == AV_SAMPLE_FMT_S32 && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_6ch_float_to_float_a_avx;
            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_6ch_int32_to_float_a_avx;
            if(   out_fmt == AV_SAMPLE_FMT_S32  && in_fmt == AV_SAMPLE_FMT_FLTP)
                ac->simd_f =  ff_pack_6ch_float_to_int32_a_avx;
        }
    }
}

#define D(type, simd) \
mix_1_1_func_type ff_mix_1_1_a_## type ## _ ## simd;\
mix_2_1_func_type ff_mix_2_1_a_## type ## _ ## simd;

D(float, sse)
D(float, avx)
D(int16, mmx)
D(int16, sse2)


av_cold void swri_rematrix_init_x86(struct SwrContext *s){
    int mm_flags = av_get_cpu_flags();
    int nb_in  = av_get_channel_layout_nb_channels(s->in_ch_layout);
    int nb_out = av_get_channel_layout_nb_channels(s->out_ch_layout);
    int num    = nb_in * nb_out;
    int i,j;

    s->mix_1_1_simd = NULL;
    s->mix_2_1_simd = NULL;

    if (s->midbuf.fmt == AV_SAMPLE_FMT_S16P){
        if(mm_flags & AV_CPU_FLAG_MMX) {
            s->mix_1_1_simd = ff_mix_1_1_a_int16_mmx;
            s->mix_2_1_simd = ff_mix_2_1_a_int16_mmx;
        }
        if(mm_flags & AV_CPU_FLAG_SSE2) {
            s->mix_1_1_simd = ff_mix_1_1_a_int16_sse2;
            s->mix_2_1_simd = ff_mix_2_1_a_int16_sse2;
        }
        s->native_simd_matrix = av_mallocz(2 * num * sizeof(int16_t));
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
    } else if(s->midbuf.fmt == AV_SAMPLE_FMT_FLTP){
        if(mm_flags & AV_CPU_FLAG_SSE) {
            s->mix_1_1_simd = ff_mix_1_1_a_float_sse;
            s->mix_2_1_simd = ff_mix_2_1_a_float_sse;
        }
        if(HAVE_AVX_EXTERNAL && mm_flags & AV_CPU_FLAG_AVX) {
            s->mix_1_1_simd = ff_mix_1_1_a_float_avx;
            s->mix_2_1_simd = ff_mix_2_1_a_float_avx;
        }
        s->native_simd_matrix = av_mallocz(num * sizeof(float));
        memcpy(s->native_simd_matrix, s->native_matrix, num * sizeof(float));
    }
}
