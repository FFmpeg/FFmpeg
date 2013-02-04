/*
 * Copyright (C) 2011-2012 Michael Niedermayer (michaelni@gmx.at)
 * Copyright (c) 2002 Fabrice Bellard
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
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "swresample.h"

#undef time
#include "time.h"
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
    case AV_SAMPLE_FMT_U8 : return ((const uint8_t*)p)[index]/127.0-1.0;
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
    case AV_SAMPLE_FMT_U8 : ((uint8_t*)p)[index]= av_clip_uint8 (lrint((v+1.0)*127));     break;
    case AV_SAMPLE_FMT_S16: ((int16_t*)p)[index]= av_clip_int16 (lrint(v*32767));         break;
    case AV_SAMPLE_FMT_S32: ((int32_t*)p)[index]= av_clipl_int32(llrint(v*2147483647));   break;
    case AV_SAMPLE_FMT_FLT: ((float  *)p)[index]= v;                                      break;
    case AV_SAMPLE_FMT_DBL: ((double *)p)[index]= v;                                      break;
    default: av_assert2(0);
    }
}

static void shift(uint8_t *a[], int index, int ch_count, enum AVSampleFormat f){
    int ch;

    if(av_sample_fmt_is_planar(f)){
        f= av_get_alt_sample_fmt(f, 0);
        for(ch= 0; ch<ch_count; ch++)
            a[ch] += index*av_get_bytes_per_sample(f);
    }else{
        a[0] += index*ch_count*av_get_bytes_per_sample(f);
    }
}

static const enum AVSampleFormat formats[] = {
    AV_SAMPLE_FMT_S16,
    AV_SAMPLE_FMT_FLTP,
    AV_SAMPLE_FMT_S16P,
    AV_SAMPLE_FMT_FLT,
    AV_SAMPLE_FMT_S32P,
    AV_SAMPLE_FMT_S32,
    AV_SAMPLE_FMT_U8P,
    AV_SAMPLE_FMT_U8,
    AV_SAMPLE_FMT_DBLP,
    AV_SAMPLE_FMT_DBL,
};

static const int rates[] = {
    8000,
    11025,
    16000,
    22050,
    32000,
    48000,
};

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

static int cmp(const int *a, const int *b){
    return *a - *b;
}

static void audiogen(void *data, enum AVSampleFormat sample_fmt,
                     int channels, int sample_rate, int nb_samples)
{
    int i, ch, k;
    double v, f, a, ampa;
    double tabf1[SWR_CH_MAX];
    double tabf2[SWR_CH_MAX];
    double taba[SWR_CH_MAX];
    unsigned static rnd;

#define PUT_SAMPLE set(data, ch, k, channels, sample_fmt, v);
#define uint_rand(x) (x = x * 1664525 + 1013904223)
#define dbl_rand(x) (uint_rand(x)*2.0 / (double)UINT_MAX - 1)
    k = 0;

    /* 1 second of single freq sinus at 1000 Hz */
    a = 0;
    for (i = 0; i < 1 * sample_rate && k < nb_samples; i++, k++) {
        v = sin(a) * 0.30;
        for (ch = 0; ch < channels; ch++)
            PUT_SAMPLE
        a += M_PI * 1000.0 * 2.0 / sample_rate;
    }

    /* 1 second of varing frequency between 100 and 10000 Hz */
    a = 0;
    for (i = 0; i < 1 * sample_rate && k < nb_samples; i++, k++) {
        v = sin(a) * 0.30;
        for (ch = 0; ch < channels; ch++)
            PUT_SAMPLE
        f  = 100.0 + (((10000.0 - 100.0) * i) / sample_rate);
        a += M_PI * f * 2.0 / sample_rate;
    }

    /* 0.5 second of low amplitude white noise */
    for (i = 0; i < sample_rate / 2 && k < nb_samples; i++, k++) {
        v = dbl_rand(rnd) * 0.30;
        for (ch = 0; ch < channels; ch++)
            PUT_SAMPLE
    }

    /* 0.5 second of high amplitude white noise */
    for (i = 0; i < sample_rate / 2 && k < nb_samples; i++, k++) {
        v = dbl_rand(rnd);
        for (ch = 0; ch < channels; ch++)
            PUT_SAMPLE
    }

    /* 1 second of unrelated ramps for each channel */
    for (ch = 0; ch < channels; ch++) {
        taba[ch]  = 0;
        tabf1[ch] = 100 + uint_rand(rnd) % 5000;
        tabf2[ch] = 100 + uint_rand(rnd) % 5000;
    }
    for (i = 0; i < 1 * sample_rate && k < nb_samples; i++, k++) {
        for (ch = 0; ch < channels; ch++) {
            v = sin(taba[ch]) * 0.30;
            PUT_SAMPLE
            f = tabf1[ch] + (((tabf2[ch] - tabf1[ch]) * i) / sample_rate);
            taba[ch] += M_PI * f * 2.0 / sample_rate;
        }
    }

    /* 2 seconds of 500 Hz with varying volume */
    a    = 0;
    ampa = 0;
    for (i = 0; i < 2 * sample_rate && k < nb_samples; i++, k++) {
        for (ch = 0; ch < channels; ch++) {
            double amp = (1.0 + sin(ampa)) * 0.15;
            if (ch & 1)
                amp = 0.30 - amp;
            v = sin(a) * amp;
            PUT_SAMPLE
            a    += M_PI * 500.0 * 2.0 / sample_rate;
            ampa += M_PI *  2.0 / sample_rate;
        }
    }
}

