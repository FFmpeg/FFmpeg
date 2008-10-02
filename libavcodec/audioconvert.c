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
 * @file audioconvert.c
 * audio conversion
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "avcodec.h"
#include "audioconvert.h"

typedef struct SampleFmtInfo {
    const char *name;
    int bits;
} SampleFmtInfo;

/** this table gives more information about formats */
static const SampleFmtInfo sample_fmt_info[SAMPLE_FMT_NB] = {
    [SAMPLE_FMT_U8]  = { .name = "u8",  .bits = 8 },
    [SAMPLE_FMT_S16] = { .name = "s16", .bits = 16 },
    [SAMPLE_FMT_S32] = { .name = "s32", .bits = 32 },
    [SAMPLE_FMT_FLT] = { .name = "flt", .bits = 32 },
    [SAMPLE_FMT_DBL] = { .name = "dbl", .bits = 64 },
};

const char *avcodec_get_sample_fmt_name(int sample_fmt)
{
    if (sample_fmt < 0 || sample_fmt >= SAMPLE_FMT_NB)
        return NULL;
    return sample_fmt_info[sample_fmt].name;
}

enum SampleFormat avcodec_get_sample_fmt(const char* name)
{
    int i;

    for (i=0; i < SAMPLE_FMT_NB; i++)
        if (!strcmp(sample_fmt_info[i].name, name))
            return i;
    return SAMPLE_FMT_NONE;
}

void avcodec_sample_fmt_string (char *buf, int buf_size, int sample_fmt)
{
    /* print header */
    if (sample_fmt < 0)
        snprintf (buf, buf_size, "name  " " depth");
    else if (sample_fmt < SAMPLE_FMT_NB) {
        SampleFmtInfo info= sample_fmt_info[sample_fmt];
        snprintf (buf, buf_size, "%-6s" "   %2d ", info.name, info.bits);
    }
}

struct AVAudioConvert {
    int in_channels, out_channels;
    int fmt_pair;
};

AVAudioConvert *av_audio_convert_alloc(enum SampleFormat out_fmt, int out_channels,
                                       enum SampleFormat in_fmt, int in_channels,
                                       const float *matrix, int flags)
{
    AVAudioConvert *ctx;
    if (in_channels!=out_channels)
        return NULL;  /* FIXME: not supported */
    ctx = av_malloc(sizeof(AVAudioConvert));
    if (!ctx)
        return NULL;
    ctx->in_channels = in_channels;
    ctx->out_channels = out_channels;
    ctx->fmt_pair = out_fmt + SAMPLE_FMT_NB*in_fmt;
    return ctx;
}

void av_audio_convert_free(AVAudioConvert *ctx)
{
    av_free(ctx);
}

int av_audio_convert(AVAudioConvert *ctx,
                           void * const out[6], const int out_stride[6],
                     const void * const  in[6], const int  in_stride[6], int len)
{
    int ch;

    //FIXME optimize common cases

    for(ch=0; ch<ctx->out_channels; ch++){
        const int is=  in_stride[ch];
        const int os= out_stride[ch];
        uint8_t *pi=  in[ch];
        uint8_t *po= out[ch];
        uint8_t *end= po + os*len;
        if(!out[ch])
            continue;

#define CONV(ofmt, otype, ifmt, expr)\
if(ctx->fmt_pair == ofmt + SAMPLE_FMT_NB*ifmt){\
    do{\
        *(otype*)po = expr; pi += is; po += os;\
    }while(po < end);\
}

//FIXME put things below under ifdefs so we do not waste space for cases no codec will need
//FIXME rounding and clipping ?

             CONV(SAMPLE_FMT_U8 , uint8_t, SAMPLE_FMT_U8 ,  *(uint8_t*)pi)
        else CONV(SAMPLE_FMT_S16, int16_t, SAMPLE_FMT_U8 , (*(uint8_t*)pi - 0x80)<<8)
        else CONV(SAMPLE_FMT_S32, int32_t, SAMPLE_FMT_U8 , (*(uint8_t*)pi - 0x80)<<24)
        else CONV(SAMPLE_FMT_FLT, float  , SAMPLE_FMT_U8 , (*(uint8_t*)pi - 0x80)*(1.0 / (1<<7)))
        else CONV(SAMPLE_FMT_DBL, double , SAMPLE_FMT_U8 , (*(uint8_t*)pi - 0x80)*(1.0 / (1<<7)))
        else CONV(SAMPLE_FMT_U8 , uint8_t, SAMPLE_FMT_S16, (*(int16_t*)pi>>8) + 0x80)
        else CONV(SAMPLE_FMT_S16, int16_t, SAMPLE_FMT_S16,  *(int16_t*)pi)
        else CONV(SAMPLE_FMT_S32, int32_t, SAMPLE_FMT_S16,  *(int16_t*)pi<<16)
        else CONV(SAMPLE_FMT_FLT, float  , SAMPLE_FMT_S16,  *(int16_t*)pi*(1.0 / (1<<15)))
        else CONV(SAMPLE_FMT_DBL, double , SAMPLE_FMT_S16,  *(int16_t*)pi*(1.0 / (1<<15)))
        else CONV(SAMPLE_FMT_U8 , uint8_t, SAMPLE_FMT_S32, (*(int32_t*)pi>>24) + 0x80)
        else CONV(SAMPLE_FMT_S16, int16_t, SAMPLE_FMT_S32,  *(int32_t*)pi>>16)
        else CONV(SAMPLE_FMT_S32, int32_t, SAMPLE_FMT_S32,  *(int32_t*)pi)
        else CONV(SAMPLE_FMT_FLT, float  , SAMPLE_FMT_S32,  *(int32_t*)pi*(1.0 / (1<<31)))
        else CONV(SAMPLE_FMT_DBL, double , SAMPLE_FMT_S32,  *(int32_t*)pi*(1.0 / (1<<31)))
        else CONV(SAMPLE_FMT_U8 , uint8_t, SAMPLE_FMT_FLT, lrintf(*(float*)pi * (1<<7)) + 0x80)
        else CONV(SAMPLE_FMT_S16, int16_t, SAMPLE_FMT_FLT, lrintf(*(float*)pi * (1<<15)))
        else CONV(SAMPLE_FMT_S32, int32_t, SAMPLE_FMT_FLT, lrintf(*(float*)pi * (1<<31)))
        else CONV(SAMPLE_FMT_FLT, float  , SAMPLE_FMT_FLT, *(float*)pi)
        else CONV(SAMPLE_FMT_DBL, double , SAMPLE_FMT_FLT, *(float*)pi)
        else CONV(SAMPLE_FMT_U8 , uint8_t, SAMPLE_FMT_DBL, lrint(*(double*)pi * (1<<7)) + 0x80)
        else CONV(SAMPLE_FMT_S16, int16_t, SAMPLE_FMT_DBL, lrint(*(double*)pi * (1<<15)))
        else CONV(SAMPLE_FMT_S32, int32_t, SAMPLE_FMT_DBL, lrint(*(double*)pi * (1<<31)))
        else CONV(SAMPLE_FMT_FLT, float  , SAMPLE_FMT_DBL, *(double*)pi)
        else CONV(SAMPLE_FMT_DBL, double , SAMPLE_FMT_DBL, *(double*)pi)
        else return -1;
    }
    return 0;
}
