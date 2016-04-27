/*
 * Copyright (c) 2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/common.h"
#include "libavutil/libm.h"
#include "libavutil/log.h"
#include "internal.h"
#include "resample.h"
#include "audio_data.h"


/* double template */
#define CONFIG_RESAMPLE_DBL
#include "resample_template.c"
#undef CONFIG_RESAMPLE_DBL

/* float template */
#define CONFIG_RESAMPLE_FLT
#include "resample_template.c"
#undef CONFIG_RESAMPLE_FLT

/* s32 template */
#define CONFIG_RESAMPLE_S32
#include "resample_template.c"
#undef CONFIG_RESAMPLE_S32

/* s16 template */
#include "resample_template.c"


/* 0th order modified Bessel function of the first kind. */
static double bessel(double x)
{
    double v     = 1;
    double lastv = 0;
    double t     = 1;
    int i;

    x = x * x / 4;
    for (i = 1; v != lastv; i++) {
        lastv = v;
        t    *= x / (i * i);
        v    += t;
    }
    return v;
}

/* Build a polyphase filterbank. */
static int build_filter(ResampleContext *c, double factor)
{
    int ph, i;
    double x, y, w;
    double *tab;
    int tap_count    = c->filter_length;
    int phase_count  = 1 << c->phase_shift;
    const int center = (tap_count - 1) / 2;

    tab = av_malloc(tap_count * sizeof(*tab));
    if (!tab)
        return AVERROR(ENOMEM);

    for (ph = 0; ph < phase_count; ph++) {
        double norm = 0;
        for (i = 0; i < tap_count; i++) {
            x = M_PI * ((double)(i - center) - (double)ph / phase_count) * factor;
            if (x == 0) y = 1.0;
            else        y = sin(x) / x;
            switch (c->filter_type) {
            case AV_RESAMPLE_FILTER_TYPE_CUBIC: {
                const float d = -0.5; //first order derivative = -0.5
                x = fabs(((double)(i - center) - (double)ph / phase_count) * factor);
                if (x < 1.0) y = 1 - 3 * x*x + 2 * x*x*x + d * (                -x*x + x*x*x);
                else         y =                           d * (-4 + 8 * x - 5 * x*x + x*x*x);
                break;
            }
            case AV_RESAMPLE_FILTER_TYPE_BLACKMAN_NUTTALL:
                w  = 2.0 * x / (factor * tap_count) + M_PI;
                y *= 0.3635819 - 0.4891775 * cos(    w) +
                                 0.1365995 * cos(2 * w) -
                                 0.0106411 * cos(3 * w);
                break;
            case AV_RESAMPLE_FILTER_TYPE_KAISER:
                w  = 2.0 * x / (factor * tap_count * M_PI);
                y *= bessel(c->kaiser_beta * sqrt(FFMAX(1 - w * w, 0)));
                break;
            }

            tab[i] = y;
            norm  += y;
        }
        /* normalize so that an uniform color remains the same */
        for (i = 0; i < tap_count; i++)
            tab[i] = tab[i] / norm;

        c->set_filter(c->filter_bank, tab, ph, tap_count);
    }

    av_free(tab);
    return 0;
}

