/*
 * audio resampling
 * Copyright (c) 2004-2012 Michael Niedermayer <michaelni@gmx.at>
 * bessel function: Copyright (c) 2006 Xiaogang Zhang
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
#include "libavutil/mem.h"
#include "resample.h"

/**
 * builds a polyphase filterbank.
 * @param factor resampling factor
 * @param scale wanted sum of coefficients for each filter
 * @param filter_type  filter type
 * @param kaiser_beta  kaiser window beta
 * @return 0 on success, negative on error
 */
static int build_filter(ResampleContext *c, void *filter, double factor, int tap_count, int alloc, int phase_count, int scale,
                        int filter_type, double kaiser_beta){
    int ph, i;
    int ph_nb = phase_count % 2 ? phase_count : phase_count / 2 + 1;
    double x, y, w, t, s;
    double *tab = av_malloc_array(tap_count+1,  sizeof(*tab));
    double *sin_lut = av_malloc_array(ph_nb, sizeof(*sin_lut));
    const int center= (tap_count-1)/2;
    double norm = 0;
    int ret = AVERROR(ENOMEM);

    if (!tab || !sin_lut)
        goto fail;

    av_assert0(tap_count == 1 || tap_count % 2 == 0);

    /* if upsampling, only need to interpolate, no filter */
    if (factor > 1.0)
        factor = 1.0;

    if (factor == 1.0) {
        for (ph = 0; ph < ph_nb; ph++)
            sin_lut[ph] = sin(M_PI * ph / phase_count) * (center & 1 ? 1 : -1);
    }
    for(ph = 0; ph < ph_nb; ph++) {
        s = sin_lut[ph];
        for(i=0;i<tap_count;i++) {
            x = M_PI * ((double)(i - center) - (double)ph / phase_count) * factor;
            if (x == 0) y = 1.0;
            else if (factor == 1.0)
                y = s / x;
            else
                y = sin(x) / x;
            switch(filter_type){
            case SWR_FILTER_TYPE_CUBIC:{
                const float d= -0.5; //first order derivative = -0.5
                x = fabs(((double)(i - center) - (double)ph / phase_count) * factor);
                if(x<1.0) y= 1 - 3*x*x + 2*x*x*x + d*(            -x*x + x*x*x);
                else      y=                       d*(-4 + 8*x - 5*x*x + x*x*x);
                break;}
            case SWR_FILTER_TYPE_BLACKMAN_NUTTALL:
                w = 2.0*x / (factor*tap_count);
                t = -cos(w);
                y *= 0.3635819 - 0.4891775 * t + 0.1365995 * (2*t*t-1) - 0.0106411 * (4*t*t*t - 3*t);
                break;
            case SWR_FILTER_TYPE_KAISER:
                w = 2.0*x / (factor*tap_count*M_PI);
                y *= av_bessel_i0(kaiser_beta*sqrt(FFMAX(1-w*w, 0)));
                break;
            default:
                av_assert0(0);
            }

            tab[i] = y;
            s = -s;
            if (!ph)
                norm += y;
        }

        /* normalize so that an uniform color remains the same */
        switch(c->format){
        case AV_SAMPLE_FMT_S16P:
            for(i=0;i<tap_count;i++)
                ((int16_t*)filter)[ph * alloc + i] = av_clip_int16(lrintf(tab[i] * scale / norm));
            if (phase_count % 2) break;
            for (i = 0; i < tap_count; i++)
                ((int16_t*)filter)[(phase_count-ph) * alloc + tap_count-1-i] = ((int16_t*)filter)[ph * alloc + i];
            break;
        case AV_SAMPLE_FMT_S32P:
            for(i=0;i<tap_count;i++)
                ((int32_t*)filter)[ph * alloc + i] = av_clipl_int32(llrint(tab[i] * scale / norm));
            if (phase_count % 2) break;
            for (i = 0; i < tap_count; i++)
                ((int32_t*)filter)[(phase_count-ph) * alloc + tap_count-1-i] = ((int32_t*)filter)[ph * alloc + i];
            break;
        case AV_SAMPLE_FMT_FLTP:
            for(i=0;i<tap_count;i++)
                ((float*)filter)[ph * alloc + i] = tab[i] * scale / norm;
            if (phase_count % 2) break;
            for (i = 0; i < tap_count; i++)
                ((float*)filter)[(phase_count-ph) * alloc + tap_count-1-i] = ((float*)filter)[ph * alloc + i];
            break;
        case AV_SAMPLE_FMT_DBLP:
            for(i=0;i<tap_count;i++)
                ((double*)filter)[ph * alloc + i] = tab[i] * scale / norm;
            if (phase_count % 2) break;
            for (i = 0; i < tap_count; i++)
                ((double*)filter)[(phase_count-ph) * alloc + tap_count-1-i] = ((double*)filter)[ph * alloc + i];
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

    ret = 0;
fail:
    av_free(tab);
    av_free(sin_lut);
    return ret;
}

static void resample_free(ResampleContext **cc){
    ResampleContext *c = *cc;
    if(!c)
        return;
    av_freep(&c->filter_bank);
    av_freep(cc);
}

static ResampleContext *resample_init(ResampleContext *c, int out_rate, int in_rate, int filter_size, int phase_shift, int linear,
                                    double cutoff0, enum AVSampleFormat format, enum SwrFilterType filter_type, double kaiser_beta,
                                    double precision, int cheby, int exact_rational)
{
    double cutoff = cutoff0? cutoff0 : 0.97;
    double factor= FFMIN(out_rate * cutoff / in_rate, 1.0);
    int phase_count= 1<<phase_shift;
    int phase_count_compensation = phase_count;
    int filter_length = FFMAX((int)ceil(filter_size/factor), 1);

    if (filter_length > 1)
        filter_length = FFALIGN(filter_length, 2);

    if (exact_rational) {
        int phase_count_exact, phase_count_exact_den;

        av_reduce(&phase_count_exact, &phase_count_exact_den, out_rate, in_rate, INT_MAX);
        if (phase_count_exact <= phase_count) {
            phase_count_compensation = phase_count_exact * (phase_count / phase_count_exact);
            phase_count = phase_count_exact;
        }
    }

    if (!c || c->phase_count != phase_count || c->linear!=linear || c->factor != factor
           || c->filter_length != filter_length || c->format != format
           || c->filter_type != filter_type || c->kaiser_beta != kaiser_beta) {
        resample_free(&c);
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

        c->phase_count   = phase_count;
        c->linear        = linear;
        c->factor        = factor;
        c->filter_length = filter_length;
        c->filter_alloc  = FFALIGN(c->filter_length, 8);
        c->filter_bank   = av_calloc(c->filter_alloc, (phase_count+1)*c->felem_size);
        c->filter_type   = filter_type;
        c->kaiser_beta   = kaiser_beta;
        c->phase_count_compensation = phase_count_compensation;
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
    while (c->dst_incr < (1<<20) && c->src_incr < (1<<20)) {
        c->dst_incr *= 2;
        c->src_incr *= 2;
    }
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

static int rebuild_filter_bank_with_compensation(ResampleContext *c)
{
    uint8_t *new_filter_bank;
    int new_src_incr, new_dst_incr;
    int phase_count = c->phase_count_compensation;
    int ret;

    if (phase_count == c->phase_count)
        return 0;

    av_assert0(!c->frac && !c->dst_incr_mod);

    new_filter_bank = av_calloc(c->filter_alloc, (phase_count + 1) * c->felem_size);
    if (!new_filter_bank)
        return AVERROR(ENOMEM);

    ret = build_filter(c, new_filter_bank, c->factor, c->filter_length, c->filter_alloc,
                       phase_count, 1 << c->filter_shift, c->filter_type, c->kaiser_beta);
    if (ret < 0) {
        av_freep(&new_filter_bank);
        return ret;
    }
    memcpy(new_filter_bank + (c->filter_alloc*phase_count+1)*c->felem_size, new_filter_bank, (c->filter_alloc-1)*c->felem_size);
    memcpy(new_filter_bank + (c->filter_alloc*phase_count  )*c->felem_size, new_filter_bank + (c->filter_alloc - 1)*c->felem_size, c->felem_size);

    if (!av_reduce(&new_src_incr, &new_dst_incr, c->src_incr,
                   c->dst_incr * (int64_t)(phase_count/c->phase_count), INT32_MAX/2))
    {
        av_freep(&new_filter_bank);
        return AVERROR(EINVAL);
    }

    c->src_incr = new_src_incr;
    c->dst_incr = new_dst_incr;
    while (c->dst_incr < (1<<20) && c->src_incr < (1<<20)) {
        c->dst_incr *= 2;
        c->src_incr *= 2;
    }
    c->ideal_dst_incr = c->dst_incr;
    c->dst_incr_div   = c->dst_incr / c->src_incr;
    c->dst_incr_mod   = c->dst_incr % c->src_incr;
    c->index         *= phase_count / c->phase_count;
    c->phase_count    = phase_count;
    av_freep(&c->filter_bank);
    c->filter_bank = new_filter_bank;
    return 0;
}

static int set_compensation(ResampleContext *c, int sample_delta, int compensation_distance){
    int ret;

    if (compensation_distance && sample_delta) {
        ret = rebuild_filter_bank_with_compensation(c);
        if (ret < 0)
            return ret;
    }

    c->compensation_distance= compensation_distance;
    if (compensation_distance)
        c->dst_incr = c->ideal_dst_incr - c->ideal_dst_incr * (int64_t)sample_delta / compensation_distance;
    else
        c->dst_incr = c->ideal_dst_incr;

    c->dst_incr_div   = c->dst_incr / c->src_incr;
    c->dst_incr_mod   = c->dst_incr % c->src_incr;

    return 0;
}

static int multiple_resample(ResampleContext *c, AudioData *dst, int dst_size, AudioData *src, int src_size, int *consumed){
    int i;
    int64_t max_src_size = (INT64_MAX/2 / c->phase_count) / c->src_incr;

    if (c->compensation_distance)
        dst_size = FFMIN(dst_size, c->compensation_distance);
    src_size = FFMIN(src_size, max_src_size);

    *consumed = 0;

    if (c->filter_length == 1 && c->phase_count == 1) {
        int64_t index2= (1LL<<32)*c->frac/c->src_incr + (1LL<<32)*c->index + 1;
        int64_t incr= (1LL<<32) * c->dst_incr / c->src_incr + 1;
        int new_size = (src_size * (int64_t)c->src_incr - c->frac + c->dst_incr - 1) / c->dst_incr;

        dst_size = FFMAX(FFMIN(dst_size, new_size), 0);
        if (dst_size > 0) {
            for (i = 0; i < dst->ch_count; i++) {
                c->dsp.resample_one(dst->ch[i], src->ch[i], dst_size, index2, incr);
                if (i+1 == dst->ch_count) {
                    c->index += dst_size * c->dst_incr_div;
                    c->index += (c->frac + dst_size * (int64_t)c->dst_incr_mod) / c->src_incr;
                    av_assert2(c->index >= 0);
                    *consumed = c->index;
                    c->frac   = (c->frac + dst_size * (int64_t)c->dst_incr_mod) % c->src_incr;
                    c->index = 0;
                }
            }
        }
    } else {
        int64_t end_index = (1LL + src_size - c->filter_length) * c->phase_count;
        int64_t delta_frac = (end_index - c->index) * c->src_incr - c->frac;
        int delta_n = (delta_frac + c->dst_incr - 1) / c->dst_incr;
        int (*resample_func)(struct ResampleContext *c, void *dst,
                             const void *src, int n, int update_ctx);

        dst_size = FFMAX(FFMIN(dst_size, delta_n), 0);
        if (dst_size > 0) {
            /* resample_linear and resample_common should have same behavior
             * when frac and dst_incr_mod are zero */
            resample_func = (c->linear && (c->frac || c->dst_incr_mod)) ?
                            c->dsp.resample_linear : c->dsp.resample_common;
            for (i = 0; i < dst->ch_count; i++)
                *consumed = resample_func(c, dst->ch[i], src->ch[i], dst_size, i+1 == dst->ch_count);
        }
    }

    if (c->compensation_distance) {
        c->compensation_distance -= dst_size;
        if (!c->compensation_distance) {
            c->dst_incr     = c->ideal_dst_incr;
            c->dst_incr_div = c->dst_incr / c->src_incr;
            c->dst_incr_mod = c->dst_incr % c->src_incr;
        }
    }

    return dst_size;
}

static int64_t get_delay(struct SwrContext *s, int64_t base){
    ResampleContext *c = s->resample;
    int64_t num = s->in_buffer_count - (c->filter_length-1)/2;
    num *= c->phase_count;
    num -= c->index;
    num *= c->src_incr;
    num -= c->frac;
    return av_rescale(num, base, s->in_sample_rate*(int64_t)c->src_incr * c->phase_count);
}

static int64_t get_out_samples(struct SwrContext *s, int in_samples) {
    ResampleContext *c = s->resample;
    // The + 2 are added to allow implementations to be slightly inaccurate, they should not be needed currently.
    // They also make it easier to proof that changes and optimizations do not
    // break the upper bound.
    int64_t num = s->in_buffer_count + 2LL + in_samples;
    num *= c->phase_count;
    num -= c->index;
    num = av_rescale_rnd(num, s->out_sample_rate, ((int64_t)s->in_sample_rate) * c->phase_count, AV_ROUND_UP) + 2;

    if (c->compensation_distance) {
        if (num > INT_MAX)
            return AVERROR(EINVAL);

        num = FFMAX(num, (num * c->ideal_dst_incr - 1) / c->dst_incr + 1);
    }
    return num;
}

static int resample_flush(struct SwrContext *s) {
    ResampleContext *c = s->resample;
    AudioData *a= &s->in_buffer;
    int i, j, ret;
    int reflection = (FFMIN(s->in_buffer_count, c->filter_length) + 1) / 2;

    if((ret = swri_realloc_audio(a, s->in_buffer_index + s->in_buffer_count + reflection)) < 0)
        return ret;
    av_assert0(a->planar);
    for(i=0; i<a->ch_count; i++){
        for(j=0; j<reflection; j++){
            memcpy(a->ch[i] + (s->in_buffer_index+s->in_buffer_count+j  )*a->bps,
                a->ch[i] + (s->in_buffer_index+s->in_buffer_count-j-1)*a->bps, a->bps);
        }
    }
    s->in_buffer_count += reflection;
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
    *out_idx = c->filter_length;
    while (c->index < 0) {
        --*out_idx;
        c->index += c->phase_count;
    }
    *out_sz = FFMAX(*out_sz + c->filter_length,
                    1 + c->filter_length * 2) - *out_idx;

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
  get_out_samples,
};
