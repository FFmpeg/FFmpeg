/*
 * Copyright (c) 2017 Paul B Mahol
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
 * An arbitrary audio FIR filter
 */

#include "libavutil/audio_fifo.h"
#include "libavutil/common.h"
#include "libavutil/float_dsp.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "af_afir.h"

static void fcmul_add_c(float *sum, const float *t, const float *c, ptrdiff_t len)
{
    int n;

    for (n = 0; n < len; n++) {
        const float cre = c[2 * n    ];
        const float cim = c[2 * n + 1];
        const float tre = t[2 * n    ];
        const float tim = t[2 * n + 1];

        sum[2 * n    ] += tre * cre - tim * cim;
        sum[2 * n + 1] += tre * cim + tim * cre;
    }

    sum[2 * n] += t[2 * n] * c[2 * n];
}

static int fir_channel(AVFilterContext *ctx, void *arg, int ch, int nb_jobs)
{
    AudioFIRContext *s = ctx->priv;
    const float *src = (const float *)s->in[0]->extended_data[ch];
    int index1 = (s->index + 1) % 3;
    int index2 = (s->index + 2) % 3;
    float *sum = s->sum[ch];
    AVFrame *out = arg;
    float *block;
    float *dst;
    int n, i, j;

    memset(sum, 0, sizeof(*sum) * s->fft_length);
    block = s->block[ch] + s->part_index * s->block_size;
    memset(block, 0, sizeof(*block) * s->fft_length);

    s->fdsp->vector_fmul_scalar(block + s->part_size, src, s->dry_gain, FFALIGN(s->nb_samples, 4));
    emms_c();

    av_rdft_calc(s->rdft[ch], block);
    block[2 * s->part_size] = block[1];
    block[1] = 0;

    j = s->part_index;

    for (i = 0; i < s->nb_partitions; i++) {
        const int coffset = i * s->coeff_size;
        const FFTComplex *coeff = s->coeff[ch * !s->one2many] + coffset;

        block = s->block[ch] + j * s->block_size;
        s->fcmul_add(sum, block, (const float *)coeff, s->part_size);

        if (j == 0)
            j = s->nb_partitions;
        j--;
    }

    sum[1] = sum[2 * s->part_size];
    av_rdft_calc(s->irdft[ch], sum);

    dst = (float *)s->buffer->extended_data[ch] + index1 * s->part_size;
    for (n = 0; n < s->part_size; n++) {
        dst[n] += sum[n];
    }

    dst = (float *)s->buffer->extended_data[ch] + index2 * s->part_size;

    memcpy(dst, sum + s->part_size, s->part_size * sizeof(*dst));

    dst = (float *)s->buffer->extended_data[ch] + s->index * s->part_size;

    if (out) {
        float *ptr = (float *)out->extended_data[ch];
        s->fdsp->vector_fmul_scalar(ptr, dst, s->wet_gain, FFALIGN(out->nb_samples, 4));
        emms_c();
    }

    return 0;
}

static int fir_frame(AudioFIRContext *s, AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFrame *out = NULL;
    int ret;

    s->nb_samples = FFMIN(s->part_size, av_audio_fifo_size(s->fifo[0]));

    if (!s->want_skip) {
        out = ff_get_audio_buffer(outlink, s->nb_samples);
        if (!out)
            return AVERROR(ENOMEM);
    }

    s->in[0] = ff_get_audio_buffer(ctx->inputs[0], s->nb_samples);
    if (!s->in[0]) {
        av_frame_free(&out);
        return AVERROR(ENOMEM);
    }

    av_audio_fifo_peek(s->fifo[0], (void **)s->in[0]->extended_data, s->nb_samples);

    ctx->internal->execute(ctx, fir_channel, out, NULL, outlink->channels);

    s->part_index = (s->part_index + 1) % s->nb_partitions;

    av_audio_fifo_drain(s->fifo[0], s->nb_samples);

    if (!s->want_skip) {
        out->pts = s->pts;
        if (s->pts != AV_NOPTS_VALUE)
            s->pts += av_rescale_q(out->nb_samples, (AVRational){1, outlink->sample_rate}, outlink->time_base);
    }

    s->index++;
    if (s->index == 3)
        s->index = 0;

    av_frame_free(&s->in[0]);

    if (s->want_skip == 1) {
        s->want_skip = 0;
        ret = 0;
    } else {
        ret = ff_filter_frame(outlink, out);
    }

    return ret;
}

