/*
 * Copyright (C) 2012-2013 Michael Niedermayer (michaelni@gmx.at)
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

#include "noise_shaping_data.c"

int swri_get_dither(SwrContext *s, void *dst, int len, unsigned seed, enum AVSampleFormat noise_fmt) {
    double scale = s->dither.noise_scale;
#define TMP_EXTRA 2
    double *tmp = av_malloc_array(len + TMP_EXTRA, sizeof(double));
    int i;

    if (!tmp)
        return AVERROR(ENOMEM);

    for(i=0; i<len + TMP_EXTRA; i++){
        double v;
        seed = seed* 1664525 + 1013904223;

        switch(s->dither.method){
            case SWR_DITHER_RECTANGULAR: v= ((double)seed) / UINT_MAX - 0.5; break;
            default:
                av_assert0(s->dither.method < SWR_DITHER_NB);
                v = ((double)seed) / UINT_MAX;
                seed = seed*1664525 + 1013904223;
                v-= ((double)seed) / UINT_MAX;
                break;
        }
        tmp[i] = v;
    }

    for(i=0; i<len; i++){
        double v;

        switch(s->dither.method){
            default:
                av_assert0(s->dither.method < SWR_DITHER_NB);
                v = tmp[i];
                break;
            case SWR_DITHER_TRIANGULAR_HIGHPASS :
                v = (- tmp[i] + 2*tmp[i+1] - tmp[i+2]) / sqrt(6);
                break;
        }

        v*= scale;

        switch(noise_fmt){
            case AV_SAMPLE_FMT_S16P: ((int16_t*)dst)[i] = v; break;
            case AV_SAMPLE_FMT_S32P: ((int32_t*)dst)[i] = v; break;
            case AV_SAMPLE_FMT_FLTP: ((float  *)dst)[i] = v; break;
            case AV_SAMPLE_FMT_DBLP: ((double *)dst)[i] = v; break;
            default: av_assert0(0);
        }
    }

    av_free(tmp);
    return 0;
}

av_cold int swri_dither_init(SwrContext *s, enum AVSampleFormat out_fmt, enum AVSampleFormat in_fmt)
{
    int i;
    double scale = 0;

    if (s->dither.method > SWR_DITHER_TRIANGULAR_HIGHPASS && s->dither.method <= SWR_DITHER_NS)
        return AVERROR(EINVAL);

    out_fmt = av_get_packed_sample_fmt(out_fmt);
    in_fmt  = av_get_packed_sample_fmt( in_fmt);

    if(in_fmt == AV_SAMPLE_FMT_FLT || in_fmt == AV_SAMPLE_FMT_DBL){
        if(out_fmt == AV_SAMPLE_FMT_S32) scale = 1.0/(1LL<<31);
        if(out_fmt == AV_SAMPLE_FMT_S16) scale = 1.0/(1LL<<15);
        if(out_fmt == AV_SAMPLE_FMT_U8 ) scale = 1.0/(1LL<< 7);
    }
    if(in_fmt == AV_SAMPLE_FMT_S32 && out_fmt == AV_SAMPLE_FMT_S32 && (s->dither.output_sample_bits&31)) scale = 1;
    if(in_fmt == AV_SAMPLE_FMT_S32 && out_fmt == AV_SAMPLE_FMT_S16) scale = 1<<16;
    if(in_fmt == AV_SAMPLE_FMT_S32 && out_fmt == AV_SAMPLE_FMT_U8 ) scale = 1<<24;
    if(in_fmt == AV_SAMPLE_FMT_S16 && out_fmt == AV_SAMPLE_FMT_U8 ) scale = 1<<8;

    scale *= s->dither.scale;

    if (out_fmt == AV_SAMPLE_FMT_S32 && s->dither.output_sample_bits)
        scale *= 1<<(32-s->dither.output_sample_bits);

    s->dither.ns_pos = 0;
    s->dither.noise_scale=   scale;
    s->dither.ns_scale   =   scale;
    s->dither.ns_scale_1 = scale ? 1/scale : 0;
    memset(s->dither.ns_errors, 0, sizeof(s->dither.ns_errors));
    for (i=0; filters[i].coefs; i++) {
        const filter_t *f = &filters[i];
        if (fabs(s->out_sample_rate - f->rate) / f->rate <= .05 && f->name == s->dither.method) {
            int j;
            s->dither.ns_taps = f->len;
            for (j=0; j<f->len; j++)
                s->dither.ns_coeffs[j] = f->coefs[j];
            s->dither.ns_scale_1 *= 1 - exp(f->gain_cB * M_LN10 * 0.005) * 2 / (1<<(8*av_get_bytes_per_sample(out_fmt)));
            break;
        }
    }
    if (!filters[i].coefs && s->dither.method > SWR_DITHER_NS) {
        av_log(s, AV_LOG_WARNING, "Requested noise shaping dither not available at this sampling rate, using triangular hp dither\n");
        s->dither.method = SWR_DITHER_TRIANGULAR_HIGHPASS;
    }

    av_assert0(!s->preout.count);
    s->dither.noise = s->preout;
    s->dither.temp  = s->preout;
    if (s->dither.method > SWR_DITHER_NS) {
        s->dither.noise.bps = 4;
        s->dither.noise.fmt = AV_SAMPLE_FMT_FLTP;
        s->dither.noise_scale = 1;
    }

    return 0;
}

#define TEMPLATE_DITHER_S16
#include "dither_template.c"
#undef TEMPLATE_DITHER_S16

#define TEMPLATE_DITHER_S32
#include "dither_template.c"
#undef TEMPLATE_DITHER_S32

#define TEMPLATE_DITHER_FLT
#include "dither_template.c"
#undef TEMPLATE_DITHER_FLT

#define TEMPLATE_DITHER_DBL
#include "dither_template.c"
#undef TEMPLATE_DITHER_DBL
