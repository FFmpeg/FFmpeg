/*
 * audio resampling
 * Copyright (c) 2004-2012 Michael Niedermayer <michaelni@gmx.at>
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
 * audio resampling
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "libavutil/avassert.h"
#include "resample.h"

/**
 * 0th order modified bessel function of the first kind.
 */
static double bessel(double x){
    double v=1;
    double lastv=0;
    double t=1;
    int i;
    static const double inv[100]={
 1.0/( 1* 1), 1.0/( 2* 2), 1.0/( 3* 3), 1.0/( 4* 4), 1.0/( 5* 5), 1.0/( 6* 6), 1.0/( 7* 7), 1.0/( 8* 8), 1.0/( 9* 9), 1.0/(10*10),
 1.0/(11*11), 1.0/(12*12), 1.0/(13*13), 1.0/(14*14), 1.0/(15*15), 1.0/(16*16), 1.0/(17*17), 1.0/(18*18), 1.0/(19*19), 1.0/(20*20),
 1.0/(21*21), 1.0/(22*22), 1.0/(23*23), 1.0/(24*24), 1.0/(25*25), 1.0/(26*26), 1.0/(27*27), 1.0/(28*28), 1.0/(29*29), 1.0/(30*30),
 1.0/(31*31), 1.0/(32*32), 1.0/(33*33), 1.0/(34*34), 1.0/(35*35), 1.0/(36*36), 1.0/(37*37), 1.0/(38*38), 1.0/(39*39), 1.0/(40*40),
 1.0/(41*41), 1.0/(42*42), 1.0/(43*43), 1.0/(44*44), 1.0/(45*45), 1.0/(46*46), 1.0/(47*47), 1.0/(48*48), 1.0/(49*49), 1.0/(50*50),
 1.0/(51*51), 1.0/(52*52), 1.0/(53*53), 1.0/(54*54), 1.0/(55*55), 1.0/(56*56), 1.0/(57*57), 1.0/(58*58), 1.0/(59*59), 1.0/(60*60),
 1.0/(61*61), 1.0/(62*62), 1.0/(63*63), 1.0/(64*64), 1.0/(65*65), 1.0/(66*66), 1.0/(67*67), 1.0/(68*68), 1.0/(69*69), 1.0/(70*70),
 1.0/(71*71), 1.0/(72*72), 1.0/(73*73), 1.0/(74*74), 1.0/(75*75), 1.0/(76*76), 1.0/(77*77), 1.0/(78*78), 1.0/(79*79), 1.0/(80*80),
 1.0/(81*81), 1.0/(82*82), 1.0/(83*83), 1.0/(84*84), 1.0/(85*85), 1.0/(86*86), 1.0/(87*87), 1.0/(88*88), 1.0/(89*89), 1.0/(90*90),
 1.0/(91*91), 1.0/(92*92), 1.0/(93*93), 1.0/(94*94), 1.0/(95*95), 1.0/(96*96), 1.0/(97*97), 1.0/(98*98), 1.0/(99*99), 1.0/(10000)
    };

    x= x*x/4;
    for(i=0; v != lastv; i++){
        lastv=v;
        t *= x*inv[i];
        v += t;
        av_assert2(i<99);
    }
    return v;
}

/**
 * builds a polyphase filterbank.
 * @param factor resampling factor
 * @param scale wanted sum of coefficients for each filter
 * @param filter_type  filter type
 * @param kaiser_beta  kaiser window beta
 * @return 0 on success, negative on error
 */