static int convert_coeffs(AVFilterContext *ctx)
{
    AudioFIRContext *s = ctx->priv;
    int i, ch, n, N;

    s->nb_taps = av_audio_fifo_size(s->fifo[1]);
    if (s->nb_taps <= 0)
        return AVERROR(EINVAL);

    for (n = 4; (1 << n) < s->nb_taps; n++);
    N = FFMIN(n, 16);
    s->ir_length = 1 << n;
    s->fft_length = (1 << (N + 1)) + 1;
    s->part_size = 1 << (N - 1);
    s->block_size = FFALIGN(s->fft_length, 32);
    s->coeff_size = FFALIGN(s->part_size + 1, 32);
    s->nb_partitions = (s->nb_taps + s->part_size - 1) / s->part_size;
    s->nb_coeffs = s->ir_length + s->nb_partitions;

    for (ch = 0; ch < ctx->inputs[0]->channels; ch++) {
        s->sum[ch] = av_calloc(s->fft_length, sizeof(**s->sum));
        if (!s->sum[ch])
            return AVERROR(ENOMEM);
    }

    for (ch = 0; ch < ctx->inputs[1]->channels; ch++) {
        s->coeff[ch] = av_calloc(s->nb_partitions * s->coeff_size, sizeof(**s->coeff));
        if (!s->coeff[ch])
            return AVERROR(ENOMEM);
    }

    for (ch = 0; ch < ctx->inputs[0]->channels; ch++) {
        s->block[ch] = av_calloc(s->nb_partitions * s->block_size, sizeof(**s->block));
        if (!s->block[ch])
            return AVERROR(ENOMEM);
    }

    for (ch = 0; ch < ctx->inputs[0]->channels; ch++) {
        s->rdft[ch]  = av_rdft_init(N, DFT_R2C);
        s->irdft[ch] = av_rdft_init(N, IDFT_C2R);
        if (!s->rdft[ch] || !s->irdft[ch])
            return AVERROR(ENOMEM);
    }

    s->in[1] = ff_get_audio_buffer(ctx->inputs[1], s->nb_taps);
    if (!s->in[1])
        return AVERROR(ENOMEM);

    s->buffer = ff_get_audio_buffer(ctx->inputs[0], s->part_size * 3);
    if (!s->buffer)
        return AVERROR(ENOMEM);

    av_audio_fifo_read(s->fifo[1], (void **)s->in[1]->extended_data, s->nb_taps);

    if (s->again) {
        float power = 0;

        for (ch = 0; ch < ctx->inputs[1]->channels; ch++) {
            float *time = (float *)s->in[1]->extended_data[!s->one2many * ch];

            for (i = 0; i < s->nb_taps; i++)
                power += FFABS(time[i]);
        }

        s->gain = sqrtf(1.f / (ctx->inputs[1]->channels * power)) / (sqrtf(ctx->inputs[1]->channels));
        for (ch = 0; ch < ctx->inputs[1]->channels; ch++) {
            float *time = (float *)s->in[1]->extended_data[!s->one2many * ch];

            s->fdsp->vector_fmul_scalar(time, time, s->gain, FFALIGN(s->nb_taps, 4));
        }
    }

    for (ch = 0; ch < ctx->inputs[1]->channels; ch++) {
        float *time = (float *)s->in[1]->extended_data[!s->one2many * ch];
        float *block = s->block[ch];
        FFTComplex *coeff = s->coeff[ch];

        for (i = FFMAX(1, s->length * s->nb_taps); i < s->nb_taps; i++)
            time[i] = 0;

        for (i = 0; i < s->nb_partitions; i++) {
            const float scale = 1.f / s->part_size;
            const int toffset = i * s->part_size;
            const int coffset = i * s->coeff_size;
            const int boffset = s->part_size;
            const int remaining = s->nb_taps - (i * s->part_size);
            const int size = remaining >= s->part_size ? s->part_size : remaining;

            memset(block, 0, sizeof(*block) * s->fft_length);
            memcpy(block + boffset, time + toffset, size * sizeof(*block));

            av_rdft_calc(s->rdft[0], block);

            coeff[coffset].re = block[0] * scale;
            coeff[coffset].im = 0;
            for (n = 1; n < s->part_size; n++) {
                coeff[coffset + n].re = block[2 * n] * scale;
                coeff[coffset + n].im = block[2 * n + 1] * scale;
            }
            coeff[coffset + s->part_size].re = block[1] * scale;
            coeff[coffset + s->part_size].im = 0;
        }
    }

    av_frame_free(&s->in[1]);
    av_log(ctx, AV_LOG_DEBUG, "nb_taps: %d\n", s->nb_taps);
    av_log(ctx, AV_LOG_DEBUG, "nb_partitions: %d\n", s->nb_partitions);
    av_log(ctx, AV_LOG_DEBUG, "partition size: %d\n", s->part_size);
    av_log(ctx, AV_LOG_DEBUG, "ir_length: %d\n", s->ir_length);

    s->have_coeffs = 1;

    return 0;
}

