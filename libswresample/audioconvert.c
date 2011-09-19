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


struct AVAudioConvert {
    int channels;
    int fmt_pair;
};

AVAudioConvert *swr_audio_convert_alloc(enum AVSampleFormat out_fmt,
                                        enum AVSampleFormat in_fmt,
                                        int channels, int flags)
{
    AVAudioConvert *ctx;
    ctx = av_malloc(sizeof(AVAudioConvert));
    if (!ctx)
        return NULL;
    ctx->channels = channels;
    ctx->fmt_pair = out_fmt + AV_SAMPLE_FMT_NB*in_fmt;
    return ctx;
}

void swr_audio_convert_free(AVAudioConvert **ctx)
{
    av_freep(ctx);
}

int swr_audio_convert(AVAudioConvert *ctx, AudioData *out, AudioData*in, int len)
{
    int ch;

    av_assert0(ctx->channels == out->ch_count);

    //FIXME optimize common cases

    for(ch=0; ch<ctx->channels; ch++){
        const int is= (in ->planar ? 1 : in->ch_count) * in->bps;
        const int os= (out->planar ? 1 :out->ch_count) *out->bps;
        const uint8_t *pi= in ->ch[ch];
        uint8_t       *po= out->ch[ch];
        uint8_t *end= po + os*len;
        if(!po)
            continue;

#define CONV(ofmt, otype, ifmt, expr)\
if(ctx->fmt_pair == ofmt + AV_SAMPLE_FMT_NB*ifmt){\
    do{\
        *(otype*)po = expr; pi += is; po += os;\
    }while(po < end);\
}

//FIXME put things below under ifdefs so we do not waste space for cases no codec will need
//FIXME rounding ?

             CONV(AV_SAMPLE_FMT_U8 , uint8_t, AV_SAMPLE_FMT_U8 ,  *(const uint8_t*)pi)
        else CONV(AV_SAMPLE_FMT_S16, int16_t, AV_SAMPLE_FMT_U8 , (*(const uint8_t*)pi - 0x80)<<8)
        else CONV(AV_SAMPLE_FMT_S32, int32_t, AV_SAMPLE_FMT_U8 , (*(const uint8_t*)pi - 0x80)<<24)
        else CONV(AV_SAMPLE_FMT_FLT, float  , AV_SAMPLE_FMT_U8 , (*(const uint8_t*)pi - 0x80)*(1.0 / (1<<7)))
        else CONV(AV_SAMPLE_FMT_DBL, double , AV_SAMPLE_FMT_U8 , (*(const uint8_t*)pi - 0x80)*(1.0 / (1<<7)))
        else CONV(AV_SAMPLE_FMT_U8 , uint8_t, AV_SAMPLE_FMT_S16, (*(const int16_t*)pi>>8) + 0x80)
        else CONV(AV_SAMPLE_FMT_S16, int16_t, AV_SAMPLE_FMT_S16,  *(const int16_t*)pi)
        else CONV(AV_SAMPLE_FMT_S32, int32_t, AV_SAMPLE_FMT_S16,  *(const int16_t*)pi<<16)
        else CONV(AV_SAMPLE_FMT_FLT, float  , AV_SAMPLE_FMT_S16,  *(const int16_t*)pi*(1.0 / (1<<15)))
        else CONV(AV_SAMPLE_FMT_DBL, double , AV_SAMPLE_FMT_S16,  *(const int16_t*)pi*(1.0 / (1<<15)))
        else CONV(AV_SAMPLE_FMT_U8 , uint8_t, AV_SAMPLE_FMT_S32, (*(const int32_t*)pi>>24) + 0x80)
        else CONV(AV_SAMPLE_FMT_S16, int16_t, AV_SAMPLE_FMT_S32,  *(const int32_t*)pi>>16)
        else CONV(AV_SAMPLE_FMT_S32, int32_t, AV_SAMPLE_FMT_S32,  *(const int32_t*)pi)
        else CONV(AV_SAMPLE_FMT_FLT, float  , AV_SAMPLE_FMT_S32,  *(const int32_t*)pi*(1.0 / (1U<<31)))
        else CONV(AV_SAMPLE_FMT_DBL, double , AV_SAMPLE_FMT_S32,  *(const int32_t*)pi*(1.0 / (1U<<31)))
        else CONV(AV_SAMPLE_FMT_U8 , uint8_t, AV_SAMPLE_FMT_FLT, av_clip_uint8(  lrintf(*(const float*)pi * (1<<7)) + 0x80))
        else CONV(AV_SAMPLE_FMT_S16, int16_t, AV_SAMPLE_FMT_FLT, av_clip_int16(  lrintf(*(const float*)pi * (1<<15))))
        else CONV(AV_SAMPLE_FMT_S32, int32_t, AV_SAMPLE_FMT_FLT, av_clipl_int32(llrintf(*(const float*)pi * (1U<<31))))
        else CONV(AV_SAMPLE_FMT_FLT, float  , AV_SAMPLE_FMT_FLT, *(const float*)pi)
        else CONV(AV_SAMPLE_FMT_DBL, double , AV_SAMPLE_FMT_FLT, *(const float*)pi)
        else CONV(AV_SAMPLE_FMT_U8 , uint8_t, AV_SAMPLE_FMT_DBL, av_clip_uint8(  lrint(*(const double*)pi * (1<<7)) + 0x80))
        else CONV(AV_SAMPLE_FMT_S16, int16_t, AV_SAMPLE_FMT_DBL, av_clip_int16(  lrint(*(const double*)pi * (1<<15))))
        else CONV(AV_SAMPLE_FMT_S32, int32_t, AV_SAMPLE_FMT_DBL, av_clipl_int32(llrint(*(const double*)pi * (1U<<31))))
        else CONV(AV_SAMPLE_FMT_FLT, float  , AV_SAMPLE_FMT_DBL, *(const double*)pi)
        else CONV(AV_SAMPLE_FMT_DBL, double , AV_SAMPLE_FMT_DBL, *(const double*)pi)
        else return -1;
    }
    return 0;
}
