/*
 * Copyright (C) 2011 Michael Niedermayer (michaelni@gmx.at)
 *
 * This file is part of libswresample
 *
 * libswresample is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * libswresample is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libswresample; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/audioconvert.h"
#include "swresample.h"
#undef fprintf

#define SAMPLES 1000

#define ASSERT_LEVEL 2

static double get(uint8_t *a[], int ch, int index, int ch_count, enum AVSampleFormat f){
    const uint8_t *p;
    if(av_sample_fmt_is_planar(f)){
        f= av_get_alt_sample_fmt(f, 0);
        p= a[ch];
    }else{
        p= a[0];
        index= ch + index*ch_count;
    }

    switch(f){
    case AV_SAMPLE_FMT_U8 : return ((const uint8_t*)p)[index]/255.0*2-1.0;
    case AV_SAMPLE_FMT_S16: return ((const int16_t*)p)[index]/32767.0;
    case AV_SAMPLE_FMT_S32: return ((const int32_t*)p)[index]/2147483647.0;
    case AV_SAMPLE_FMT_FLT: return ((const float  *)p)[index];
    case AV_SAMPLE_FMT_DBL: return ((const double *)p)[index];
    default: av_assert0(0);
    }
}

static void  set(uint8_t *a[], int ch, int index, int ch_count, enum AVSampleFormat f, double v){
    uint8_t *p;
    if(av_sample_fmt_is_planar(f)){
        f= av_get_alt_sample_fmt(f, 0);
        p= a[ch];
    }else{
        p= a[0];
        index= ch + index*ch_count;
    }
    switch(f){
    case AV_SAMPLE_FMT_U8 : ((uint8_t*)p)[index]= (v+1.0)*255.0/2; break;
    case AV_SAMPLE_FMT_S16: ((int16_t*)p)[index]= v*32767;         break;
    case AV_SAMPLE_FMT_S32: ((int32_t*)p)[index]= v*2147483647;    break;
    case AV_SAMPLE_FMT_FLT: ((float  *)p)[index]= v;               break;
    case AV_SAMPLE_FMT_DBL: ((double *)p)[index]= v;               break;
    default: av_assert2(0);
    }
}

uint64_t layouts[]={
AV_CH_LAYOUT_MONO                    ,
AV_CH_LAYOUT_STEREO                  ,
AV_CH_LAYOUT_2_1                     ,
AV_CH_LAYOUT_SURROUND                ,
AV_CH_LAYOUT_4POINT0                 ,
AV_CH_LAYOUT_2_2                     ,
AV_CH_LAYOUT_QUAD                    ,
AV_CH_LAYOUT_5POINT0                 ,
AV_CH_LAYOUT_5POINT1                 ,
AV_CH_LAYOUT_5POINT0_BACK            ,
AV_CH_LAYOUT_5POINT1_BACK            ,
AV_CH_LAYOUT_7POINT0                 ,
AV_CH_LAYOUT_7POINT1                 ,
AV_CH_LAYOUT_7POINT1_WIDE            ,
0
};

static void setup_array(uint8_t *out[SWR_CH_MAX], uint8_t *in, enum AVSampleFormat format, int samples){
    if(av_sample_fmt_is_planar(format)){
        int i;
        int plane_size= av_get_bytes_per_sample(format&0xFF)*samples;
        format&=0xFF;
        for(i=0; i<SWR_CH_MAX; i++){
            out[i]= in + i*plane_size;
        }
    }else{
        out[0]= in;
    }
}

int main(int argc, char **argv){
    int in_sample_rate, out_sample_rate, ch ,i, in_ch_layout_index, out_ch_layout_index, osr, flush_count;
    uint64_t in_ch_layout, out_ch_layout;
    enum AVSampleFormat in_sample_fmt, out_sample_fmt;
    int sample_rates[]={8000,11025,16000,22050,32000};
    uint8_t array_in[SAMPLES*8*8];
    uint8_t array_mid[SAMPLES*8*8*3];
    uint8_t array_out[SAMPLES*8*8+100];
    uint8_t *ain[SWR_CH_MAX];
    uint8_t *aout[SWR_CH_MAX];
    uint8_t *amid[SWR_CH_MAX];

    struct SwrContext * forw_ctx= NULL;
    struct SwrContext *backw_ctx= NULL;

    in_sample_rate=16000;
    for(osr=0; osr<5; osr++){
        out_sample_rate= sample_rates[osr];
        for(in_sample_fmt= AV_SAMPLE_FMT_U8; in_sample_fmt<=AV_SAMPLE_FMT_DBL; in_sample_fmt++){
            for(out_sample_fmt= AV_SAMPLE_FMT_U8; out_sample_fmt<=AV_SAMPLE_FMT_DBL; out_sample_fmt++){
                for(in_ch_layout_index=0; layouts[in_ch_layout_index]; in_ch_layout_index++){
                    int in_ch_count;
                    in_ch_layout= layouts[in_ch_layout_index];
                    in_ch_count= av_get_channel_layout_nb_channels(in_ch_layout);
                    for(out_ch_layout_index=0; layouts[out_ch_layout_index]; out_ch_layout_index++){
                        int out_count, mid_count, out_ch_count;
                        out_ch_layout= layouts[out_ch_layout_index];
                        out_ch_count= av_get_channel_layout_nb_channels(out_ch_layout);
                        fprintf(stderr, "ch %d->%d, rate:%5d->%5d, fmt:%s->%s",
                               in_ch_count, out_ch_count,
                               in_sample_rate, out_sample_rate,
                               av_get_sample_fmt_name(in_sample_fmt), av_get_sample_fmt_name(out_sample_fmt));
                        forw_ctx  = swr_alloc_set_opts(forw_ctx, out_ch_layout, av_get_alt_sample_fmt(out_sample_fmt, 1), out_sample_rate,
                                                                  in_ch_layout, av_get_alt_sample_fmt( in_sample_fmt, 1),  in_sample_rate,
                                                       0, 0);
                        backw_ctx = swr_alloc_set_opts(backw_ctx, in_ch_layout,  in_sample_fmt,             in_sample_rate,
                                                                 out_ch_layout, av_get_alt_sample_fmt(out_sample_fmt, 1), out_sample_rate,
                                                       0, 0);
                        if(swr_init( forw_ctx) < 0)
                            fprintf(stderr, "swr_init(->) failed\n");
                        if(swr_init(backw_ctx) < 0)
                            fprintf(stderr, "swr_init(<-) failed\n");
                        if(!forw_ctx)
                            fprintf(stderr, "Failed to init forw_cts\n");
                        if(!backw_ctx)
                            fprintf(stderr, "Failed to init backw_ctx\n");
                               //FIXME test planar
                        setup_array(ain , array_in , av_get_alt_sample_fmt( in_sample_fmt, 1),   SAMPLES);
                        setup_array(amid, array_mid, av_get_alt_sample_fmt(out_sample_fmt, 1), 3*SAMPLES);
                        setup_array(aout, array_out,  in_sample_fmt           ,   SAMPLES);
                        for(ch=0; ch<in_ch_count; ch++){
                            for(i=0; i<SAMPLES; i++)
                                set(ain, ch, i, in_ch_count, av_get_alt_sample_fmt(in_sample_fmt, 1), sin(i*i*3/SAMPLES));
                        }
                        mid_count= swr_convert(forw_ctx, amid, 3*SAMPLES, ain, SAMPLES);
                        out_count= swr_convert(backw_ctx,aout, SAMPLES, amid, mid_count);

                        for(ch=0; ch<in_ch_count; ch++){
                            double sse, x, maxdiff=0;
                            double sum_a= 0;
                            double sum_b= 0;
                            double sum_aa= 0;
                            double sum_bb= 0;
                            double sum_ab= 0;
                            for(i=0; i<out_count; i++){
                                double a= get(ain , ch, i, in_ch_count, av_get_alt_sample_fmt(in_sample_fmt, 1));
                                double b= get(aout, ch, i, in_ch_count, in_sample_fmt);
                                sum_a += a;
                                sum_b += b;
                                sum_aa+= a*a;
                                sum_bb+= b*b;
                                sum_ab+= a*b;
                                maxdiff= FFMAX(maxdiff, FFABS(a-b));
                            }
                            x = sum_ab/sum_bb;
                            sse= sum_aa + sum_bb*x*x - 2*x*sum_ab;

                            fprintf(stderr, "[%f %f %f] len:%5d\n", sqrt(sse/out_count), x, maxdiff, out_count);
                        }

                        flush_count=swr_convert(backw_ctx,aout, SAMPLES, 0, 0);
                        if(flush_count){
                            for(ch=0; ch<in_ch_count; ch++){
                                double sse, x, maxdiff=0;
                                double sum_a= 0;
                                double sum_b= 0;
                                double sum_aa= 0;
                                double sum_bb= 0;
                                double sum_ab= 0;
                                for(i=0; i<flush_count; i++){
                                    double a= get(ain , ch, i+out_count, in_ch_count, av_get_alt_sample_fmt(in_sample_fmt, 1));
                                    double b= get(aout, ch, i, in_ch_count, in_sample_fmt);
                                    sum_a += a;
                                    sum_b += b;
                                    sum_aa+= a*a;
                                    sum_bb+= b*b;
                                    sum_ab+= a*b;
                                    maxdiff= FFMAX(maxdiff, FFABS(a-b));
                                }
                                x = sum_ab/sum_bb;
                                sse= sum_aa + sum_bb*x*x - 2*x*sum_ab;

                                fprintf(stderr, "[%f %f %f] len:%5d\n", sqrt(sse/flush_count), x, maxdiff, flush_count);
                            }
                        }


                        fprintf(stderr, "\n");
                    }
                }
            }
        }
    }

    return 0;
}