static int read_ir(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *ctx = link->dst;
    AudioFIRContext *s = ctx->priv;
    int nb_taps, max_nb_taps, ret;

    ret = av_audio_fifo_write(s->fifo[1], (void **)frame->extended_data,
                             frame->nb_samples);
    av_frame_free(&frame);
    if (ret < 0)
        return ret;

    nb_taps = av_audio_fifo_size(s->fifo[1]);
    max_nb_taps = MAX_IR_DURATION * ctx->outputs[0]->sample_rate;
    if (nb_taps > max_nb_taps) {
        av_log(ctx, AV_LOG_ERROR, "Too big number of coefficients: %d > %d.\n", nb_taps, max_nb_taps);
        return AVERROR(EINVAL);
    }

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *ctx = link->dst;
    AudioFIRContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int ret;

    ret = av_audio_fifo_write(s->fifo[0], (void **)frame->extended_data,
                              frame->nb_samples);
    if (ret > 0 && s->pts == AV_NOPTS_VALUE)
        s->pts = frame->pts;

    av_frame_free(&frame);

    if (ret < 0)
        return ret;

    if (!s->have_coeffs && s->eof_coeffs) {
        ret = convert_coeffs(ctx);
        if (ret < 0)
            return ret;
    }

    if (s->have_coeffs) {
        while (av_audio_fifo_size(s->fifo[0]) >= s->part_size) {
            ret = fir_frame(s, outlink);
            if (ret < 0)
                return ret;
        }
    }
    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioFIRContext *s = ctx->priv;
    int ret;

    if (!s->eof_coeffs) {
        ret = ff_request_frame(ctx->inputs[1]);
        if (ret == AVERROR_EOF) {
            s->eof_coeffs = 1;
            ret = 0;
        }
        return ret;
    }
    ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF && s->have_coeffs) {
        if (s->need_padding) {
            AVFrame *silence = ff_get_audio_buffer(outlink, s->part_size);

            if (!silence)
                return AVERROR(ENOMEM);
            ret = av_audio_fifo_write(s->fifo[0], (void **)silence->extended_data,
                                      silence->nb_samples);
            av_frame_free(&silence);
            if (ret < 0)
                return ret;
            s->need_padding = 0;
        }

        while (av_audio_fifo_size(s->fifo[0]) > 0) {
            ret = fir_frame(s, outlink);
            if (ret < 0)
                return ret;
        }
        ret = AVERROR_EOF;
    }
    return ret;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_NONE
    };
    int ret, i;

    layouts = ff_all_channel_counts();
    if ((ret = ff_channel_layouts_ref(layouts, &ctx->outputs[0]->in_channel_layouts)) < 0)
        return ret;

    for (i = 0; i < 2; i++) {
        layouts = ff_all_channel_counts();
        if ((ret = ff_channel_layouts_ref(layouts, &ctx->inputs[i]->out_channel_layouts)) < 0)
            return ret;
    }

    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_set_common_formats(ctx, formats)) < 0)
        return ret;

    formats = ff_all_samplerates();
    return ff_set_common_samplerates(ctx, formats);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioFIRContext *s = ctx->priv;

    if (ctx->inputs[0]->channels != ctx->inputs[1]->channels &&
        ctx->inputs[1]->channels != 1) {
        av_log(ctx, AV_LOG_ERROR,
               "Second input must have same number of channels as first input or "
               "exactly 1 channel.\n");
        return AVERROR(EINVAL);
    }

    s->one2many = ctx->inputs[1]->channels == 1;
    outlink->sample_rate = ctx->inputs[0]->sample_rate;
    outlink->time_base   = ctx->inputs[0]->time_base;
    outlink->channel_layout = ctx->inputs[0]->channel_layout;
    outlink->channels = ctx->inputs[0]->channels;

    s->fifo[0] = av_audio_fifo_alloc(ctx->inputs[0]->format, ctx->inputs[0]->channels, 1024);
    s->fifo[1] = av_audio_fifo_alloc(ctx->inputs[1]->format, ctx->inputs[1]->channels, 1024);
    if (!s->fifo[0] || !s->fifo[1])
        return AVERROR(ENOMEM);

    s->sum = av_calloc(outlink->channels, sizeof(*s->sum));
    s->coeff = av_calloc(ctx->inputs[1]->channels, sizeof(*s->coeff));
    s->block = av_calloc(ctx->inputs[0]->channels, sizeof(*s->block));
    s->rdft = av_calloc(outlink->channels, sizeof(*s->rdft));
    s->irdft = av_calloc(outlink->channels, sizeof(*s->irdft));
    if (!s->sum || !s->coeff || !s->block || !s->rdft || !s->irdft)
        return AVERROR(ENOMEM);

    s->nb_channels = outlink->channels;
    s->nb_coef_channels = ctx->inputs[1]->channels;
    s->want_skip = 1;
    s->need_padding = 1;
    s->pts = AV_NOPTS_VALUE;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioFIRContext *s = ctx->priv;
    int ch;

    if (s->sum) {
        for (ch = 0; ch < s->nb_channels; ch++) {
            av_freep(&s->sum[ch]);
        }
    }
    av_freep(&s->sum);

    if (s->coeff) {
        for (ch = 0; ch < s->nb_coef_channels; ch++) {
            av_freep(&s->coeff[ch]);
        }
    }
    av_freep(&s->coeff);

    if (s->block) {
        for (ch = 0; ch < s->nb_channels; ch++) {
            av_freep(&s->block[ch]);
        }
    }
    av_freep(&s->block);

    if (s->rdft) {
        for (ch = 0; ch < s->nb_channels; ch++) {
            av_rdft_end(s->rdft[ch]);
        }
    }
    av_freep(&s->rdft);

    if (s->irdft) {
        for (ch = 0; ch < s->nb_channels; ch++) {
            av_rdft_end(s->irdft[ch]);
        }
    }
    av_freep(&s->irdft);

    av_frame_free(&s->in[0]);
    av_frame_free(&s->in[1]);
    av_frame_free(&s->buffer);

    av_audio_fifo_free(s->fifo[0]);
    av_audio_fifo_free(s->fifo[1]);

    av_freep(&s->fdsp);
}

