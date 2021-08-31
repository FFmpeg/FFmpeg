/*
 * Copyright (c) 2016 Kyle Swanson <k@ylo.ph>.
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

/* http://k.ylo.ph/2016/04/04/loudnorm.html */

#include "libavutil/opt.h"
#include "avfilter.h"
#include "internal.h"
#include "audio.h"
#include "ebur128.h"

enum FrameType {
    FIRST_FRAME,
    INNER_FRAME,
    FINAL_FRAME,
    LINEAR_MODE,
    FRAME_NB
};

enum LimiterState {
    OUT,
    ATTACK,
    SUSTAIN,
    RELEASE,
    STATE_NB
};

enum PrintFormat {
    NONE,
    JSON,
    SUMMARY,
    PF_NB
};

typedef struct LoudNormContext {
    const AVClass *class;
    double target_i;
    double target_lra;
    double target_tp;
    double measured_i;
    double measured_lra;
    double measured_tp;
    double measured_thresh;
    double offset;
    int linear;
    int dual_mono;
    enum PrintFormat print_format;

    double *buf;
    int buf_size;
    int buf_index;
    int prev_buf_index;

    double delta[30];
    double weights[21];
    double prev_delta;
    int index;

    double gain_reduction[2];
    double *limiter_buf;
    double *prev_smp;
    int limiter_buf_index;
    int limiter_buf_size;
    enum LimiterState limiter_state;
    int peak_index;
    int env_index;
    int env_cnt;
    int attack_length;
    int release_length;

    int64_t pts;
    enum FrameType frame_type;
    int above_threshold;
    int prev_nb_samples;
    int channels;

    FFEBUR128State *r128_in;
    FFEBUR128State *r128_out;
} LoudNormContext;