int main(int argc, char **argv){
    int in_sample_rate, out_sample_rate, ch ,i, flush_count;
    uint64_t in_ch_layout, out_ch_layout;
    enum AVSampleFormat in_sample_fmt, out_sample_fmt;
    uint8_t array_in[SAMPLES*8*8];
    uint8_t array_mid[SAMPLES*8*8*3];
    uint8_t array_out[SAMPLES*8*8+100];
    uint8_t *ain[SWR_CH_MAX];
    uint8_t *aout[SWR_CH_MAX];
    uint8_t *amid[SWR_CH_MAX];
    int flush_i=0;
    int mode;
    int num_tests = 10000;
    uint32_t seed = 0;
    uint32_t rand_seed = 0;
    int remaining_tests[FF_ARRAY_ELEMS(rates) * FF_ARRAY_ELEMS(layouts) * FF_ARRAY_ELEMS(formats) * FF_ARRAY_ELEMS(layouts) * FF_ARRAY_ELEMS(formats)];
    int max_tests = FF_ARRAY_ELEMS(remaining_tests);
    int test;
    int specific_test= -1;

    struct SwrContext * forw_ctx= NULL;
    struct SwrContext *backw_ctx= NULL;

    if (argc > 1) {
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
            av_log(NULL, AV_LOG_INFO, "Usage: swresample-test [<num_tests>[ <test>]]  \n"
                   "num_tests           Default is %d\n", num_tests);
            return 0;
        }
        num_tests = strtol(argv[1], NULL, 0);
        if(num_tests < 0) {
            num_tests = -num_tests;
            rand_seed = time(0);
        }
        if(num_tests<= 0 || num_tests>max_tests)
            num_tests = max_tests;
        if(argc > 2) {
            specific_test = strtol(argv[1], NULL, 0);
        }
    }

    for(i=0; i<max_tests; i++)
        remaining_tests[i] = i;

    for(test=0; test<num_tests; test++){
        unsigned r;
        uint_rand(seed);
        r = (seed * (uint64_t)(max_tests - test)) >>32;
        FFSWAP(int, remaining_tests[r], remaining_tests[max_tests - test - 1]);
    }
    qsort(remaining_tests + max_tests - num_tests, num_tests, sizeof(remaining_tests[0]), (void*)cmp);
    in_sample_rate=16000;
    for(test=0; test<num_tests; test++){
        char  in_layout_string[256];
        char out_layout_string[256];
        unsigned vector= remaining_tests[max_tests - test - 1];
        int in_ch_count;
        int out_count, mid_count, out_ch_count;

        in_ch_layout    = layouts[vector % FF_ARRAY_ELEMS(layouts)]; vector /= FF_ARRAY_ELEMS(layouts);
        out_ch_layout   = layouts[vector % FF_ARRAY_ELEMS(layouts)]; vector /= FF_ARRAY_ELEMS(layouts);
        in_sample_fmt   = formats[vector % FF_ARRAY_ELEMS(formats)]; vector /= FF_ARRAY_ELEMS(formats);
        out_sample_fmt  = formats[vector % FF_ARRAY_ELEMS(formats)]; vector /= FF_ARRAY_ELEMS(formats);
        out_sample_rate = rates  [vector % FF_ARRAY_ELEMS(rates  )]; vector /= FF_ARRAY_ELEMS(rates);
        av_assert0(!vector);

        if(specific_test == 0){
            if(out_sample_rate != in_sample_rate || in_ch_layout != out_ch_layout)
                continue;
        }

        in_ch_count= av_get_channel_layout_nb_channels(in_ch_layout);
        out_ch_count= av_get_channel_layout_nb_channels(out_ch_layout);
        av_get_channel_layout_string( in_layout_string, sizeof( in_layout_string),  in_ch_count,  in_ch_layout);
        av_get_channel_layout_string(out_layout_string, sizeof(out_layout_string), out_ch_count, out_ch_layout);
        fprintf(stderr, "TEST: %s->%s, rate:%5d->%5d, fmt:%s->%s\n",
                in_layout_string, out_layout_string,
                in_sample_rate, out_sample_rate,
                av_get_sample_fmt_name(in_sample_fmt), av_get_sample_fmt_name(out_sample_fmt));
        forw_ctx  = swr_alloc_set_opts(forw_ctx, out_ch_layout, out_sample_fmt,  out_sample_rate,
                                                    in_ch_layout,  in_sample_fmt,  in_sample_rate,
                                        0, 0);
        backw_ctx = swr_alloc_set_opts(backw_ctx, in_ch_layout,  in_sample_fmt,             in_sample_rate,
                                                    out_ch_layout, out_sample_fmt, out_sample_rate,
                                        0, 0);
        if(!forw_ctx) {
            fprintf(stderr, "Failed to init forw_cts\n");
            return 1;
        }
        if(!backw_ctx) {
            fprintf(stderr, "Failed to init backw_ctx\n");
            return 1;
        }
        if(swr_init( forw_ctx) < 0)
            fprintf(stderr, "swr_init(->) failed\n");
        if(swr_init(backw_ctx) < 0)
            fprintf(stderr, "swr_init(<-) failed\n");
                //FIXME test planar
        setup_array(ain , array_in ,  in_sample_fmt,   SAMPLES);
        setup_array(amid, array_mid, out_sample_fmt, 3*SAMPLES);
        setup_array(aout, array_out,  in_sample_fmt           ,   SAMPLES);
#if 0
        for(ch=0; ch<in_ch_count; ch++){
            for(i=0; i<SAMPLES; i++)
                set(ain, ch, i, in_ch_count, in_sample_fmt, sin(i*i*3/SAMPLES));
        }
#else
        audiogen(ain, in_sample_fmt, in_ch_count, SAMPLES/6+1, SAMPLES);
#endif
        mode = uint_rand(rand_seed) % 3;
        if(mode==0 /*|| out_sample_rate == in_sample_rate*/) {
            mid_count= swr_convert(forw_ctx, amid, 3*SAMPLES, (const uint8_t **)ain, SAMPLES);
        } else if(mode==1){
            mid_count= swr_convert(forw_ctx, amid,         0, (const uint8_t **)ain, SAMPLES);
            mid_count+=swr_convert(forw_ctx, amid, 3*SAMPLES, (const uint8_t **)ain,       0);
        } else {
            int tmp_count;
            mid_count= swr_convert(forw_ctx, amid,         0, (const uint8_t **)ain,       1);
            av_assert0(mid_count==0);
            shift(ain,  1, in_ch_count, in_sample_fmt);
            mid_count+=swr_convert(forw_ctx, amid, 3*SAMPLES, (const uint8_t **)ain,       0);
            shift(amid,  mid_count, out_ch_count, out_sample_fmt); tmp_count = mid_count;
            mid_count+=swr_convert(forw_ctx, amid,         2, (const uint8_t **)ain,       2);
            shift(amid,  mid_count-tmp_count, out_ch_count, out_sample_fmt); tmp_count = mid_count;
            shift(ain,  2, in_ch_count, in_sample_fmt);
            mid_count+=swr_convert(forw_ctx, amid,         1, (const uint8_t **)ain, SAMPLES-3);
            shift(amid,  mid_count-tmp_count, out_ch_count, out_sample_fmt); tmp_count = mid_count;
            shift(ain, -3, in_ch_count, in_sample_fmt);
            mid_count+=swr_convert(forw_ctx, amid, 3*SAMPLES, (const uint8_t **)ain,       0);
            shift(amid,  -tmp_count, out_ch_count, out_sample_fmt);
        }
        out_count= swr_convert(backw_ctx,aout, SAMPLES, (const uint8_t **)amid, mid_count);

        for(ch=0; ch<in_ch_count; ch++){
            double sse, maxdiff=0;
            double sum_a= 0;
            double sum_b= 0;
            double sum_aa= 0;
            double sum_bb= 0;
            double sum_ab= 0;
            for(i=0; i<out_count; i++){
                double a= get(ain , ch, i, in_ch_count, in_sample_fmt);
                double b= get(aout, ch, i, in_ch_count, in_sample_fmt);
                sum_a += a;
                sum_b += b;
                sum_aa+= a*a;
                sum_bb+= b*b;
                sum_ab+= a*b;
                maxdiff= FFMAX(maxdiff, FFABS(a-b));
            }
            sse= sum_aa + sum_bb - 2*sum_ab;
            if(sse < 0 && sse > -0.00001) sse=0; //fix rounding error

            fprintf(stderr, "[e:%f c:%f max:%f] len:%5d\n", out_count ? sqrt(sse/out_count) : 0, sum_ab/(sqrt(sum_aa*sum_bb)), maxdiff, out_count);
        }

        flush_i++;
        flush_i%=21;
        flush_count = swr_convert(backw_ctx,aout, flush_i, 0, 0);
        shift(aout,  flush_i, in_ch_count, in_sample_fmt);
        flush_count+= swr_convert(backw_ctx,aout, SAMPLES-flush_i, 0, 0);
        shift(aout, -flush_i, in_ch_count, in_sample_fmt);
        if(flush_count){
            for(ch=0; ch<in_ch_count; ch++){
                double sse, maxdiff=0;
                double sum_a= 0;
                double sum_b= 0;
                double sum_aa= 0;
                double sum_bb= 0;
                double sum_ab= 0;
                for(i=0; i<flush_count; i++){
                    double a= get(ain , ch, i+out_count, in_ch_count, in_sample_fmt);
                    double b= get(aout, ch, i, in_ch_count, in_sample_fmt);
                    sum_a += a;
                    sum_b += b;
                    sum_aa+= a*a;
                    sum_bb+= b*b;
                    sum_ab+= a*b;
                    maxdiff= FFMAX(maxdiff, FFABS(a-b));
                }
                sse= sum_aa + sum_bb - 2*sum_ab;
                if(sse < 0 && sse > -0.00001) sse=0; //fix rounding error

                fprintf(stderr, "[e:%f c:%f max:%f] len:%5d F:%3d\n", sqrt(sse/flush_count), sum_ab/(sqrt(sum_aa*sum_bb)), maxdiff, flush_count, flush_i);
            }
        }


        fprintf(stderr, "\n");
    }

    return 0;
}
