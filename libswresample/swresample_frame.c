/*
 * Copyright (c) 2014 Luca Barbato <lu_zero@gentoo.org>
 * Copyright (c) 2014 Michael Niedermayer <michaelni@gmx.at>
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

#include "swresample_internal.h"
#include "libavutil/frame.h"
#include "libavutil/opt.h"

int swr_config_frame(SwrContext *s, const AVFrame *out, const AVFrame *in)
{
    swr_close(s);

    if (in) {
        if (av_opt_set_int(s, "icl", in->channel_layout, 0) < 0)
            goto fail;
        if (av_opt_set_int(s, "isf", in->format, 0) < 0)
            goto fail;
        if (av_opt_set_int(s, "isr", in->sample_rate, 0) < 0)
            goto fail;
    }

    if (out) {
        if (av_opt_set_int(s, "ocl", out->channel_layout, 0) < 0)
            goto fail;
        if (av_opt_set_int(s, "osf", out->format,  0) < 0)
            goto fail;
        if (av_opt_set_int(s, "osr", out->sample_rate, 0) < 0)
            goto fail;
    }

    return 0;
fail:
    av_log(s, AV_LOG_ERROR, "Failed to set option\n");
    return AVERROR(EINVAL);
}

static int config_changed(SwrContext *s,
                          const AVFrame *out, const AVFrame *in)
{
    int ret = 0;

    if (in) {
        if (s->in_ch_layout   != in->channel_layout ||
            s->in_sample_rate != in->sample_rate ||
            s->in_sample_fmt  != in->format) {
            ret |= AVERROR_INPUT_CHANGED;
        }
    }

    if (out) {
        if (s->out_ch_layout   != out->channel_layout ||
            s->out_sample_rate != out->sample_rate ||
            s->out_sample_fmt  != out->format) {
            ret |= AVERROR_OUTPUT_CHANGED;
        }
    }

    return ret;
}

static inline int convert_frame(SwrContext *s,
                                AVFrame *out, const AVFrame *in)
{
    int ret;
    uint8_t **out_data = NULL;
    const uint8_t **in_data = NULL;
    int out_nb_samples = 0, in_nb_samples = 0;

    if (out) {
        out_data       = out->extended_data;
        out_nb_samples = out->nb_samples;
    }

    if (in) {
        in_data       = (const uint8_t **)in->extended_data;
        in_nb_samples = in->nb_samples;
    }

    ret = swr_convert(s, out_data, out_nb_samples, in_data, in_nb_samples);

    if (ret < 0) {
        if (out)
            out->nb_samples = 0;
        return ret;
    }

    if (out)
        out->nb_samples = ret;

    return 0;
}

static inline int available_samples(AVFrame *out)
{
    int bytes_per_sample = av_get_bytes_per_sample(out->format);
    int samples = out->linesize[0] / bytes_per_sample;

    if (av_sample_fmt_is_planar(out->format)) {
        return samples;
    } else {
        int channels = av_get_channel_layout_nb_channels(out->channel_layout);
        return samples / channels;
    }
}

int swr_convert_frame(SwrContext *s,
                      AVFrame *out, const AVFrame *in)
{
    int ret, setup = 0;

    if (!swr_is_initialized(s)) {
        if ((ret = swr_config_frame(s, out, in)) < 0)
            return ret;
        if ((ret = swr_init(s)) < 0)
            return ret;
        setup = 1;
    } else {
        // return as is or reconfigure for input changes?
        if ((ret = config_changed(s, out, in)))
            return ret;
    }

    if (out) {
        if (!out->linesize[0]) {
            out->nb_samples =   swr_get_delay(s, s->out_sample_rate)
                              + in->nb_samples*(int64_t)s->out_sample_rate / s->in_sample_rate
                              + 3;
            if ((ret = av_frame_get_buffer(out, 0)) < 0) {
                if (setup)
                    swr_close(s);
                return ret;
            }
        } else {
            if (!out->nb_samples)
                out->nb_samples = available_samples(out);
        }
    }

    return convert_frame(s, out, in);
}