static int build_filter(ResampleContext *c, void *filter, double factor, int tap_count, int alloc, int phase_count, int scale,
                        int filter_type, int kaiser_beta){
    int ph, i;
    double x, y, w;
    double *tab = av_malloc_array(tap_count,  sizeof(*tab));
    const int center= (tap_count-1)/2;

    if (!tab)
        return AVERROR(ENOMEM);

    /* if upsampling, only need to interpolate, no filter */
    if (factor > 1.0)
        factor = 1.0;

    for(ph=0;ph<phase_count;ph++) {
        double norm = 0;
        for(i=0;i<tap_count;i++) {
            x = M_PI * ((double)(i - center) - (double)ph / phase_count) * factor;
            if (x == 0) y = 1.0;
            else        y = sin(x) / x;
            switch(filter_type){
            case SWR_FILTER_TYPE_CUBIC:{
                const float d= -0.5; //first order derivative = -0.5
                x = fabs(((double)(i - center) - (double)ph / phase_count) * factor);
                if(x<1.0) y= 1 - 3*x*x + 2*x*x*x + d*(            -x*x + x*x*x);
                else      y=                       d*(-4 + 8*x - 5*x*x + x*x*x);
                break;}
            case SWR_FILTER_TYPE_BLACKMAN_NUTTALL:
                w = 2.0*x / (factor*tap_count) + M_PI;
                y *= 0.3635819 - 0.4891775 * cos(w) + 0.1365995 * cos(2*w) - 0.0106411 * cos(3*w);
                break;
            case SWR_FILTER_TYPE_KAISER:
                w = 2.0*x / (factor*tap_count*M_PI);
                y *= bessel(kaiser_beta*sqrt(FFMAX(1-w*w, 0)));
                break;
            default:
                av_assert0(0);
            }

            tab[i] = y;
            norm += y;
        }

        /* normalize so that an uniform color remains the same */
        switch(c->format){
        case AV_SAMPLE_FMT_S16P:
            for(i=0;i<tap_count;i++)
                ((int16_t*)filter)[ph * alloc + i] = av_clip(lrintf(tab[i] * scale / norm), INT16_MIN, INT16_MAX);
            break;
        case AV_SAMPLE_FMT_S32P:
            for(i=0;i<tap_count;i++)
                ((int32_t*)filter)[ph * alloc + i] = av_clipl_int32(llrint(tab[i] * scale / norm));
            break;
        case AV_SAMPLE_FMT_FLTP:
            for(i=0;i<tap_count;i++)
                ((float*)filter)[ph * alloc + i] = tab[i] * scale / norm;
            break;
        case AV_SAMPLE_FMT_DBLP:
            for(i=0;i<tap_count;i++)
                ((double*)filter)[ph * alloc + i] = tab[i] * scale / norm;
            break;
        }
    }
#if 0
    {
#define LEN 1024
        int j,k;
        double sine[LEN + tap_count];
        double filtered[LEN];
        double maxff=-2, minff=2, maxsf=-2, minsf=2;
        for(i=0; i<LEN; i++){
            double ss=0, sf=0, ff=0;
            for(j=0; j<LEN+tap_count; j++)
                sine[j]= cos(i*j*M_PI/LEN);
            for(j=0; j<LEN; j++){
                double sum=0;
                ph=0;
                for(k=0; k<tap_count; k++)
                    sum += filter[ph * tap_count + k] * sine[k+j];
                filtered[j]= sum / (1<<FILTER_SHIFT);
                ss+= sine[j + center] * sine[j + center];
                ff+= filtered[j] * filtered[j];
                sf+= sine[j + center] * filtered[j];
            }
            ss= sqrt(2*ss/LEN);
            ff= sqrt(2*ff/LEN);
            sf= 2*sf/LEN;
            maxff= FFMAX(maxff, ff);
            minff= FFMIN(minff, ff);
            maxsf= FFMAX(maxsf, sf);
            minsf= FFMIN(minsf, sf);
            if(i%11==0){
                av_log(NULL, AV_LOG_ERROR, "i:%4d ss:%f ff:%13.6e-%13.6e sf:%13.6e-%13.6e\n", i, ss, maxff, minff, maxsf, minsf);
                minff=minsf= 2;
                maxff=maxsf= -2;
            }
        }
    }
#endif

    av_free(tab);
    return 0;
}