#define OFFSET(x) offsetof(LoudNormContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption loudnorm_options[] = {
    { "I",                "set integrated loudness target",    OFFSET(target_i),         AV_OPT_TYPE_DOUBLE,  {.dbl = -24.},   -70.,       -5.,  FLAGS },
    { "i",                "set integrated loudness target",    OFFSET(target_i),         AV_OPT_TYPE_DOUBLE,  {.dbl = -24.},   -70.,       -5.,  FLAGS },
    { "LRA",              "set loudness range target",         OFFSET(target_lra),       AV_OPT_TYPE_DOUBLE,  {.dbl =  7.},     1.,        50.,  FLAGS },
    { "lra",              "set loudness range target",         OFFSET(target_lra),       AV_OPT_TYPE_DOUBLE,  {.dbl =  7.},     1.,        50.,  FLAGS },
    { "TP",               "set maximum true peak",             OFFSET(target_tp),        AV_OPT_TYPE_DOUBLE,  {.dbl = -2.},    -9.,         0.,  FLAGS },
    { "tp",               "set maximum true peak",             OFFSET(target_tp),        AV_OPT_TYPE_DOUBLE,  {.dbl = -2.},    -9.,         0.,  FLAGS },
    { "measured_I",       "measured IL of input file",         OFFSET(measured_i),       AV_OPT_TYPE_DOUBLE,  {.dbl =  0.},    -99.,        0.,  FLAGS },
    { "measured_i",       "measured IL of input file",         OFFSET(measured_i),       AV_OPT_TYPE_DOUBLE,  {.dbl =  0.},    -99.,        0.,  FLAGS },
    { "measured_LRA",     "measured LRA of input file",        OFFSET(measured_lra),     AV_OPT_TYPE_DOUBLE,  {.dbl =  0.},     0.,        99.,  FLAGS },
    { "measured_lra",     "measured LRA of input file",        OFFSET(measured_lra),     AV_OPT_TYPE_DOUBLE,  {.dbl =  0.},     0.,        99.,  FLAGS },
    { "measured_TP",      "measured true peak of input file",  OFFSET(measured_tp),      AV_OPT_TYPE_DOUBLE,  {.dbl =  99.},   -99.,       99.,  FLAGS },
    { "measured_tp",      "measured true peak of input file",  OFFSET(measured_tp),      AV_OPT_TYPE_DOUBLE,  {.dbl =  99.},   -99.,       99.,  FLAGS },
    { "measured_thresh",  "measured threshold of input file",  OFFSET(measured_thresh),  AV_OPT_TYPE_DOUBLE,  {.dbl = -70.},   -99.,        0.,  FLAGS },
    { "offset",           "set offset gain",                   OFFSET(offset),           AV_OPT_TYPE_DOUBLE,  {.dbl =  0.},    -99.,       99.,  FLAGS },
    { "linear",           "normalize linearly if possible",    OFFSET(linear),           AV_OPT_TYPE_BOOL,    {.i64 =  1},        0,         1,  FLAGS },
    { "dual_mono",        "treat mono input as dual-mono",     OFFSET(dual_mono),        AV_OPT_TYPE_BOOL,    {.i64 =  0},        0,         1,  FLAGS },
    { "print_format",     "set print format for stats",        OFFSET(print_format),     AV_OPT_TYPE_INT,     {.i64 =  NONE},  NONE,  PF_NB -1,  FLAGS, "print_format" },
    {     "none",         0,                                   0,                        AV_OPT_TYPE_CONST,   {.i64 =  NONE},     0,         0,  FLAGS, "print_format" },
    {     "json",         0,                                   0,                        AV_OPT_TYPE_CONST,   {.i64 =  JSON},     0,         0,  FLAGS, "print_format" },
    {     "summary",      0,                                   0,                        AV_OPT_TYPE_CONST,   {.i64 =  SUMMARY},  0,         0,  FLAGS, "print_format" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(loudnorm);

static inline int frame_size(int sample_rate, int frame_len_msec)
{
    const int frame_size = round((double)sample_rate * (frame_len_msec / 1000.0));
    return frame_size + (frame_size % 2);
}

static void init_gaussian_filter(LoudNormContext *s)
{
    double total_weight = 0.0;
    const double sigma = 3.5;
    double adjust;
    int i;

    const int offset = 21 / 2;
    const double c1 = 1.0 / (sigma * sqrt(2.0 * M_PI));
    const double c2 = 2.0 * pow(sigma, 2.0);

    for (i = 0; i < 21; i++) {
        const int x = i - offset;
        s->weights[i] = c1 * exp(-(pow(x, 2.0) / c2));
        total_weight += s->weights[i];
    }

    adjust = 1.0 / total_weight;
    for (i = 0; i < 21; i++)
        s->weights[i] *= adjust;
}

static double gaussian_filter(LoudNormContext *s, int index)
{
    double result = 0.;
    int i;

    index = index - 10 > 0 ? index - 10 : index + 20;
    for (i = 0; i < 21; i++)
        result += s->delta[((index + i) < 30) ? (index + i) : (index + i - 30)] * s->weights[i];

    return result;
}

static void detect_peak(LoudNormContext *s, int offset, int nb_samples, int channels, int *peak_delta, double *peak_value)
{
    int n, c, i, index;
    double ceiling;
    double *buf;

    *peak_delta = -1;
    buf = s->limiter_buf;
    ceiling = s->target_tp;

    index = s->limiter_buf_index + (offset * channels) + (1920 * channels);
    if (index >= s->limiter_buf_size)
        index -= s->limiter_buf_size;

    if (s->frame_type == FIRST_FRAME) {
        for (c = 0; c < channels; c++)
            s->prev_smp[c] = fabs(buf[index + c - channels]);
    }

    for (n = 0; n < nb_samples; n++) {
        for (c = 0; c < channels; c++) {
            double this, next, max_peak;

            this = fabs(buf[(index + c) < s->limiter_buf_size ? (index + c) : (index + c - s->limiter_buf_size)]);
            next = fabs(buf[(index + c + channels) < s->limiter_buf_size ? (index + c + channels) : (index + c + channels - s->limiter_buf_size)]);

            if ((s->prev_smp[c] <= this) && (next <= this) && (this > ceiling) && (n > 0)) {
                int detected;

                detected = 1;
                for (i = 2; i < 12; i++) {
                    next = fabs(buf[(index + c + (i * channels)) < s->limiter_buf_size ? (index + c + (i * channels)) : (index + c + (i * channels) - s->limiter_buf_size)]);
                    if (next > this) {
                        detected = 0;
                        break;
                    }
                }

                if (!detected)
                    continue;

                for (c = 0; c < channels; c++) {
                    if (c == 0 || fabs(buf[index + c]) > max_peak)
                        max_peak = fabs(buf[index + c]);

                    s->prev_smp[c] = fabs(buf[(index + c) < s->limiter_buf_size ? (index + c) : (index + c - s->limiter_buf_size)]);
                }

                *peak_delta = n;
                s->peak_index = index;
                *peak_value = max_peak;
                return;
            }

            s->prev_smp[c] = this;
        }

        index += channels;
        if (index >= s->limiter_buf_size)
            index -= s->limiter_buf_size;
    }
}

static void true_peak_limiter(LoudNormContext *s, double *out, int nb_samples, int channels)
{
    int n, c, index, peak_delta, smp_cnt;
    double ceiling, peak_value;
    double *buf;

    buf = s->limiter_buf;
    ceiling = s->target_tp;
    index = s->limiter_buf_index;
    smp_cnt = 0;

    if (s->frame_type == FIRST_FRAME) {
        double max;

        max = 0.;
        for (n = 0; n < 1920; n++) {
            for (c = 0; c < channels; c++) {
              max = fabs(buf[c]) > max ? fabs(buf[c]) : max;
            }
            buf += channels;
        }

        if (max > ceiling) {
            s->gain_reduction[1] = ceiling / max;
            s->limiter_state = SUSTAIN;
            buf = s->limiter_buf;

            for (n = 0; n < 1920; n++) {
                for (c = 0; c < channels; c++) {
                    double env;
                    env = s->gain_reduction[1];
                    buf[c] *= env;
                }
                buf += channels;
            }
        }

        buf = s->limiter_buf;
    }

    do {

        switch(s->limiter_state) {
        case OUT:
            detect_peak(s, smp_cnt, nb_samples - smp_cnt, channels, &peak_delta, &peak_value);
            if (peak_delta != -1) {
                s->env_cnt = 0;
                smp_cnt += (peak_delta - s->attack_length);
                s->gain_reduction[0] = 1.;
                s->gain_reduction[1] = ceiling / peak_value;
                s->limiter_state = ATTACK;

                s->env_index = s->peak_index - (s->attack_length * channels);
                if (s->env_index < 0)
                    s->env_index += s->limiter_buf_size;

                s->env_index += (s->env_cnt * channels);
                if (s->env_index > s->limiter_buf_size)
                    s->env_index -= s->limiter_buf_size;

            } else {
                smp_cnt = nb_samples;
            }
            break;

        case ATTACK:
            for (; s->env_cnt < s->attack_length; s->env_cnt++) {
                for (c = 0; c < channels; c++) {
                    double env;
                    env = s->gain_reduction[0] - ((double) s->env_cnt / (s->attack_length - 1) * (s->gain_reduction[0] - s->gain_reduction[1]));
                    buf[s->env_index + c] *= env;
                }

                s->env_index += channels;
                if (s->env_index >= s->limiter_buf_size)
                    s->env_index -= s->limiter_buf_size;

                smp_cnt++;
                if (smp_cnt >= nb_samples) {
                    s->env_cnt++;
                    break;
                }
            }

            if (smp_cnt < nb_samples) {
                s->env_cnt = 0;
                s->attack_length = 1920;
                s->limiter_state = SUSTAIN;
            }
            break;

        case SUSTAIN:
            detect_peak(s, smp_cnt, nb_samples, channels, &peak_delta, &peak_value);
            if (peak_delta == -1) {
                s->limiter_state = RELEASE;
                s->gain_reduction[0] = s->gain_reduction[1];
                s->gain_reduction[1] = 1.;
                s->env_cnt = 0;
                break;
            } else {
                double gain_reduction;
                gain_reduction = ceiling / peak_value;

                if (gain_reduction < s->gain_reduction[1]) {
                    s->limiter_state = ATTACK;

                    s->attack_length = peak_delta;
                    if (s->attack_length <= 1)
                        s->attack_length =  2;

                    s->gain_reduction[0] = s->gain_reduction[1];
                    s->gain_reduction[1] = gain_reduction;
                    s->env_cnt = 0;
                    break;
                }

                for (s->env_cnt = 0; s->env_cnt < peak_delta; s->env_cnt++) {
                    for (c = 0; c < channels; c++) {
                        double env;
                        env = s->gain_reduction[1];
                        buf[s->env_index + c] *= env;
                    }

                    s->env_index += channels;
                    if (s->env_index >= s->limiter_buf_size)
                        s->env_index -= s->limiter_buf_size;

                    smp_cnt++;
                    if (smp_cnt >= nb_samples) {
                        s->env_cnt++;
                        break;
                    }
                }
            }
            break;

        case RELEASE:
            for (; s->env_cnt < s->release_length; s->env_cnt++) {
                for (c = 0; c < channels; c++) {
                    double env;
                    env = s->gain_reduction[0] + (((double) s->env_cnt / (s->release_length - 1)) * (s->gain_reduction[1] - s->gain_reduction[0]));
                    buf[s->env_index + c] *= env;
                }

                s->env_index += channels;
                if (s->env_index >= s->limiter_buf_size)
                    s->env_index -= s->limiter_buf_size;

                smp_cnt++;
                if (smp_cnt >= nb_samples) {
                    s->env_cnt++;
                    break;
                }
            }

            if (smp_cnt < nb_samples) {
                s->env_cnt = 0;
                s->limiter_state = OUT;
            }

            break;
        }

    } while (smp_cnt < nb_samples);

    for (n = 0; n < nb_samples; n++) {
        for (c = 0; c < channels; c++) {
            out[c] = buf[index + c];
            if (fabs(out[c]) > ceiling) {
                out[c] = ceiling * (out[c] < 0 ? -1 : 1);
            }
        }
        out += channels;
        index += channels;
        if (index >= s->limiter_buf_size)
            index -= s->limiter_buf_size;
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    LoudNormContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    const double *src;
    double *dst;
    double *buf;
    double *limiter_buf;
    int i, n, c, subframe_length, src_index;
    double gain, gain_next, env_global, env_shortterm,
    global, shortterm, lra, relative_threshold;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_audio_buffer(outlink, in->nb_samples);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    if (s->pts == AV_NOPTS_VALUE)
        s->pts = in->pts;

    out->pts = s->pts;
    src = (const double *)in->data[0];
    dst = (double *)out->data[0];
    buf = s->buf;
    limiter_buf = s->limiter_buf;

    ff_ebur128_add_frames_double(s->r128_in, src, in->nb_samples);

    if (s->frame_type == FIRST_FRAME && in->nb_samples < frame_size(inlink->sample_rate, 3000)) {
        double offset, offset_tp, true_peak;

        ff_ebur128_loudness_global(s->r128_in, &global);
        for (c = 0; c < inlink->ch_layout.nb_channels; c++) {
            double tmp;
            ff_ebur128_sample_peak(s->r128_in, c, &tmp);
            if (c == 0 || tmp > true_peak)
                true_peak = tmp;
        }

        offset    = pow(10., (s->target_i - global) / 20.);
        offset_tp = true_peak * offset;
        s->offset = offset_tp < s->target_tp ? offset : s->target_tp - true_peak;
        s->frame_type = LINEAR_MODE;
    }

    switch (s->frame_type) {
    case FIRST_FRAME:
        for (n = 0; n < in->nb_samples; n++) {
            for (c = 0; c < inlink->ch_layout.nb_channels; c++) {
                buf[s->buf_index + c] = src[c];
            }
            src += inlink->ch_layout.nb_channels;
            s->buf_index += inlink->ch_layout.nb_channels;
        }

        ff_ebur128_loudness_shortterm(s->r128_in, &shortterm);

        if (shortterm < s->measured_thresh) {
            s->above_threshold = 0;
            env_shortterm = shortterm <= -70. ? 0. : s->target_i - s->measured_i;
        } else {
            s->above_threshold = 1;
            env_shortterm = shortterm <= -70. ? 0. : s->target_i - shortterm;
        }

        for (n = 0; n < 30; n++)
            s->delta[n] = pow(10., env_shortterm / 20.);
        s->prev_delta = s->delta[s->index];

        s->buf_index =
        s->limiter_buf_index = 0;

        for (n = 0; n < (s->limiter_buf_size / inlink->ch_layout.nb_channels); n++) {
            for (c = 0; c < inlink->ch_layout.nb_channels; c++) {
                limiter_buf[s->limiter_buf_index + c] = buf[s->buf_index + c] * s->delta[s->index] * s->offset;
            }
            s->limiter_buf_index += inlink->ch_layout.nb_channels;
            if (s->limiter_buf_index >= s->limiter_buf_size)
                s->limiter_buf_index -= s->limiter_buf_size;

            s->buf_index += inlink->ch_layout.nb_channels;
        }

        subframe_length = frame_size(inlink->sample_rate, 100);
        true_peak_limiter(s, dst, subframe_length, inlink->ch_layout.nb_channels);
        ff_ebur128_add_frames_double(s->r128_out, dst, subframe_length);

        s->pts +=
        out->nb_samples =
        inlink->min_samples =
        inlink->max_samples = subframe_length;

        s->frame_type = INNER_FRAME;
        break;

    case INNER_FRAME:
        gain      = gaussian_filter(s, s->index + 10 < 30 ? s->index + 10 : s->index + 10 - 30);
        gain_next = gaussian_filter(s, s->index + 11 < 30 ? s->index + 11 : s->index + 11 - 30);

        for (n = 0; n < in->nb_samples; n++) {
            for (c = 0; c < inlink->ch_layout.nb_channels; c++) {
                buf[s->prev_buf_index + c] = src[c];
                limiter_buf[s->limiter_buf_index + c] = buf[s->buf_index + c] * (gain + (((double) n / in->nb_samples) * (gain_next - gain))) * s->offset;
            }
            src += inlink->ch_layout.nb_channels;

            s->limiter_buf_index += inlink->ch_layout.nb_channels;
            if (s->limiter_buf_index >= s->limiter_buf_size)
                s->limiter_buf_index -= s->limiter_buf_size;

            s->prev_buf_index += inlink->ch_layout.nb_channels;
            if (s->prev_buf_index >= s->buf_size)
                s->prev_buf_index -= s->buf_size;

            s->buf_index += inlink->ch_layout.nb_channels;
            if (s->buf_index >= s->buf_size)
                s->buf_index -= s->buf_size;
        }

        subframe_length = (frame_size(inlink->sample_rate, 100) - in->nb_samples) * inlink->ch_layout.nb_channels;
        s->limiter_buf_index = s->limiter_buf_index + subframe_length < s->limiter_buf_size ? s->limiter_buf_index + subframe_length : s->limiter_buf_index + subframe_length - s->limiter_buf_size;

        true_peak_limiter(s, dst, in->nb_samples, inlink->ch_layout.nb_channels);
        ff_ebur128_add_frames_double(s->r128_out, dst, in->nb_samples);

        ff_ebur128_loudness_range(s->r128_in, &lra);
        ff_ebur128_loudness_global(s->r128_in, &global);
        ff_ebur128_loudness_shortterm(s->r128_in, &shortterm);
        ff_ebur128_relative_threshold(s->r128_in, &relative_threshold);

        if (s->above_threshold == 0) {
            double shortterm_out;

            if (shortterm > s->measured_thresh)
                s->prev_delta *= 1.0058;

            ff_ebur128_loudness_shortterm(s->r128_out, &shortterm_out);
            if (shortterm_out >= s->target_i)
                s->above_threshold = 1;
        }

        if (shortterm < relative_threshold || shortterm <= -70. || s->above_threshold == 0) {
            s->delta[s->index] = s->prev_delta;
        } else {
            env_global = fabs(shortterm - global) < (s->target_lra / 2.) ? shortterm - global : (s->target_lra / 2.) * ((shortterm - global) < 0 ? -1 : 1);
            env_shortterm = s->target_i - shortterm;
            s->delta[s->index] = pow(10., (env_global + env_shortterm) / 20.);
        }

        s->prev_delta = s->delta[s->index];
        s->index++;
        if (s->index >= 30)
            s->index -= 30;
        s->prev_nb_samples = in->nb_samples;
        s->pts += in->nb_samples;
        break;

    case FINAL_FRAME:
        gain = gaussian_filter(s, s->index + 10 < 30 ? s->index + 10 : s->index + 10 - 30);
        s->limiter_buf_index = 0;
        src_index = 0;

        for (n = 0; n < s->limiter_buf_size / inlink->ch_layout.nb_channels; n++) {
            for (c = 0; c < inlink->ch_layout.nb_channels; c++) {
                s->limiter_buf[s->limiter_buf_index + c] = src[src_index + c] * gain * s->offset;
            }
            src_index += inlink->ch_layout.nb_channels;

            s->limiter_buf_index += inlink->ch_layout.nb_channels;
            if (s->limiter_buf_index >= s->limiter_buf_size)
                s->limiter_buf_index -= s->limiter_buf_size;
        }

        subframe_length = frame_size(inlink->sample_rate, 100);
        for (i = 0; i < in->nb_samples / subframe_length; i++) {
            true_peak_limiter(s, dst, subframe_length, inlink->ch_layout.nb_channels);

            for (n = 0; n < subframe_length; n++) {
                for (c = 0; c < inlink->ch_layout.nb_channels; c++) {
                    if (src_index < (in->nb_samples * inlink->ch_layout.nb_channels)) {
                        limiter_buf[s->limiter_buf_index + c] = src[src_index + c] * gain * s->offset;
                    } else {
                        limiter_buf[s->limiter_buf_index + c] = 0.;
                    }
                }

                if (src_index < (in->nb_samples * inlink->ch_layout.nb_channels))
                    src_index += inlink->ch_layout.nb_channels;

                s->limiter_buf_index += inlink->ch_layout.nb_channels;
                if (s->limiter_buf_index >= s->limiter_buf_size)
                    s->limiter_buf_index -= s->limiter_buf_size;
            }

            dst += (subframe_length * inlink->ch_layout.nb_channels);
        }

        dst = (double *)out->data[0];
        ff_ebur128_add_frames_double(s->r128_out, dst, in->nb_samples);
        break;

    case LINEAR_MODE:
        for (n = 0; n < in->nb_samples; n++) {
            for (c = 0; c < inlink->ch_layout.nb_channels; c++) {
                dst[c] = src[c] * s->offset;
            }
            src += inlink->ch_layout.nb_channels;
            dst += inlink->ch_layout.nb_channels;
        }

        dst = (double *)out->data[0];
        ff_ebur128_add_frames_double(s->r128_out, dst, in->nb_samples);
        s->pts += in->nb_samples;
        break;
    }

    if (in != out)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static int request_frame(AVFilterLink *outlink)
{
    int ret;
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    LoudNormContext *s = ctx->priv;

    ret = ff_request_frame(inlink);
    if (ret == AVERROR_EOF && s->frame_type == INNER_FRAME) {
        double *src;
        double *buf;
        int nb_samples, n, c, offset;
        AVFrame *frame;

        nb_samples  = (s->buf_size / inlink->ch_layout.nb_channels) - s->prev_nb_samples;
        nb_samples -= (frame_size(inlink->sample_rate, 100) - s->prev_nb_samples);

        frame = ff_get_audio_buffer(outlink, nb_samples);
        if (!frame)
            return AVERROR(ENOMEM);
        frame->nb_samples = nb_samples;

        buf = s->buf;
        src = (double *)frame->data[0];

        offset  = ((s->limiter_buf_size / inlink->ch_layout.nb_channels) - s->prev_nb_samples) * inlink->ch_layout.nb_channels;
        offset -= (frame_size(inlink->sample_rate, 100) - s->prev_nb_samples) * inlink->ch_layout.nb_channels;
        s->buf_index = s->buf_index - offset < 0 ? s->buf_index - offset + s->buf_size : s->buf_index - offset;

        for (n = 0; n < nb_samples; n++) {
            for (c = 0; c < inlink->ch_layout.nb_channels; c++) {
                src[c] = buf[s->buf_index + c];
            }
            src += inlink->ch_layout.nb_channels;
            s->buf_index += inlink->ch_layout.nb_channels;
            if (s->buf_index >= s->buf_size)
                s->buf_index -= s->buf_size;
        }

        s->frame_type = FINAL_FRAME;
        ret = filter_frame(inlink, frame);
    }
    return ret;
}

static int query_formats(AVFilterContext *ctx)
{
    LoudNormContext *s = ctx->priv;
    AVFilterFormats *formats;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    static const int input_srate[] = {192000, -1};
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBL,
        AV_SAMPLE_FMT_NONE
    };
    int ret = ff_set_common_all_channel_counts(ctx);
    if (ret < 0)
        return ret;

    ret = ff_set_common_formats_from_list(ctx, sample_fmts);
    if (ret < 0)
        return ret;

    if (s->frame_type != LINEAR_MODE) {
        formats = ff_make_format_list(input_srate);
        if (!formats)
            return AVERROR(ENOMEM);
        ret = ff_formats_ref(formats, &inlink->outcfg.samplerates);
        if (ret < 0)
            return ret;
        ret = ff_formats_ref(formats, &outlink->incfg.samplerates);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    LoudNormContext *s = ctx->priv;

    s->r128_in = ff_ebur128_init(inlink->ch_layout.nb_channels, inlink->sample_rate, 0, FF_EBUR128_MODE_I | FF_EBUR128_MODE_S | FF_EBUR128_MODE_LRA | FF_EBUR128_MODE_SAMPLE_PEAK);
    if (!s->r128_in)
        return AVERROR(ENOMEM);

    s->r128_out = ff_ebur128_init(inlink->ch_layout.nb_channels, inlink->sample_rate, 0, FF_EBUR128_MODE_I | FF_EBUR128_MODE_S | FF_EBUR128_MODE_LRA | FF_EBUR128_MODE_SAMPLE_PEAK);
    if (!s->r128_out)
        return AVERROR(ENOMEM);

    if (inlink->ch_layout.nb_channels == 1 && s->dual_mono) {
        ff_ebur128_set_channel(s->r128_in,  0, FF_EBUR128_DUAL_MONO);
        ff_ebur128_set_channel(s->r128_out, 0, FF_EBUR128_DUAL_MONO);
    }

    s->buf_size = frame_size(inlink->sample_rate, 3000) * inlink->ch_layout.nb_channels;
    s->buf = av_malloc_array(s->buf_size, sizeof(*s->buf));
    if (!s->buf)
        return AVERROR(ENOMEM);

    s->limiter_buf_size = frame_size(inlink->sample_rate, 210) * inlink->ch_layout.nb_channels;
    s->limiter_buf = av_malloc_array(s->buf_size, sizeof(*s->limiter_buf));
    if (!s->limiter_buf)
        return AVERROR(ENOMEM);

    s->prev_smp = av_malloc_array(inlink->ch_layout.nb_channels, sizeof(*s->prev_smp));
    if (!s->prev_smp)
        return AVERROR(ENOMEM);

    init_gaussian_filter(s);

    if (s->frame_type != LINEAR_MODE) {
        inlink->min_samples =
        inlink->max_samples = frame_size(inlink->sample_rate, 3000);
    }

    s->pts = AV_NOPTS_VALUE;
    s->buf_index =
    s->prev_buf_index =
    s->limiter_buf_index = 0;
    s->channels = inlink->ch_layout.nb_channels;
    s->index = 1;
    s->limiter_state = OUT;
    s->offset = pow(10., s->offset / 20.);
    s->target_tp = pow(10., s->target_tp / 20.);
    s->attack_length = frame_size(inlink->sample_rate, 10);
    s->release_length = frame_size(inlink->sample_rate, 100);

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    LoudNormContext *s = ctx->priv;
    s->frame_type = FIRST_FRAME;

    if (s->linear) {
        double offset, offset_tp;
        offset    = s->target_i - s->measured_i;
        offset_tp = s->measured_tp + offset;

        if (s->measured_tp != 99 && s->measured_thresh != -70 && s->measured_lra != 0 && s->measured_i != 0) {
            if ((offset_tp <= s->target_tp) && (s->measured_lra <= s->target_lra)) {
                s->frame_type = LINEAR_MODE;
                s->offset = offset;
            }
        }
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LoudNormContext *s = ctx->priv;
    double i_in, i_out, lra_in, lra_out, thresh_in, thresh_out, tp_in, tp_out;
    int c;

    if (!s->r128_in || !s->r128_out)
        goto end;

    ff_ebur128_loudness_range(s->r128_in, &lra_in);
    ff_ebur128_loudness_global(s->r128_in, &i_in);
    ff_ebur128_relative_threshold(s->r128_in, &thresh_in);
    for (c = 0; c < s->channels; c++) {
        double tmp;
        ff_ebur128_sample_peak(s->r128_in, c, &tmp);
        if ((c == 0) || (tmp > tp_in))
            tp_in = tmp;
    }

    ff_ebur128_loudness_range(s->r128_out, &lra_out);
    ff_ebur128_loudness_global(s->r128_out, &i_out);
    ff_ebur128_relative_threshold(s->r128_out, &thresh_out);
    for (c = 0; c < s->channels; c++) {
        double tmp;
        ff_ebur128_sample_peak(s->r128_out, c, &tmp);
        if ((c == 0) || (tmp > tp_out))
            tp_out = tmp;
    }

    switch(s->print_format) {
    case NONE:
        break;

    case JSON:
        av_log(ctx, AV_LOG_INFO,
            "\n{\n"
            "\t\"input_i\" : \"%.2f\",\n"
            "\t\"input_tp\" : \"%.2f\",\n"
            "\t\"input_lra\" : \"%.2f\",\n"
            "\t\"input_thresh\" : \"%.2f\",\n"
            "\t\"output_i\" : \"%.2f\",\n"
            "\t\"output_tp\" : \"%+.2f\",\n"
            "\t\"output_lra\" : \"%.2f\",\n"
            "\t\"output_thresh\" : \"%.2f\",\n"
            "\t\"normalization_type\" : \"%s\",\n"
            "\t\"target_offset\" : \"%.2f\"\n"
            "}\n",
            i_in,
            20. * log10(tp_in),
            lra_in,
            thresh_in,
            i_out,
            20. * log10(tp_out),
            lra_out,
            thresh_out,
            s->frame_type == LINEAR_MODE ? "linear" : "dynamic",
            s->target_i - i_out
        );
        break;

    case SUMMARY:
        av_log(ctx, AV_LOG_INFO,
            "\n"
            "Input Integrated:   %+6.1f LUFS\n"
            "Input True Peak:    %+6.1f dBTP\n"
            "Input LRA:          %6.1f LU\n"
            "Input Threshold:    %+6.1f LUFS\n"
            "\n"
            "Output Integrated:  %+6.1f LUFS\n"
            "Output True Peak:   %+6.1f dBTP\n"
            "Output LRA:         %6.1f LU\n"
            "Output Threshold:   %+6.1f LUFS\n"
            "\n"
            "Normalization Type:   %s\n"
            "Target Offset:      %+6.1f LU\n",
            i_in,
            20. * log10(tp_in),
            lra_in,
            thresh_in,
            i_out,
            20. * log10(tp_out),
            lra_out,
            thresh_out,
            s->frame_type == LINEAR_MODE ? "Linear" : "Dynamic",
            s->target_i - i_out
        );
        break;
    }

end:
    if (s->r128_in)
        ff_ebur128_destroy(&s->r128_in);
    if (s->r128_out)
        ff_ebur128_destroy(&s->r128_out);
    av_freep(&s->limiter_buf);
    av_freep(&s->prev_smp);
    av_freep(&s->buf);
}

static const AVFilterPad avfilter_af_loudnorm_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad avfilter_af_loudnorm_outputs[] = {
    {
        .name          = "default",
        .request_frame = request_frame,
        .type          = AVMEDIA_TYPE_AUDIO,
    },
};

const AVFilter ff_af_loudnorm = {
    .name          = "loudnorm",
    .description   = NULL_IF_CONFIG_SMALL("EBU R128 loudness normalization"),
    .priv_size     = sizeof(LoudNormContext),
    .priv_class    = &loudnorm_class,
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(avfilter_af_loudnorm_inputs),
    FILTER_OUTPUTS(avfilter_af_loudnorm_outputs),
    FILTER_QUERY_FUNC(query_formats),
};
