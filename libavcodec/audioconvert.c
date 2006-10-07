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
 *
 */

/**
 * @file audioconvert.c
 * audio conversion
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "avcodec.h"

int av_audio_convert(void *maybe_dspcontext_or_something_av_convert_specific,
                     void *out[6], int out_stride[6], enum SampleFormat out_fmt,
                     void * in[6], int  in_stride[6], enum SampleFormat  in_fmt, int len){
    int ch;
    const int isize= FFMIN( in_fmt+1, 4);
    const int osize= FFMIN(out_fmt+1, 4);
    const int fmt_pair= out_fmt + 5*in_fmt;

    //FIXME optimize common cases

    for(ch=0; ch<6; ch++){
        const int is=  in_stride[ch] * isize;
        const int os= out_stride[ch] * osize;
        uint8_t *pi=  in[ch];
        uint8_t *po= out[ch];
        uint8_t *end= po + os;
        if(!out[ch])
            continue;

#define CONV(ofmt, otype, ifmt, expr)\
if(fmt_pair == ofmt + 5*ifmt){\
    do{\
        *(otype*)po = expr; pi += is; po += os;\
    }while(po < end);\
}

//FIXME put things below under ifdefs so we dont waste space for cases no codec will need
//FIXME rounding and cliping ?

             CONV(SAMPLE_FMT_U8 , uint8_t, SAMPLE_FMT_U8 ,  *(uint8_t*)pi)
        else CONV(SAMPLE_FMT_S16, int16_t, SAMPLE_FMT_U8 , (*(uint8_t*)pi - 0x80)<<8)
        else CONV(SAMPLE_FMT_S32, int32_t, SAMPLE_FMT_U8 , (*(uint8_t*)pi - 0x80)<<24)
        else CONV(SAMPLE_FMT_FLT, float  , SAMPLE_FMT_U8 , (*(uint8_t*)pi - 0x80)*(1.0 / (1<<7)))
        else CONV(SAMPLE_FMT_U8 , uint8_t, SAMPLE_FMT_S16, (*(int16_t*)pi>>8) + 0x80)
        else CONV(SAMPLE_FMT_S16, int16_t, SAMPLE_FMT_S16,  *(int16_t*)pi)
        else CONV(SAMPLE_FMT_S32, int32_t, SAMPLE_FMT_S16,  *(int16_t*)pi<<16)
        else CONV(SAMPLE_FMT_FLT, float  , SAMPLE_FMT_S16,  *(int16_t*)pi*(1.0 / (1<<15)))
        else CONV(SAMPLE_FMT_U8 , uint8_t, SAMPLE_FMT_S32, (*(int32_t*)pi>>24) + 0x80)
        else CONV(SAMPLE_FMT_S16, int16_t, SAMPLE_FMT_S32,  *(int32_t*)pi>>16)
        else CONV(SAMPLE_FMT_S32, int32_t, SAMPLE_FMT_S32,  *(int32_t*)pi)
        else CONV(SAMPLE_FMT_FLT, float  , SAMPLE_FMT_S32,  *(int32_t*)pi*(1.0 / (1<<31)))
        else CONV(SAMPLE_FMT_U8 , uint8_t, SAMPLE_FMT_FLT, lrintf(*(float*)pi * (1<<7)) + 0x80)
        else CONV(SAMPLE_FMT_S16, int16_t, SAMPLE_FMT_FLT, lrintf(*(float*)pi * (1<<15)))
        else CONV(SAMPLE_FMT_S32, int32_t, SAMPLE_FMT_FLT, lrintf(*(float*)pi * (1<<31)))
        else CONV(SAMPLE_FMT_FLT, float  , SAMPLE_FMT_FLT, *(float*)pi)
        else return -1;
    }
    return 0;
}