static ResampleContext *resample_init(ResampleContext *c, int out_rate, int in_rate, int filter_size, int phase_shift, int linear,
                                    double cutoff0, enum AVSampleFormat format, enum SwrFilterType filter_type, int kaiser_beta,
                                    double precision, int cheby)
{
    double cutoff = cutoff0? cutoff0 : 0.97;
    double factor= FFMIN(out_rate * cutoff / in_rate, 1.0);
    int phase_count= 1<<phase_shift;

    if (!c || c->phase_shift != phase_shift || c->linear!=linear || c->factor != factor
           || c->filter_length != FFMAX((int)ceil(filter_size/factor), 1) || c->format != format
           || c->filter_type != filter_type || c->kaiser_beta != kaiser_beta) {
        c = av_mallocz(sizeof(*c));
        if (!c)
            return NULL;

        c->format= format;

        c->felem_size= av_get_bytes_per_sample(c->format);

        switch(c->format){
        case AV_SAMPLE_FMT_S16P:
            c->filter_shift = 15;
            break;
        case AV_SAMPLE_FMT_S32P:
            c->filter_shift = 30;
            break;
        case AV_SAMPLE_FMT_FLTP:
        case AV_SAMPLE_FMT_DBLP:
            c->filter_shift = 0;
            break;
        default:
            av_log(NULL, AV_LOG_ERROR, "Unsupported sample format\n");
            av_assert0(0);
        }

        if (filter_size/factor > INT32_MAX/256) {
            av_log(NULL, AV_LOG_ERROR, "Filter length too large\n");
            goto error;
        }

        c->phase_shift   = phase_shift;
        c->phase_mask    = phase_count - 1;
        c->linear        = linear;
        c->factor        = factor;
        c->filter_length = FFMAX((int)ceil(filter_size/factor), 1);
        c->filter_alloc  = FFALIGN(c->filter_length, 8);
        c->filter_bank   = av_calloc(c->filter_alloc, (phase_count+1)*c->felem_size);
        c->filter_type   = filter_type;
        c->kaiser_beta   = kaiser_beta;
        if (!c->filter_bank)
            goto error;
        if (build_filter(c, (void*)c->filter_bank, factor, c->filter_length, c->filter_alloc, phase_count, 1<<c->filter_shift, filter_type, kaiser_beta))
            goto error;
        memcpy(c->filter_bank + (c->filter_alloc*phase_count+1)*c->felem_size, c->filter_bank, (c->filter_alloc-1)*c->felem_size);
        memcpy(c->filter_bank + (c->filter_alloc*phase_count  )*c->felem_size, c->filter_bank + (c->filter_alloc - 1)*c->felem_size, c->felem_size);
    }

    c->compensation_distance= 0;
    if(!av_reduce(&c->src_incr, &c->dst_incr, out_rate, in_rate * (int64_t)phase_count, INT32_MAX/2))
        goto error;
    c->ideal_dst_incr = c->dst_incr;
    c->dst_incr_div   = c->dst_incr / c->src_incr;
    c->dst_incr_mod   = c->dst_incr % c->src_incr;

    c->index= -phase_count*((c->filter_length-1)/2);
    c->frac= 0;

    swri_resample_dsp_init(c);

    return c;
error:
    av_freep(&c->filter_bank);
    av_free(c);
    return NULL;
}

static void resample_free(ResampleContext **c){
    if(!*c)
        return;
    av_freep(&(*c)->filter_bank);
    av_freep(c);
}

static int set_compensation(ResampleContext *c, int sample_delta, int compensation_distance){
    c->compensation_distance= compensation_distance;
    if (compensation_distance)
        c->dst_incr = c->ideal_dst_incr - c->ideal_dst_incr * (int64_t)sample_delta / compensation_distance;
    else
        c->dst_incr = c->ideal_dst_incr;

    c->dst_incr_div   = c->dst_incr / c->src_incr;
    c->dst_incr_mod   = c->dst_incr % c->src_incr;

    return 0;
}

static int swri_resample(ResampleContext *c,
                         uint8_t *dst, const uint8_t *src, int *consumed,
                         int src_size, int dst_size, int update_ctx)
{
    if (c->filter_length == 1 && c->phase_shift == 0) {
        int index= c->index;
        int frac= c->frac;
        int64_t index2= (1LL<<32)*c->frac/c->src_incr + (1LL<<32)*index;
        int64_t incr= (1LL<<32) * c->dst_incr / c->src_incr;
        int new_size = (src_size * (int64_t)c->src_incr - frac + c->dst_incr - 1) / c->dst_incr;

        dst_size= FFMIN(dst_size, new_size);
        c->dsp.resample_one(dst, src, dst_size, index2, incr);

        index += dst_size * c->dst_incr_div;
        index += (frac + dst_size * (int64_t)c->dst_incr_mod) / c->src_incr;
        av_assert2(index >= 0);
        *consumed= index;
        if (update_ctx) {
            c->frac   = (frac + dst_size * (int64_t)c->dst_incr_mod) % c->src_incr;
            c->index = 0;
        }
    } else {
        int64_t end_index = (1LL + src_size - c->filter_length) << c->phase_shift;
        int64_t delta_frac = (end_index - c->index) * c->src_incr - c->frac;
        int delta_n = (delta_frac + c->dst_incr - 1) / c->dst_incr;

        dst_size = FFMIN(dst_size, delta_n);
        if (dst_size > 0) {
            *consumed = c->dsp.resample(c, dst, src, dst_size, update_ctx);
        } else {
            *consumed = 0;
        }
    }

    return dst_size;
}