static av_cold int init(AVFilterContext *ctx)
{
    AudioFIRContext *s = ctx->priv;

    s->fcmul_add = fcmul_add_c;

    s->fdsp = avpriv_float_dsp_alloc(0);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    if (ARCH_X86)
        ff_afir_init_x86(s);

    return 0;
}

static const AVFilterPad afir_inputs[] = {
    {
        .name           = "main",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = filter_frame,
    },{
        .name           = "ir",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = read_ir,
    },
    { NULL }
};

static const AVFilterPad afir_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define OFFSET(x) offsetof(AudioFIRContext, x)

static const AVOption afir_options[] = {
    { "dry",    "set dry gain",     OFFSET(dry_gain), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 1, AF },
    { "wet",    "set wet gain",     OFFSET(wet_gain), AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 1, AF },
    { "length", "set IR length",    OFFSET(length),   AV_OPT_TYPE_FLOAT, {.dbl=1}, 0, 1, AF },
    { "again",  "enable auto gain", OFFSET(again),    AV_OPT_TYPE_BOOL,  {.i64=1}, 0, 1, AF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(afir);

AVFilter ff_af_afir = {
    .name          = "afir",
    .description   = NULL_IF_CONFIG_SMALL("Apply Finite Impulse Response filter with supplied coefficients in 2nd stream."),
    .priv_size     = sizeof(AudioFIRContext),
    .priv_class    = &afir_class,
    .query_formats = query_formats,
    .init          = init,
    .uninit        = uninit,
    .inputs        = afir_inputs,
    .outputs       = afir_outputs,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