ResampleContext *ff_audio_resample_init(AVAudioResampleContext *avr)
{
    ResampleContext *c;
    int out_rate    = avr->out_sample_rate;
    int in_rate     = avr->in_sample_rate;
    double factor   = FFMIN(out_rate * avr->cutoff / in_rate, 1.0);
    int phase_count = 1 << avr->phase_shift;
    int felem_size;

    if (avr->internal_sample_fmt != AV_SAMPLE_FMT_S16P &&
        avr->internal_sample_fmt != AV_SAMPLE_FMT_S32P &&
        avr->internal_sample_fmt != AV_SAMPLE_FMT_FLTP &&
        avr->internal_sample_fmt != AV_SAMPLE_FMT_DBLP) {
        av_log(avr, AV_LOG_ERROR, "Unsupported internal format for "
               "resampling: %s\n",
               av_get_sample_fmt_name(avr->internal_sample_fmt));
        return NULL;
    }
    c = av_mallocz(sizeof(*c));
    if (!c)
        return NULL;

    c->avr           = avr;
    c->phase_shift   = avr->phase_shift;
    c->phase_mask    = phase_count - 1;
    c->linear        = avr->linear_interp;
    c->filter_length = FFMAX((int)ceil(avr->filter_size / factor), 1);
    c->filter_type   = avr->filter_type;
    c->kaiser_beta   = avr->kaiser_beta;

    switch (avr->internal_sample_fmt) {
    case AV_SAMPLE_FMT_DBLP:
        c->resample_one  = c->linear ? resample_linear_dbl : resample_one_dbl;
        c->resample_nearest = resample_nearest_dbl;
        c->set_filter    = set_filter_dbl;
        break;
    case AV_SAMPLE_FMT_FLTP:
        c->resample_one  = c->linear ? resample_linear_flt : resample_one_flt;
        c->resample_nearest = resample_nearest_flt;
        c->set_filter    = set_filter_flt;
        break;
    case AV_SAMPLE_FMT_S32P:
        c->resample_one  = c->linear ? resample_linear_s32 : resample_one_s32;
        c->resample_nearest = resample_nearest_s32;
        c->set_filter    = set_filter_s32;
        break;
    case AV_SAMPLE_FMT_S16P:
        c->resample_one  = c->linear ? resample_linear_s16 : resample_one_s16;
        c->resample_nearest = resample_nearest_s16;
        c->set_filter    = set_filter_s16;
        break;
    }

    if (ARCH_AARCH64)
        ff_audio_resample_init_aarch64(c, avr->internal_sample_fmt);
    if (ARCH_ARM)
        ff_audio_resample_init_arm(c, avr->internal_sample_fmt);

    felem_size = av_get_bytes_per_sample(avr->internal_sample_fmt);
    c->filter_bank = av_mallocz(c->filter_length * (phase_count + 1) * felem_size);
    if (!c->filter_bank)
        goto error;

    if (build_filter(c, factor) < 0)
        goto error;

    memcpy(&c->filter_bank[(c->filter_length * phase_count + 1) * felem_size],
           c->filter_bank, (c->filter_length - 1) * felem_size);
    memcpy(&c->filter_bank[c->filter_length * phase_count * felem_size],
           &c->filter_bank[(c->filter_length - 1) * felem_size], felem_size);

    c->compensation_distance = 0;
    if (!av_reduce(&c->src_incr, &c->dst_incr, out_rate,
                   in_rate * (int64_t)phase_count, INT32_MAX / 2))
        goto error;
    c->ideal_dst_incr = c->dst_incr;

    c->padding_size   = (c->filter_length - 1) / 2;
    c->initial_padding_filled = 0;
    c->index = 0;
    c->frac  = 0;

    /* allocate internal buffer */
    c->buffer = ff_audio_data_alloc(avr->resample_channels, c->padding_size,
                                    avr->internal_sample_fmt,
                                    "resample buffer");
    if (!c->buffer)
        goto error;
    c->buffer->nb_samples      = c->padding_size;
    c->initial_padding_samples = c->padding_size;

    av_log(avr, AV_LOG_DEBUG, "resample: %s from %d Hz to %d Hz\n",
           av_get_sample_fmt_name(avr->internal_sample_fmt),
           avr->in_sample_rate, avr->out_sample_rate);

    return c;

error:
    ff_audio_data_free(&c->buffer);
    av_free(c->filter_bank);
    av_free(c);
    return NULL;
}

void ff_audio_resample_free(ResampleContext **c)
{
    if (!*c)
        return;
    ff_audio_data_free(&(*c)->buffer);
    av_free((*c)->filter_bank);
    av_freep(c);
}

int avresample_set_compensation(AVAudioResampleContext *avr, int sample_delta,
                                int compensation_distance)
{
    ResampleContext *c;

    if (compensation_distance < 0)
        return AVERROR(EINVAL);
    if (!compensation_distance && sample_delta)
        return AVERROR(EINVAL);

    if (!avr->resample_needed) {
        av_log(avr, AV_LOG_ERROR, "Unable to set resampling compensation\n");
        return AVERROR(EINVAL);
    }
    c = avr->resample;
    c->compensation_distance = compensation_distance;
    if (compensation_distance) {
        c->dst_incr = c->ideal_dst_incr - c->ideal_dst_incr *
                      (int64_t)sample_delta / compensation_distance;
    } else {
        c->dst_incr = c->ideal_dst_incr;
    }

    return 0;
}

static int resample(ResampleContext *c, void *dst, const void *src,
                    int *consumed, int src_size, int dst_size, int update_ctx,
                    int nearest_neighbour)
{
    int dst_index;
    unsigned int index = c->index;
    int frac          = c->frac;
    int dst_incr_frac = c->dst_incr % c->src_incr;
    int dst_incr      = c->dst_incr / c->src_incr;
    int compensation_distance = c->compensation_distance;

    if (!dst != !src)
        return AVERROR(EINVAL);

    if (nearest_neighbour) {
        uint64_t index2 = ((uint64_t)index) << 32;
        int64_t incr   = (1LL << 32) * c->dst_incr / c->src_incr;
        dst_size       = FFMIN(dst_size,
                               (src_size-1-index) * (int64_t)c->src_incr /
                               c->dst_incr);

        if (dst) {
            for(dst_index = 0; dst_index < dst_size; dst_index++) {
                c->resample_nearest(dst, dst_index, src, index2 >> 32);
                index2 += incr;
            }
        } else {
            dst_index = dst_size;
        }
        index += dst_index * dst_incr;
        index += (frac + dst_index * (int64_t)dst_incr_frac) / c->src_incr;
        frac   = (frac + dst_index * (int64_t)dst_incr_frac) % c->src_incr;
    } else {
        for (dst_index = 0; dst_index < dst_size; dst_index++) {
            int sample_index = index >> c->phase_shift;

            if (sample_index + c->filter_length > src_size)
                break;

            if (dst)
                c->resample_one(c, dst, dst_index, src, index, frac);

            frac  += dst_incr_frac;
            index += dst_incr;
            if (frac >= c->src_incr) {
                frac -= c->src_incr;
                index++;
            }
            if (dst_index + 1 == compensation_distance) {
                compensation_distance = 0;
                dst_incr_frac = c->ideal_dst_incr % c->src_incr;
                dst_incr      = c->ideal_dst_incr / c->src_incr;
            }
        }
    }
    if (consumed)
        *consumed = index >> c->phase_shift;

    if (update_ctx) {
        index &= c->phase_mask;

        if (compensation_distance) {
            compensation_distance -= dst_index;
            if (compensation_distance <= 0)
                return AVERROR_BUG;
        }
        c->frac     = frac;
        c->index    = index;
        c->dst_incr = dst_incr_frac + c->src_incr*dst_incr;
        c->compensation_distance = compensation_distance;
    }

    return dst_index;
}