static int multiple_resample(ResampleContext *c, AudioData *dst, int dst_size, AudioData *src, int src_size, int *consumed){
    int i, ret= -1;
    int av_unused mm_flags = av_get_cpu_flags();
    int need_emms = c->format == AV_SAMPLE_FMT_S16P && ARCH_X86_32 &&
                    (mm_flags & (AV_CPU_FLAG_MMX2 | AV_CPU_FLAG_SSE2)) == AV_CPU_FLAG_MMX2;
    int64_t max_src_size = (INT64_MAX >> (c->phase_shift+1)) / c->src_incr;

    if (c->compensation_distance)
        dst_size = FFMIN(dst_size, c->compensation_distance);
    src_size = FFMIN(src_size, max_src_size);

    for(i=0; i<dst->ch_count; i++){
        ret= swri_resample(c, dst->ch[i], src->ch[i],
                           consumed, src_size, dst_size, i+1==dst->ch_count);
    }
    if(need_emms)
        emms_c();

    if (c->compensation_distance) {
        c->compensation_distance -= ret;
        if (!c->compensation_distance) {
            c->dst_incr     = c->ideal_dst_incr;
            c->dst_incr_div = c->dst_incr / c->src_incr;
            c->dst_incr_mod = c->dst_incr % c->src_incr;
        }
    }

    return ret;
}

static int64_t get_delay(struct SwrContext *s, int64_t base){
    ResampleContext *c = s->resample;
    int64_t num = s->in_buffer_count - (c->filter_length-1)/2;
    num <<= c->phase_shift;
    num -= c->index;
    num *= c->src_incr;
    num -= c->frac;
    return av_rescale(num, base, s->in_sample_rate*(int64_t)c->src_incr << c->phase_shift);
}

static int resample_flush(struct SwrContext *s) {
    AudioData *a= &s->in_buffer;
    int i, j, ret;
    if((ret = swri_realloc_audio(a, s->in_buffer_index + 2*s->in_buffer_count)) < 0)
        return ret;
    av_assert0(a->planar);
    for(i=0; i<a->ch_count; i++){
        for(j=0; j<s->in_buffer_count; j++){
            memcpy(a->ch[i] + (s->in_buffer_index+s->in_buffer_count+j  )*a->bps,
                a->ch[i] + (s->in_buffer_index+s->in_buffer_count-j-1)*a->bps, a->bps);
        }
    }
    s->in_buffer_count += (s->in_buffer_count+1)/2;
    return 0;
}

// in fact the whole handle multiple ridiculously small buffers might need more thinking...
static int invert_initial_buffer(ResampleContext *c, AudioData *dst, const AudioData *src,
                                 int in_count, int *out_idx, int *out_sz)
{
    int n, ch, num = FFMIN(in_count + *out_sz, c->filter_length + 1), res;

    if (c->index >= 0)
        return 0;

    if ((res = swri_realloc_audio(dst, c->filter_length * 2 + 1)) < 0)
        return res;

    // copy
    for (n = *out_sz; n < num; n++) {
        for (ch = 0; ch < src->ch_count; ch++) {
            memcpy(dst->ch[ch] + ((c->filter_length + n) * c->felem_size),
                   src->ch[ch] + ((n - *out_sz) * c->felem_size), c->felem_size);
        }
    }

    // if not enough data is in, return and wait for more
    if (num < c->filter_length + 1) {
        *out_sz = num;
        *out_idx = c->filter_length;
        return INT_MAX;
    }

    // else invert
    for (n = 1; n <= c->filter_length; n++) {
        for (ch = 0; ch < src->ch_count; ch++) {
            memcpy(dst->ch[ch] + ((c->filter_length - n) * c->felem_size),
                   dst->ch[ch] + ((c->filter_length + n) * c->felem_size),
                   c->felem_size);
        }
    }

    res = num - *out_sz;
    *out_idx = c->filter_length + (c->index >> c->phase_shift);
    *out_sz = FFMAX(*out_sz + c->filter_length,
                    1 + c->filter_length * 2) - *out_idx;
    c->index &= c->phase_mask;

    return FFMAX(res, 0);
}

struct Resampler const swri_resampler={
  resample_init,
  resample_free,
  multiple_resample,
  resample_flush,
  set_compensation,
  get_delay,
  invert_initial_buffer,
};
