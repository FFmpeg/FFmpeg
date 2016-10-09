/*
 * Copyright (c) 2016 Muhammad Faiz <mfcc64@gmail.com>
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

#include "libavutil/opt.h"
#include "libavutil/eval.h"
#include "libavutil/avassert.h"
#include "libavcodec/avfft.h"
#include "avfilter.h"
#include "internal.h"
#include "audio.h"

#define RDFT_BITS_MIN 4
#define RDFT_BITS_MAX 16

enum WindowFunc {
    WFUNC_MIN,
    WFUNC_RECTANGULAR = WFUNC_MIN,
    WFUNC_HANN,
    WFUNC_HAMMING,
    WFUNC_BLACKMAN,
    WFUNC_NUTTALL3,
    WFUNC_MNUTTALL3,
    WFUNC_NUTTALL,
    WFUNC_BNUTTALL,
    WFUNC_BHARRIS,
    WFUNC_MAX = WFUNC_BHARRIS
};

#define NB_GAIN_ENTRY_MAX 4096
typedef struct {
    double  freq;
    double  gain;
} GainEntry;

typedef struct {
    int buf_idx;
    int overlap_idx;
} OverlapIndex;

typedef struct {
    const AVClass *class;

    RDFTContext   *analysis_irdft;
    RDFTContext   *rdft;
    RDFTContext   *irdft;
    int           analysis_rdft_len;
    int           rdft_len;

    float         *analysis_buf;
    float         *kernel_tmp_buf;
    float         *kernel_buf;
    float         *conv_buf;
    OverlapIndex  *conv_idx;
    int           fir_len;
    int           nsamples_max;
    int64_t       next_pts;
    int           frame_nsamples_max;
    int           remaining;

    char          *gain_cmd;
    char          *gain_entry_cmd;
    const char    *gain;
    const char    *gain_entry;
    double        delay;
    double        accuracy;
    int           wfunc;
    int           fixed;
    int           multi;
    int           zero_phase;

    int           nb_gain_entry;
    int           gain_entry_err;
    GainEntry     gain_entry_tbl[NB_GAIN_ENTRY_MAX];
} FIREqualizerContext;

#define OFFSET(x) offsetof(FIREqualizerContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption firequalizer_options[] = {
    { "gain", "set gain curve", OFFSET(gain), AV_OPT_TYPE_STRING, { .str = "gain_interpolate(f)" }, 0, 0, FLAGS },
    { "gain_entry", "set gain entry", OFFSET(gain_entry), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS },
    { "delay", "set delay", OFFSET(delay), AV_OPT_TYPE_DOUBLE, { .dbl = 0.01 }, 0.0, 1e10, FLAGS },
    { "accuracy", "set accuracy", OFFSET(accuracy), AV_OPT_TYPE_DOUBLE, { .dbl = 5.0 }, 0.0, 1e10, FLAGS },
    { "wfunc", "set window function", OFFSET(wfunc), AV_OPT_TYPE_INT, { .i64 = WFUNC_HANN }, WFUNC_MIN, WFUNC_MAX, FLAGS, "wfunc" },
        { "rectangular", "rectangular window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_RECTANGULAR }, 0, 0, FLAGS, "wfunc" },
        { "hann", "hann window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_HANN }, 0, 0, FLAGS, "wfunc" },
        { "hamming", "hamming window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_HAMMING }, 0, 0, FLAGS, "wfunc" },
        { "blackman", "blackman window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_BLACKMAN }, 0, 0, FLAGS, "wfunc" },
        { "nuttall3", "3-term nuttall window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_NUTTALL3 }, 0, 0, FLAGS, "wfunc" },
        { "mnuttall3", "minimum 3-term nuttall window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_MNUTTALL3 }, 0, 0, FLAGS, "wfunc" },
        { "nuttall", "nuttall window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_NUTTALL }, 0, 0, FLAGS, "wfunc" },
        { "bnuttall", "blackman-nuttall window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_BNUTTALL }, 0, 0, FLAGS, "wfunc" },
        { "bharris", "blackman-harris window", 0, AV_OPT_TYPE_CONST, { .i64 = WFUNC_BHARRIS }, 0, 0, FLAGS, "wfunc" },
    { "fixed", "set fixed frame samples", OFFSET(fixed), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "multi", "set multi channels mode", OFFSET(multi), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "zero_phase", "set zero phase mode", OFFSET(zero_phase), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(firequalizer);

static void common_uninit(FIREqualizerContext *s)
{
    av_rdft_end(s->analysis_irdft);
    av_rdft_end(s->rdft);
    av_rdft_end(s->irdft);
    s->analysis_irdft = s->rdft = s->irdft = NULL;

    av_freep(&s->analysis_buf);
    av_freep(&s->kernel_tmp_buf);
    av_freep(&s->kernel_buf);
    av_freep(&s->conv_buf);
    av_freep(&s->conv_idx);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FIREqualizerContext *s = ctx->priv;

    common_uninit(s);
    av_freep(&s->gain_cmd);
    av_freep(&s->gain_entry_cmd);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterChannelLayouts *layouts;
    AVFilterFormats *formats;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_NONE
    };
    int ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_samplerates(ctx, formats);
}

static void fast_convolute(FIREqualizerContext *s, const float *kernel_buf, float *conv_buf,
                           OverlapIndex *idx, float *data, int nsamples)
{
    if (nsamples <= s->nsamples_max) {
        float *buf = conv_buf + idx->buf_idx * s->rdft_len;
        float *obuf = conv_buf + !idx->buf_idx * s->rdft_len + idx->overlap_idx;
        int k;

        memcpy(buf, data, nsamples * sizeof(*data));
        memset(buf + nsamples, 0, (s->rdft_len - nsamples) * sizeof(*data));
        av_rdft_calc(s->rdft, buf);

        buf[0] *= kernel_buf[0];
        buf[1] *= kernel_buf[1];
        for (k = 2; k < s->rdft_len; k += 2) {
            float re, im;
            re = buf[k] * kernel_buf[k] - buf[k+1] * kernel_buf[k+1];
            im = buf[k] * kernel_buf[k+1] + buf[k+1] * kernel_buf[k];
            buf[k] = re;
            buf[k+1] = im;
        }

        av_rdft_calc(s->irdft, buf);
        for (k = 0; k < s->rdft_len - idx->overlap_idx; k++)
            buf[k] += obuf[k];
        memcpy(data, buf, nsamples * sizeof(*data));
        idx->buf_idx = !idx->buf_idx;
        idx->overlap_idx = nsamples;
    } else {
        while (nsamples > s->nsamples_max * 2) {
            fast_convolute(s, kernel_buf, conv_buf, idx, data, s->nsamples_max);
            data += s->nsamples_max;
            nsamples -= s->nsamples_max;
        }
        fast_convolute(s, kernel_buf, conv_buf, idx, data, nsamples/2);
        fast_convolute(s, kernel_buf, conv_buf, idx, data + nsamples/2, nsamples - nsamples/2);
    }
}

static double entry_func(void *p, double freq, double gain)
{
    AVFilterContext *ctx = p;
    FIREqualizerContext *s = ctx->priv;

    if (s->nb_gain_entry >= NB_GAIN_ENTRY_MAX) {
        av_log(ctx, AV_LOG_ERROR, "entry table overflow.\n");
        s->gain_entry_err = AVERROR(EINVAL);
        return 0;
    }

    if (isnan(freq)) {
        av_log(ctx, AV_LOG_ERROR, "nan frequency (%g, %g).\n", freq, gain);
        s->gain_entry_err = AVERROR(EINVAL);
        return 0;
    }

    if (s->nb_gain_entry > 0 && freq <= s->gain_entry_tbl[s->nb_gain_entry - 1].freq) {
        av_log(ctx, AV_LOG_ERROR, "unsorted frequency (%g, %g).\n", freq, gain);
        s->gain_entry_err = AVERROR(EINVAL);
        return 0;
    }

    s->gain_entry_tbl[s->nb_gain_entry].freq = freq;
    s->gain_entry_tbl[s->nb_gain_entry].gain = gain;
    s->nb_gain_entry++;
    return 0;
}

static int gain_entry_compare(const void *key, const void *memb)
{
    const double *freq = key;
    const GainEntry *entry = memb;

    if (*freq < entry[0].freq)
        return -1;
    if (*freq > entry[1].freq)
        return 1;
    return 0;
}

static double gain_interpolate_func(void *p, double freq)
{
    AVFilterContext *ctx = p;
    FIREqualizerContext *s = ctx->priv;
    GainEntry *res;
    double d0, d1, d;

    if (isnan(freq))
        return freq;

    if (!s->nb_gain_entry)
        return 0;

    if (freq <= s->gain_entry_tbl[0].freq)
        return s->gain_entry_tbl[0].gain;

    if (freq >= s->gain_entry_tbl[s->nb_gain_entry-1].freq)
        return s->gain_entry_tbl[s->nb_gain_entry-1].gain;

    res = bsearch(&freq, &s->gain_entry_tbl, s->nb_gain_entry - 1, sizeof(*res), gain_entry_compare);
    av_assert0(res);

    d  = res[1].freq - res[0].freq;
    d0 = freq - res[0].freq;
    d1 = res[1].freq - freq;

    if (d0 && d1)
        return (d0 * res[1].gain + d1 * res[0].gain) / d;

    if (d0)
        return res[1].gain;

    return res[0].gain;
}

static const char *const var_names[] = {
    "f",
    "sr",
    "ch",
    "chid",
    "chs",
    "chlayout",
    NULL
};

enum VarOffset {
    VAR_F,
    VAR_SR,
    VAR_CH,
    VAR_CHID,
    VAR_CHS,
    VAR_CHLAYOUT,
    VAR_NB
};

static int generate_kernel(AVFilterContext *ctx, const char *gain, const char *gain_entry)
{
    FIREqualizerContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const char *gain_entry_func_names[] = { "entry", NULL };
    const char *gain_func_names[] = { "gain_interpolate", NULL };
    double (*gain_entry_funcs[])(void *, double, double) = { entry_func, NULL };
    double (*gain_funcs[])(void *, double) = { gain_interpolate_func, NULL };
    double vars[VAR_NB];
    AVExpr *gain_expr;
    int ret, k, center, ch;

    s->nb_gain_entry = 0;
    s->gain_entry_err = 0;
    if (gain_entry) {
        double result = 0.0;
        ret = av_expr_parse_and_eval(&result, gain_entry, NULL, NULL, NULL, NULL,
                                     gain_entry_func_names, gain_entry_funcs, ctx, 0, ctx);
        if (ret < 0)
            return ret;
        if (s->gain_entry_err < 0)
            return s->gain_entry_err;
    }

    av_log(ctx, AV_LOG_DEBUG, "nb_gain_entry = %d.\n", s->nb_gain_entry);

    ret = av_expr_parse(&gain_expr, gain, var_names,
                        gain_func_names, gain_funcs, NULL, NULL, 0, ctx);
    if (ret < 0)
        return ret;

    vars[VAR_CHS] = inlink->channels;
    vars[VAR_CHLAYOUT] = inlink->channel_layout;
    vars[VAR_SR] = inlink->sample_rate;
    for (ch = 0; ch < inlink->channels; ch++) {
        vars[VAR_CH] = ch;
        vars[VAR_CHID] = av_channel_layout_extract_channel(inlink->channel_layout, ch);
        vars[VAR_F] = 0.0;
        s->analysis_buf[0] = pow(10.0, 0.05 * av_expr_eval(gain_expr, vars, ctx));
        vars[VAR_F] = 0.5 * inlink->sample_rate;
        s->analysis_buf[1] = pow(10.0, 0.05 * av_expr_eval(gain_expr, vars, ctx));

        for (k = 1; k < s->analysis_rdft_len/2; k++) {
            vars[VAR_F] = k * ((double)inlink->sample_rate /(double)s->analysis_rdft_len);
            s->analysis_buf[2*k] = pow(10.0, 0.05 * av_expr_eval(gain_expr, vars, ctx));
            s->analysis_buf[2*k+1] = 0.0;
        }

        av_rdft_calc(s->analysis_irdft, s->analysis_buf);
        center = s->fir_len / 2;

        for (k = 0; k <= center; k++) {
            double u = k * (M_PI/center);
            double win;
            switch (s->wfunc) {
            case WFUNC_RECTANGULAR:
                win = 1.0;
                break;
            case WFUNC_HANN:
                win = 0.5 + 0.5 * cos(u);
                break;
            case WFUNC_HAMMING:
                win = 0.53836 + 0.46164 * cos(u);
                break;
            case WFUNC_BLACKMAN:
                win = 0.42 + 0.5 * cos(u) + 0.08 * cos(2*u);
                break;
            case WFUNC_NUTTALL3:
                win = 0.40897 + 0.5 * cos(u) + 0.09103 * cos(2*u);
                break;
            case WFUNC_MNUTTALL3:
                win = 0.4243801 + 0.4973406 * cos(u) + 0.0782793 * cos(2*u);
                break;
            case WFUNC_NUTTALL:
                win = 0.355768 + 0.487396 * cos(u) + 0.144232 * cos(2*u) + 0.012604 * cos(3*u);
                break;
            case WFUNC_BNUTTALL:
                win = 0.3635819 + 0.4891775 * cos(u) + 0.1365995 * cos(2*u) + 0.0106411 * cos(3*u);
                break;
            case WFUNC_BHARRIS:
                win = 0.35875 + 0.48829 * cos(u) + 0.14128 * cos(2*u) + 0.01168 * cos(3*u);
                break;
            default:
                av_assert0(0);
            }
            s->analysis_buf[k] *= (2.0/s->analysis_rdft_len) * (2.0/s->rdft_len) * win;
        }

        for (k = 0; k < center - k; k++) {
            float tmp = s->analysis_buf[k];
            s->analysis_buf[k] = s->analysis_buf[center - k];
            s->analysis_buf[center - k] = tmp;
        }

        for (k = 1; k <= center; k++)
            s->analysis_buf[center + k] = s->analysis_buf[center - k];

        memset(s->analysis_buf + s->fir_len, 0, (s->rdft_len - s->fir_len) * sizeof(*s->analysis_buf));
        av_rdft_calc(s->rdft, s->analysis_buf);

        for (k = 0; k < s->rdft_len; k++) {
            if (isnan(s->analysis_buf[k]) || isinf(s->analysis_buf[k])) {
                av_log(ctx, AV_LOG_ERROR, "filter kernel contains nan or infinity.\n");
                av_expr_free(gain_expr);
                return AVERROR(EINVAL);
            }
        }

        memcpy(s->kernel_tmp_buf + ch * s->rdft_len, s->analysis_buf, s->rdft_len * sizeof(*s->analysis_buf));
        if (!s->multi)
            break;
    }

    memcpy(s->kernel_buf, s->kernel_tmp_buf, (s->multi ? inlink->channels : 1) * s->rdft_len * sizeof(*s->kernel_buf));
    av_expr_free(gain_expr);
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    FIREqualizerContext *s = ctx->priv;
    int rdft_bits;

    common_uninit(s);

    s->next_pts = 0;
    s->frame_nsamples_max = 0;

    s->fir_len = FFMAX(2 * (int)(inlink->sample_rate * s->delay) + 1, 3);
    s->remaining = s->fir_len - 1;

    for (rdft_bits = RDFT_BITS_MIN; rdft_bits <= RDFT_BITS_MAX; rdft_bits++) {
        s->rdft_len = 1 << rdft_bits;
        s->nsamples_max = s->rdft_len - s->fir_len + 1;
        if (s->nsamples_max * 2 >= s->fir_len)
            break;
    }

    if (rdft_bits > RDFT_BITS_MAX) {
        av_log(ctx, AV_LOG_ERROR, "too large delay, please decrease it.\n");
        return AVERROR(EINVAL);
    }

    if (!(s->rdft = av_rdft_init(rdft_bits, DFT_R2C)) || !(s->irdft = av_rdft_init(rdft_bits, IDFT_C2R)))
        return AVERROR(ENOMEM);

    for ( ; rdft_bits <= RDFT_BITS_MAX; rdft_bits++) {
        s->analysis_rdft_len = 1 << rdft_bits;
        if (inlink->sample_rate <= s->accuracy * s->analysis_rdft_len)
            break;
    }

    if (rdft_bits > RDFT_BITS_MAX) {
        av_log(ctx, AV_LOG_ERROR, "too small accuracy, please increase it.\n");
        return AVERROR(EINVAL);
    }

    if (!(s->analysis_irdft = av_rdft_init(rdft_bits, IDFT_C2R)))
        return AVERROR(ENOMEM);

    s->analysis_buf = av_malloc_array(s->analysis_rdft_len, sizeof(*s->analysis_buf));
    s->kernel_tmp_buf = av_malloc_array(s->rdft_len * (s->multi ? inlink->channels : 1), sizeof(*s->kernel_tmp_buf));
    s->kernel_buf = av_malloc_array(s->rdft_len * (s->multi ? inlink->channels : 1), sizeof(*s->kernel_buf));
    s->conv_buf   = av_calloc(2 * s->rdft_len * inlink->channels, sizeof(*s->conv_buf));
    s->conv_idx   = av_calloc(inlink->channels, sizeof(*s->conv_idx));
    if (!s->analysis_buf || !s->kernel_tmp_buf || !s->kernel_buf || !s->conv_buf || !s->conv_idx)
        return AVERROR(ENOMEM);

    av_log(ctx, AV_LOG_DEBUG, "sample_rate = %d, channels = %d, analysis_rdft_len = %d, rdft_len = %d, fir_len = %d, nsamples_max = %d.\n",
           inlink->sample_rate, inlink->channels, s->analysis_rdft_len, s->rdft_len, s->fir_len, s->nsamples_max);

    if (s->fixed)
        inlink->min_samples = inlink->max_samples = inlink->partial_buf_size = s->nsamples_max;

    return generate_kernel(ctx, s->gain_cmd ? s->gain_cmd : s->gain,
                           s->gain_entry_cmd ? s->gain_entry_cmd : s->gain_entry);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    FIREqualizerContext *s = ctx->priv;
    int ch;

    for (ch = 0; ch < inlink->channels; ch++) {
        fast_convolute(s, s->kernel_buf + (s->multi ? ch * s->rdft_len : 0),
                       s->conv_buf + 2 * ch * s->rdft_len, s->conv_idx + ch,
                       (float *) frame->extended_data[ch], frame->nb_samples);
    }

    s->next_pts = AV_NOPTS_VALUE;
    if (frame->pts != AV_NOPTS_VALUE) {
        s->next_pts = frame->pts + av_rescale_q(frame->nb_samples, av_make_q(1, inlink->sample_rate), inlink->time_base);
        if (s->zero_phase)
            frame->pts -= av_rescale_q(s->fir_len/2, av_make_q(1, inlink->sample_rate), inlink->time_base);
    }
    s->frame_nsamples_max = FFMAX(s->frame_nsamples_max, frame->nb_samples);
    return ff_filter_frame(ctx->outputs[0], frame);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    FIREqualizerContext *s= ctx->priv;
    int ret;

    ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF && s->remaining > 0 && s->frame_nsamples_max > 0) {
        AVFrame *frame = ff_get_audio_buffer(outlink, FFMIN(s->remaining, s->frame_nsamples_max));

        if (!frame)
            return AVERROR(ENOMEM);

        av_samples_set_silence(frame->extended_data, 0, frame->nb_samples, outlink->channels, frame->format);
        frame->pts = s->next_pts;
        s->remaining -= frame->nb_samples;
        ret = filter_frame(ctx->inputs[0], frame);
    }

    return ret;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    FIREqualizerContext *s = ctx->priv;
    int ret = AVERROR(ENOSYS);

    if (!strcmp(cmd, "gain")) {
        char *gain_cmd;

        gain_cmd = av_strdup(args);
        if (!gain_cmd)
            return AVERROR(ENOMEM);

        ret = generate_kernel(ctx, gain_cmd, s->gain_entry_cmd ? s->gain_entry_cmd : s->gain_entry);
        if (ret >= 0) {
            av_freep(&s->gain_cmd);
            s->gain_cmd = gain_cmd;
        } else {
            av_freep(&gain_cmd);
        }
    } else if (!strcmp(cmd, "gain_entry")) {
        char *gain_entry_cmd;

        gain_entry_cmd = av_strdup(args);
        if (!gain_entry_cmd)
            return AVERROR(ENOMEM);

        ret = generate_kernel(ctx, s->gain_cmd ? s->gain_cmd : s->gain, gain_entry_cmd);
        if (ret >= 0) {
            av_freep(&s->gain_entry_cmd);
            s->gain_entry_cmd = gain_entry_cmd;
        } else {
            av_freep(&gain_entry_cmd);
        }
    }

    return ret;
}

static const AVFilterPad firequalizer_inputs[] = {
    {
        .name           = "default",
        .config_props   = config_input,
        .filter_frame   = filter_frame,
        .type           = AVMEDIA_TYPE_AUDIO,
        .needs_writable = 1,
    },
    { NULL }
};

static const AVFilterPad firequalizer_outputs[] = {
    {
        .name           = "default",
        .request_frame  = request_frame,
        .type           = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_firequalizer = {
    .name               = "firequalizer",
    .description        = NULL_IF_CONFIG_SMALL("Finite Impulse Response Equalizer."),
    .uninit             = uninit,
    .query_formats      = query_formats,
    .process_command    = process_command,
    .priv_size          = sizeof(FIREqualizerContext),
    .inputs             = firequalizer_inputs,
    .outputs            = firequalizer_outputs,
    .priv_class         = &firequalizer_class,
};
