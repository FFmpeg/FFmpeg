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

#include "libavutil/avassert.h"
#include "swresample_internal.h"

void swri_get_dither(SwrContext *s, void *dst, int len, unsigned seed, enum AVSampleFormat out_fmt, enum AVSampleFormat in_fmt) {
    double scale = 0;
#define TMP_EXTRA 2
    double *tmp = av_malloc((len + TMP_EXTRA) * sizeof(double));
    int i;

    out_fmt = av_get_packed_sample_fmt(out_fmt);
    in_fmt  = av_get_packed_sample_fmt( in_fmt);

    if(in_fmt == AV_SAMPLE_FMT_FLT || in_fmt == AV_SAMPLE_FMT_DBL){
        if(out_fmt == AV_SAMPLE_FMT_S32) scale = 1.0/(1L<<31);
        if(out_fmt == AV_SAMPLE_FMT_S16) scale = 1.0/(1L<<15);
        if(out_fmt == AV_SAMPLE_FMT_U8 ) scale = 1.0/(1L<< 7);
    }
    if(in_fmt == AV_SAMPLE_FMT_S32 && out_fmt == AV_SAMPLE_FMT_S16) scale = 1L<<16;
    if(in_fmt == AV_SAMPLE_FMT_S32 && out_fmt == AV_SAMPLE_FMT_U8 ) scale = 1L<<24;
    if(in_fmt == AV_SAMPLE_FMT_S16 && out_fmt == AV_SAMPLE_FMT_U8 ) scale = 1L<<8;

    scale *= s->dither_scale;

    for(i=0; i<len + TMP_EXTRA; i++){
        double v;
        seed = seed* 1664525 + 1013904223;

        switch(s->dither_method){
            case SWR_DITHER_RECTANGULAR: v= ((double)seed) / UINT_MAX - 0.5; break;
            case SWR_DITHER_TRIANGULAR :
            case SWR_DITHER_TRIANGULAR_HIGHPASS :
                v = ((double)seed) / UINT_MAX;
                seed = seed*1664525 + 1013904223;
                v-= ((double)seed) / UINT_MAX;
                break;
            default: av_assert0(0);
        }
        tmp[i] = v;
    }

    for(i=0; i<len; i++){
        double v;

        switch(s->dither_method){
            case SWR_DITHER_RECTANGULAR:
            case SWR_DITHER_TRIANGULAR :
                v = tmp[i];
                break;
            case SWR_DITHER_TRIANGULAR_HIGHPASS :
                v = (- tmp[i] + 2*tmp[i+1] - tmp[i+2]) / sqrt(6);
                break;
            default: av_assert0(0);
        }

        v*= scale;

        switch(in_fmt){
            case AV_SAMPLE_FMT_S16: ((int16_t*)dst)[i] = v; break;
            case AV_SAMPLE_FMT_S32: ((int32_t*)dst)[i] = v; break;
            case AV_SAMPLE_FMT_FLT: ((float  *)dst)[i] = v; break;
            case AV_SAMPLE_FMT_DBL: ((double *)dst)[i] = v; break;
            default: av_assert0(0);
        }
    }

    av_free(tmp);
}
