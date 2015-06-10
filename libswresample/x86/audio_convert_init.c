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

#include "libavutil/x86/cpu.h"
#include "libswresample/swresample_internal.h"
#include "libswresample/audioconvert.h"

#define PROTO(pre, in, out, cap) void ff ## pre ## in## _to_ ##out## _a_ ##cap(uint8_t **dst, const uint8_t **src, int len);
#define PROTO2(pre, out, cap) PROTO(pre, int16, out, cap) PROTO(pre, int32, out, cap) PROTO(pre, float, out, cap)
#define PROTO3(pre, cap) PROTO2(pre, int16, cap) PROTO2(pre, int32, cap) PROTO2(pre, float, cap)
#define PROTO4(pre) PROTO3(pre, mmx) PROTO3(pre, sse) PROTO3(pre, sse2) PROTO3(pre, ssse3) PROTO3(pre, sse4) PROTO3(pre, avx) PROTO3(pre, avx2)
PROTO4(_)
PROTO4(_pack_2ch_)
PROTO4(_pack_6ch_)
PROTO4(_pack_8ch_)
PROTO4(_unpack_2ch_)
PROTO4(_unpack_6ch_)

av_cold void swri_audio_convert_init_x86(struct AudioConvert *ac,
                                 enum AVSampleFormat out_fmt,
                                 enum AVSampleFormat in_fmt,
                                 int channels){
    int mm_flags = av_get_cpu_flags();

    ac->simd_f= NULL;

//FIXME add memcpy case

#define MULTI_CAPS_FUNC(flag, cap) \
    if (EXTERNAL_##flag(mm_flags)) {\
        if(   out_fmt == AV_SAMPLE_FMT_S32  && in_fmt == AV_SAMPLE_FMT_S16 || out_fmt == AV_SAMPLE_FMT_S32P && in_fmt == AV_SAMPLE_FMT_S16P)\
            ac->simd_f =  ff_int16_to_int32_a_ ## cap;\
        if(   out_fmt == AV_SAMPLE_FMT_S16  && in_fmt == AV_SAMPLE_FMT_S32 || out_fmt == AV_SAMPLE_FMT_S16P && in_fmt == AV_SAMPLE_FMT_S32P)\
            ac->simd_f =  ff_int32_to_int16_a_ ## cap;\
    }

MULTI_CAPS_FUNC(MMX, mmx)
MULTI_CAPS_FUNC(SSE2, sse2)

    if(EXTERNAL_MMX(mm_flags)) {
        if(channels == 6) {
            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_FLTP || out_fmt == AV_SAMPLE_FMT_S32 && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_6ch_float_to_float_a_mmx;
        }
    }
    if(EXTERNAL_SSE(mm_flags)) {
        if(channels == 6) {
            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_FLTP || out_fmt == AV_SAMPLE_FMT_S32 && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_6ch_float_to_float_a_sse;

            if(   out_fmt == AV_SAMPLE_FMT_FLTP  && in_fmt == AV_SAMPLE_FMT_FLT || out_fmt == AV_SAMPLE_FMT_S32P && in_fmt == AV_SAMPLE_FMT_S32)
                ac->simd_f =  ff_unpack_6ch_float_to_float_a_sse;
        }
    }
    if(EXTERNAL_SSE2(mm_flags)) {
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
        if(channels == 6) {
            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_6ch_int32_to_float_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_S32  && in_fmt == AV_SAMPLE_FMT_FLTP)
                ac->simd_f =  ff_pack_6ch_float_to_int32_a_sse2;

            if(   out_fmt == AV_SAMPLE_FMT_FLTP  && in_fmt == AV_SAMPLE_FMT_S32)
                ac->simd_f =  ff_unpack_6ch_int32_to_float_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_S32P  && in_fmt == AV_SAMPLE_FMT_FLT)
                ac->simd_f =  ff_unpack_6ch_float_to_int32_a_sse2;
        }
        if(channels == 8) {
            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_FLTP || out_fmt == AV_SAMPLE_FMT_S32 && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_8ch_float_to_float_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_8ch_int32_to_float_a_sse2;
            if(   out_fmt == AV_SAMPLE_FMT_S32  && in_fmt == AV_SAMPLE_FMT_FLTP)
                ac->simd_f =  ff_pack_8ch_float_to_int32_a_sse2;
        }
    }
    if(EXTERNAL_SSSE3(mm_flags)) {
        if(channels == 2) {
            if(   out_fmt == AV_SAMPLE_FMT_S16P  && in_fmt == AV_SAMPLE_FMT_S16)
                ac->simd_f =  ff_unpack_2ch_int16_to_int16_a_ssse3;
            if(   out_fmt == AV_SAMPLE_FMT_S32P  && in_fmt == AV_SAMPLE_FMT_S16)
                ac->simd_f =  ff_unpack_2ch_int16_to_int32_a_ssse3;
            if(   out_fmt == AV_SAMPLE_FMT_FLTP  && in_fmt == AV_SAMPLE_FMT_S16)
                ac->simd_f =  ff_unpack_2ch_int16_to_float_a_ssse3;
        }
    }
    if(EXTERNAL_AVX_FAST(mm_flags)) {
        if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_S32 || out_fmt == AV_SAMPLE_FMT_FLTP && in_fmt == AV_SAMPLE_FMT_S32P)
            ac->simd_f =  ff_int32_to_float_a_avx;
    }
    if(EXTERNAL_AVX(mm_flags)) {
        if(channels == 6) {
            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_FLTP || out_fmt == AV_SAMPLE_FMT_S32 && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_6ch_float_to_float_a_avx;
            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_6ch_int32_to_float_a_avx;
            if(   out_fmt == AV_SAMPLE_FMT_S32  && in_fmt == AV_SAMPLE_FMT_FLTP)
                ac->simd_f =  ff_pack_6ch_float_to_int32_a_avx;

            if(   out_fmt == AV_SAMPLE_FMT_FLTP  && in_fmt == AV_SAMPLE_FMT_FLT || out_fmt == AV_SAMPLE_FMT_S32P && in_fmt == AV_SAMPLE_FMT_S32)
                ac->simd_f =  ff_unpack_6ch_float_to_float_a_avx;
            if(   out_fmt == AV_SAMPLE_FMT_FLTP  && in_fmt == AV_SAMPLE_FMT_S32)
                ac->simd_f =  ff_unpack_6ch_int32_to_float_a_avx;
            if(   out_fmt == AV_SAMPLE_FMT_S32P  && in_fmt == AV_SAMPLE_FMT_FLT)
                ac->simd_f =  ff_unpack_6ch_float_to_int32_a_avx;
        }
        if(channels == 8) {
            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_FLTP || out_fmt == AV_SAMPLE_FMT_S32 && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_8ch_float_to_float_a_avx;
            if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_S32P)
                ac->simd_f =  ff_pack_8ch_int32_to_float_a_avx;
            if(   out_fmt == AV_SAMPLE_FMT_S32  && in_fmt == AV_SAMPLE_FMT_FLTP)
                ac->simd_f =  ff_pack_8ch_float_to_int32_a_avx;
        }
    }
    if(EXTERNAL_AVX2(mm_flags)) {
        if(   out_fmt == AV_SAMPLE_FMT_S32  && in_fmt == AV_SAMPLE_FMT_FLT || out_fmt == AV_SAMPLE_FMT_S32P && in_fmt == AV_SAMPLE_FMT_FLTP)
            ac->simd_f =  ff_float_to_int32_a_avx2;
    }
}
