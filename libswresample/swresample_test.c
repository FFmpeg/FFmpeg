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

static double get(const void *p, int index, enum AVSampleFormat f){
    switch(f){
    case AV_SAMPLE_FMT_U8 : return ((const uint8_t*)p)[index]/255.0*2-1.0;
    case AV_SAMPLE_FMT_S16: return ((const int16_t*)p)[index]/32767.0;
    case AV_SAMPLE_FMT_S32: return ((const int32_t*)p)[index]/2147483647.0;
    case AV_SAMPLE_FMT_FLT: return ((const float  *)p)[index];
    case AV_SAMPLE_FMT_DBL: return ((const double *)p)[index];
    default: av_assert2(0);
    }
}

static void  set(void *p, int index, enum AVSampleFormat f, double v){
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

int main(int argc, char **argv){
    int in_sample_rate, out_sample_rate, ch ,i, in_ch_layout_index, out_ch_layout_index, osr;
    uint64_t in_ch_layout, out_ch_layout;
    enum AVSampleFormat in_sample_fmt, out_sample_fmt;
    int sample_rates[]={8000,11025,16000,22050,32000};
    uint8_t array_in[SAMPLES*8*8];
    uint8_t array_mid[SAMPLES*8*8*3];
    uint8_t array_out[SAMPLES*8*8+100];
    struct SwrContext * forw_ctx= NULL;
    struct SwrContext *backw_ctx= NULL;

    in_sample_rate=16000;
    for(osr=0; osr<5; osr++){
        out_sample_rate= sample_rates[osr];
        for(in_sample_fmt= AV_SAMPLE_FMT_U8; in_sample_fmt<=AV_SAMPLE_FMT_DBL; in_sample_fmt++){
            for(out_sample_fmt= AV_SAMPLE_FMT_U8; out_sample_fmt<=AV_SAMPLE_FMT_DBL; out_sample_fmt++){
                for(in_ch_layout_index=0; layouts[in_ch_layout_index]; in_ch_layout_index++){
                    in_ch_layout= layouts[in_ch_layout_index];
                    int in_ch_count= av_get_channel_layout_nb_channels(in_ch_layout);
                    for(out_ch_layout_index=0; layouts[out_ch_layout_index]; out_ch_layout_index++){
                        int out_count, mid_count;
                        out_ch_layout= layouts[out_ch_layout_index];
                        int out_ch_count= av_get_channel_layout_nb_channels(out_ch_layout);
                        fprintf(stderr, "ch %d->%d, rate:%5d->%5d, fmt:%s->%s",
                               in_ch_count, out_ch_count,
                               in_sample_rate, out_sample_rate,
                               av_get_sample_fmt_name(in_sample_fmt), av_get_sample_fmt_name(out_sample_fmt));
                        forw_ctx  = swr_alloc2(forw_ctx, out_ch_layout, out_sample_fmt, out_sample_rate,
                                                                  in_ch_layout,  in_sample_fmt,  in_sample_rate, 0, 0);
                        backw_ctx = swr_alloc2(backw_ctx,in_ch_layout,  in_sample_fmt,  in_sample_rate,
                                                                 out_ch_layout, out_sample_fmt, out_sample_rate, 0, 0);
                        if(swr_init( forw_ctx) < 0)
                            fprintf(stderr, "swr_init(->) failed\n");
                        if(swr_init(backw_ctx) < 0)
                            fprintf(stderr, "swr_init(<-) failed\n");
                        if(!forw_ctx)
                            fprintf(stderr, "Failed to init forw_cts\n");
                        if(!backw_ctx)
                            fprintf(stderr, "Failed to init backw_ctx\n");
                               //FIXME test planar
                        for(ch=0; ch<in_ch_count; ch++){
                            for(i=0; i<SAMPLES; i++)
                                set(array_in, ch + i*in_ch_count, in_sample_fmt, sin(i*i*3/SAMPLES));
                        }
                        mid_count= swr_convert(forw_ctx, (      uint8_t*[]){array_mid}, 3*SAMPLES,
                                                                (const uint8_t*[]){array_in }, SAMPLES);
                        out_count= swr_convert(backw_ctx,(      uint8_t*[]){array_out}, 3*SAMPLES,
                                                                (const uint8_t*[]){array_mid}, mid_count);
                        for(ch=0; ch<in_ch_count; ch++){
                            double sse, x, maxdiff=0;
                            double sum_a= 0;
                            double sum_b= 0;
                            double sum_aa= 0;
                            double sum_bb= 0;
                            double sum_ab= 0;
                            for(i=0; i<SAMPLES; i++){
                                double a= get(array_in , ch + i*in_ch_count, in_sample_fmt);
                                double b= get(array_out, ch + i*in_ch_count, in_sample_fmt);
                                sum_a += a;
                                sum_b += b;
                                sum_aa+= a*a;
                                sum_bb+= b*b;
                                sum_ab+= a*b;
                                maxdiff= FFMAX(maxdiff, FFABS(a-b));
                            }
                            x = sum_ab/sum_bb;
                            sse= sum_aa + sum_bb*x*x - 2*x*sum_ab;

                            fprintf(stderr, "[%f %f %f] len:%5d\n", sqrt(sse/SAMPLES), x, maxdiff, out_count);
                        }
                        fprintf(stderr, "\n");
                    }
                }
            }
        }
    }

    return 0;
}
