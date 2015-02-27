/*
 * audio conversion
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * audio conversion
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "libavutil/avstring.h"
#include "libavutil/avassert.h"
#include "libavutil/libm.h"
#include "libavutil/samplefmt.h"
#include "audioconvert.h"


#define CONV_FUNC_NAME(dst_fmt, src_fmt) conv_ ## src_fmt ## _to_ ## dst_fmt

//FIXME rounding ?
#define CONV_FUNC(ofmt, otype, ifmt, expr)\
static void CONV_FUNC_NAME(ofmt, ifmt)(uint8_t *po, const uint8_t *pi, int is, int os, uint8_t *end)\
{\
    uint8_t *end2 = end - 3*os;\
    while(po < end2){\
        *(otype*)po = expr; pi += is; po += os;\
        *(otype*)po = expr; pi += is; po += os;\
        *(otype*)po = expr; pi += is; po += os;\
        *(otype*)po = expr; pi += is; po += os;\
    }\
    while(po < end){\
        *(otype*)po = expr; pi += is; po += os;\
    }\
}

//FIXME put things below under ifdefs so we do not waste space for cases no codec will need
CONV_FUNC(AV_SAMPLE_FMT_U8 , uint8_t, AV_SAMPLE_FMT_U8 ,  *(const uint8_t*)pi)
CONV_FUNC(AV_SAMPLE_FMT_S16, int16_t, AV_SAMPLE_FMT_U8 , (*(const uint8_t*)pi - 0x80U)<<8)
CONV_FUNC(AV_SAMPLE_FMT_S32, int32_t, AV_SAMPLE_FMT_U8 , (*(const uint8_t*)pi - 0x80U)<<24)
CONV_FUNC(AV_SAMPLE_FMT_FLT, float  , AV_SAMPLE_FMT_U8 , (*(const uint8_t*)pi - 0x80)*(1.0f/ (1<<7)))
CONV_FUNC(AV_SAMPLE_FMT_DBL, double , AV_SAMPLE_FMT_U8 , (*(const uint8_t*)pi - 0x80)*(1.0 / (1<<7)))
CONV_FUNC(AV_SAMPLE_FMT_U8 , uint8_t, AV_SAMPLE_FMT_S16, (*(const int16_t*)pi>>8) + 0x80)
CONV_FUNC(AV_SAMPLE_FMT_S16, int16_t, AV_SAMPLE_FMT_S16,  *(const int16_t*)pi)
CONV_FUNC(AV_SAMPLE_FMT_S32, int32_t, AV_SAMPLE_FMT_S16,  *(const int16_t*)pi<<16)
CONV_FUNC(AV_SAMPLE_FMT_FLT, float  , AV_SAMPLE_FMT_S16,  *(const int16_t*)pi*(1.0f/ (1<<15)))
CONV_FUNC(AV_SAMPLE_FMT_DBL, double , AV_SAMPLE_FMT_S16,  *(const int16_t*)pi*(1.0 / (1<<15)))
CONV_FUNC(AV_SAMPLE_FMT_U8 , uint8_t, AV_SAMPLE_FMT_S32, (*(const int32_t*)pi>>24) + 0x80)
CONV_FUNC(AV_SAMPLE_FMT_S16, int16_t, AV_SAMPLE_FMT_S32,  *(const int32_t*)pi>>16)
CONV_FUNC(AV_SAMPLE_FMT_S32, int32_t, AV_SAMPLE_FMT_S32,  *(const int32_t*)pi)
CONV_FUNC(AV_SAMPLE_FMT_FLT, float  , AV_SAMPLE_FMT_S32,  *(const int32_t*)pi*(1.0f/ (1U<<31)))
CONV_FUNC(AV_SAMPLE_FMT_DBL, double , AV_SAMPLE_FMT_S32,  *(const int32_t*)pi*(1.0 / (1U<<31)))
CONV_FUNC(AV_SAMPLE_FMT_U8 , uint8_t, AV_SAMPLE_FMT_FLT, av_clip_uint8(  lrintf(*(const float*)pi * (1<<7)) + 0x80))
CONV_FUNC(AV_SAMPLE_FMT_S16, int16_t, AV_SAMPLE_FMT_FLT, av_clip_int16(  lrintf(*(const float*)pi * (1<<15))))
CONV_FUNC(AV_SAMPLE_FMT_S32, int32_t, AV_SAMPLE_FMT_FLT, av_clipl_int32(llrintf(*(const float*)pi * (1U<<31))))
CONV_FUNC(AV_SAMPLE_FMT_FLT, float  , AV_SAMPLE_FMT_FLT, *(const float*)pi)
CONV_FUNC(AV_SAMPLE_FMT_DBL, double , AV_SAMPLE_FMT_FLT, *(const float*)pi)
CONV_FUNC(AV_SAMPLE_FMT_U8 , uint8_t, AV_SAMPLE_FMT_DBL, av_clip_uint8(  lrint(*(const double*)pi * (1<<7)) + 0x80))
CONV_FUNC(AV_SAMPLE_FMT_S16, int16_t, AV_SAMPLE_FMT_DBL, av_clip_int16(  lrint(*(const double*)pi * (1<<15))))
CONV_FUNC(AV_SAMPLE_FMT_S32, int32_t, AV_SAMPLE_FMT_DBL, av_clipl_int32(llrint(*(const double*)pi * (1U<<31))))
CONV_FUNC(AV_SAMPLE_FMT_FLT, float  , AV_SAMPLE_FMT_DBL, *(const double*)pi)
CONV_FUNC(AV_SAMPLE_FMT_DBL, double , AV_SAMPLE_FMT_DBL, *(const double*)pi)

#define FMT_PAIR_FUNC(out, in) [(out) + AV_SAMPLE_FMT_NB*(in)] = CONV_FUNC_NAME(out, in)

static conv_func_type * const fmt_pair_to_conv_functions[AV_SAMPLE_FMT_NB*AV_SAMPLE_FMT_NB] = {
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_U8 , AV_SAMPLE_FMT_U8 ),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_U8 ),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_U8 ),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_U8 ),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_U8 ),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_U8 , AV_SAMPLE_FMT_S16),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S16),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S16),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_S16),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_U8 , AV_SAMPLE_FMT_S32),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S32),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S32),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S32),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_S32),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_U8 , AV_SAMPLE_FMT_FLT),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_FLT),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_FLT),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLT),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_FLT),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_U8 , AV_SAMPLE_FMT_DBL),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_DBL),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_DBL),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_DBL),
    FMT_PAIR_FUNC(AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBL),
};

static void cpy1(uint8_t **dst, const uint8_t **src, int len){
    memcpy(*dst, *src, len);
}
static void cpy2(uint8_t **dst, const uint8_t **src, int len){
    memcpy(*dst, *src, 2*len);
}
static void cpy4(uint8_t **dst, const uint8_t **src, int len){
    memcpy(*dst, *src, 4*len);
}
static void cpy8(uint8_t **dst, const uint8_t **src, int len){
    memcpy(*dst, *src, 8*len);
}

AudioConvert *swri_audio_convert_alloc(enum AVSampleFormat out_fmt,
                                       enum AVSampleFormat in_fmt,
                                       int channels, const int *ch_map,
                                       int flags)
{
    AudioConvert *ctx;
    conv_func_type *f = fmt_pair_to_conv_functions[av_get_packed_sample_fmt(out_fmt) + AV_SAMPLE_FMT_NB*av_get_packed_sample_fmt(in_fmt)];

    if (!f)
        return NULL;
    ctx = av_mallocz(sizeof(*ctx));
    if (!ctx)
        return NULL;

    if(channels == 1){
         in_fmt = av_get_planar_sample_fmt( in_fmt);
        out_fmt = av_get_planar_sample_fmt(out_fmt);
    }

    ctx->channels = channels;
    ctx->conv_f   = f;
    ctx->ch_map   = ch_map;
    if (in_fmt == AV_SAMPLE_FMT_U8 || in_fmt == AV_SAMPLE_FMT_U8P)
        memset(ctx->silence, 0x80, sizeof(ctx->silence));

    if(out_fmt == in_fmt && !ch_map) {
        switch(av_get_bytes_per_sample(in_fmt)){
            case 1:ctx->simd_f = cpy1; break;
            case 2:ctx->simd_f = cpy2; break;
            case 4:ctx->simd_f = cpy4; break;
            case 8:ctx->simd_f = cpy8; break;
        }
    }

    if(HAVE_YASM && HAVE_MMX) swri_audio_convert_init_x86(ctx, out_fmt, in_fmt, channels);
    if(ARCH_ARM)              swri_audio_convert_init_arm(ctx, out_fmt, in_fmt, channels);
    if(ARCH_AARCH64)          swri_audio_convert_init_aarch64(ctx, out_fmt, in_fmt, channels);

    return ctx;
}

void swri_audio_convert_free(AudioConvert **ctx)
{
    av_freep(ctx);
}

int swri_audio_convert(AudioConvert *ctx, AudioData *out, AudioData *in, int len)
{
    int ch;
    int off=0;
    const int os= (out->planar ? 1 :out->ch_count) *out->bps;
    unsigned misaligned = 0;

    av_assert0(ctx->channels == out->ch_count);

    if (ctx->in_simd_align_mask) {
        int planes = in->planar ? in->ch_count : 1;
        unsigned m = 0;
        for (ch = 0; ch < planes; ch++)
            m |= (intptr_t)in->ch[ch];
        misaligned |= m & ctx->in_simd_align_mask;
    }
    if (ctx->out_simd_align_mask) {
        int planes = out->planar ? out->ch_count : 1;
        unsigned m = 0;
        for (ch = 0; ch < planes; ch++)
            m |= (intptr_t)out->ch[ch];
        misaligned |= m & ctx->out_simd_align_mask;
    }

    //FIXME optimize common cases

    if(ctx->simd_f && !ctx->ch_map && !misaligned){
        off = len&~15;
        av_assert1(off>=0);
        av_assert1(off<=len);
        av_assert2(ctx->channels == SWR_CH_MAX || !in->ch[ctx->channels]);
        if(off>0){
            if(out->planar == in->planar){
                int planes = out->planar ? out->ch_count : 1;
                for(ch=0; ch<planes; ch++){
                    ctx->simd_f(out->ch+ch, (const uint8_t **)in->ch+ch, off * (out->planar ? 1 :out->ch_count));
                }
            }else{
                ctx->simd_f(out->ch, (const uint8_t **)in->ch, off);
            }
        }
        if(off == len)
            return 0;
    }

    for(ch=0; ch<ctx->channels; ch++){
        const int ich= ctx->ch_map ? ctx->ch_map[ch] : ch;
        const int is= ich < 0 ? 0 : (in->planar ? 1 : in->ch_count) * in->bps;
        const uint8_t *pi= ich < 0 ? ctx->silence : in->ch[ich];
        uint8_t       *po= out->ch[ch];
        uint8_t *end= po + os*len;
        if(!po)
            continue;
        ctx->conv_f(po+off*os, pi+off*is, is, os, end);
    }
    return 0;
}