int ff_audio_resample(ResampleContext *c, AudioData *dst, AudioData *src)
{
    int ch, in_samples, in_leftover, consumed = 0, out_samples = 0;
    int ret = AVERROR(EINVAL);
    int nearest_neighbour = (c->compensation_distance == 0 &&
                             c->filter_length == 1 &&
                             c->phase_shift == 0);

    in_samples  = src ? src->nb_samples : 0;
    in_leftover = c->buffer->nb_samples;

    /* add input samples to the internal buffer */
    if (src) {
        ret = ff_audio_data_combine(c->buffer, in_leftover, src, 0, in_samples);
        if (ret < 0)
            return ret;
    } else if (in_leftover <= c->final_padding_samples) {
        /* no remaining samples to flush */
        return 0;
    }

    if (!c->initial_padding_filled) {
        int bps = av_get_bytes_per_sample(c->avr->internal_sample_fmt);
        int i;

        if (src && c->buffer->nb_samples < 2 * c->padding_size)
            return 0;

        for (i = 0; i < c->padding_size; i++)
            for (ch = 0; ch < c->buffer->channels; ch++) {
                if (c->buffer->nb_samples > 2 * c->padding_size - i) {
                    memcpy(c->buffer->data[ch] + bps * i,
                           c->buffer->data[ch] + bps * (2 * c->padding_size - i), bps);
                } else {
                    memset(c->buffer->data[ch] + bps * i, 0, bps);
                }
            }
        c->initial_padding_filled = 1;
    }

    if (!src && !c->final_padding_filled) {
        int bps = av_get_bytes_per_sample(c->avr->internal_sample_fmt);
        int i;

        ret = ff_audio_data_realloc(c->buffer,
                                    FFMAX(in_samples, in_leftover) +
                                    c->padding_size);
        if (ret < 0) {
            av_log(c->avr, AV_LOG_ERROR, "Error reallocating resampling buffer\n");
            return AVERROR(ENOMEM);
        }

        for (i = 0; i < c->padding_size; i++)
            for (ch = 0; ch < c->buffer->channels; ch++) {
                if (in_leftover > i) {
                    memcpy(c->buffer->data[ch] + bps * (in_leftover + i),
                           c->buffer->data[ch] + bps * (in_leftover - i - 1),
                           bps);
                } else {
                    memset(c->buffer->data[ch] + bps * (in_leftover + i),
                           0, bps);
                }
            }
        c->buffer->nb_samples   += c->padding_size;
        c->final_padding_samples = c->padding_size;
        c->final_padding_filled  = 1;
    }


    /* calculate output size and reallocate output buffer if needed */
    /* TODO: try to calculate this without the dummy resample() run */
    if (!dst->read_only && dst->allow_realloc) {
        out_samples = resample(c, NULL, NULL, NULL, c->buffer->nb_samples,
                               INT_MAX, 0, nearest_neighbour);
        ret = ff_audio_data_realloc(dst, out_samples);
        if (ret < 0) {
            av_log(c->avr, AV_LOG_ERROR, "error reallocating output\n");
            return ret;
        }
    }

    /* resample each channel plane */
    for (ch = 0; ch < c->buffer->channels; ch++) {
        out_samples = resample(c, (void *)dst->data[ch],
                               (const void *)c->buffer->data[ch], &consumed,
                               c->buffer->nb_samples, dst->allocated_samples,
                               ch + 1 == c->buffer->channels, nearest_neighbour);
    }
    if (out_samples < 0) {
        av_log(c->avr, AV_LOG_ERROR, "error during resampling\n");
        return out_samples;
    }

    /* drain consumed samples from the internal buffer */
    ff_audio_data_drain(c->buffer, consumed);
    c->initial_padding_samples = FFMAX(c->initial_padding_samples - consumed, 0);

    av_log(c->avr, AV_LOG_TRACE, "resampled %d in + %d leftover to %d out + %d leftover\n",
            in_samples, in_leftover, out_samples, c->buffer->nb_samples);

    dst->nb_samples = out_samples;
    return 0;
}

int avresample_get_delay(AVAudioResampleContext *avr)
{
    ResampleContext *c = avr->resample;

    if (!avr->resample_needed || !avr->resample)
        return 0;

    return FFMAX(c->buffer->nb_samples - c->padding_size, 0);
}
