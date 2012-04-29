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

#define MULTI_CAPS_FUNC_DECL(cap) \
    void ff_int16_to_int32_a_ ## cap(uint8_t **dst, const uint8_t **src, int len);\
    void ff_int32_to_int16_a_ ## cap(uint8_t **dst, const uint8_t **src, int len);\

MULTI_CAPS_FUNC_DECL(mmx)
MULTI_CAPS_FUNC_DECL(sse)

void ff_int32_to_float_a_sse2(uint8_t **dst, const uint8_t **src, int len);
void ff_int16_to_float_a_sse2(uint8_t **dst, const uint8_t **src, int len);
void ff_float_to_int32_a_sse2(uint8_t **dst, const uint8_t **src, int len);
void ff_float_to_int16_a_sse2(uint8_t **dst, const uint8_t **src, int len);

void swri_audio_convert_init_x86(struct AudioConvert *ac,
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
MULTI_CAPS_FUNC(AV_CPU_FLAG_SSE, sse)

    if(mm_flags & AV_CPU_FLAG_SSE2) {
        if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_S32 || out_fmt == AV_SAMPLE_FMT_FLTP && in_fmt == AV_SAMPLE_FMT_S32P)
            ac->simd_f =  ff_int32_to_float_a_sse2;
        if(   out_fmt == AV_SAMPLE_FMT_FLT  && in_fmt == AV_SAMPLE_FMT_S16 || out_fmt == AV_SAMPLE_FMT_FLTP && in_fmt == AV_SAMPLE_FMT_S16P)
            ac->simd_f =  ff_int16_to_float_a_sse2;
        if(   out_fmt == AV_SAMPLE_FMT_S32  && in_fmt == AV_SAMPLE_FMT_FLT || out_fmt == AV_SAMPLE_FMT_S32P && in_fmt == AV_SAMPLE_FMT_FLTP)
            ac->simd_f =  ff_float_to_int32_a_sse2;
        if(   out_fmt == AV_SAMPLE_FMT_S16  && in_fmt == AV_SAMPLE_FMT_FLT || out_fmt == AV_SAMPLE_FMT_S16P && in_fmt == AV_SAMPLE_FMT_FLTP)
            ac->simd_f =  ff_float_to_int16_a_sse2;
    }
}
