/*
 * Copyright (C) 2017 Paul B Mahol
 * Copyright (C) 2013-2015 Andreas Fuchs, Wolfgang Hrauda
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

#include <math.h>

#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/float_dsp.h"
#include "libavutil/intmath.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"

#include "avfilter.h"
#include "filters.h"
#include "internal.h"
#include "audio.h"

#define TIME_DOMAIN      0
#define FREQUENCY_DOMAIN 1

#define HRIR_STEREO 0
#define HRIR_MULTI  1

typedef struct HeadphoneContext {
    const AVClass *class;

    char *map;
    int type;

    int lfe_channel;

    int have_hrirs;
    int eof_hrirs;

    int ir_len;
    int air_len;

    int nb_hrir_inputs;

    int nb_irs;

    float gain;
    float lfe_gain, gain_lfe;

    float *ringbuffer[2];
    int write[2];

    int buffer_length;
    int n_fft;
    int size;
    int hrir_fmt;

    float *data_ir[2];
    float *temp_src[2];
    FFTComplex *temp_fft[2];
    FFTComplex *temp_afft[2];

    FFTContext *fft[2], *ifft[2];
    FFTComplex *data_hrtf[2];

    float (*scalarproduct_float)(const float *v1, const float *v2, int len);
    struct hrir_inputs {
        int          ir_len;
        int          eof;
    } hrir_in[64];
    uint64_t mapping[64];
} HeadphoneContext;

static int parse_channel_name(const char *arg, uint64_t *rchannel)
{
    uint64_t layout = av_get_channel_layout(arg);

    if (av_get_channel_layout_nb_channels(layout) != 1)
        return AVERROR(EINVAL);
    *rchannel = layout;
    return 0;
}

static void parse_map(AVFilterContext *ctx)
{
    HeadphoneContext *s = ctx->priv;
    char *arg, *tokenizer, *p;
    uint64_t used_channels = 0;

    p = s->map;
    while ((arg = av_strtok(p, "|", &tokenizer))) {
        uint64_t out_channel;

        p = NULL;
        if (parse_channel_name(arg, &out_channel)) {
            av_log(ctx, AV_LOG_WARNING, "Failed to parse \'%s\' as channel name.\n", arg);
            continue;
        }
        if (used_channels & out_channel) {
            av_log(ctx, AV_LOG_WARNING, "Ignoring duplicate channel '%s'.\n", arg);
            continue;
        }
        used_channels        |= out_channel;
        s->mapping[s->nb_irs] = out_channel;
        s->nb_irs++;
    }

    if (s->hrir_fmt == HRIR_MULTI)
        s->nb_hrir_inputs = 1;
    else
        s->nb_hrir_inputs = s->nb_irs;
}

typedef struct ThreadData {
    AVFrame *in, *out;
    int *write;
    float **ir;
    int *n_clippings;
    float **ringbuffer;
    float **temp_src;
    FFTComplex **temp_fft;
    FFTComplex **temp_afft;
} ThreadData;

static int headphone_convolute(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    HeadphoneContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in, *out = td->out;
    int offset = jobnr;
    int *write = &td->write[jobnr];
    const float *const ir = td->ir[jobnr];
    int *n_clippings = &td->n_clippings[jobnr];
    float *ringbuffer = td->ringbuffer[jobnr];
    float *temp_src = td->temp_src[jobnr];
    const int ir_len = s->ir_len;
    const int air_len = s->air_len;
    const float *src = (const float *)in->data[0];
    float *dst = (float *)out->data[0];
    const int in_channels = in->channels;
    const int buffer_length = s->buffer_length;
    const uint32_t modulo = (uint32_t)buffer_length - 1;
    float *buffer[64];
    int wr = *write;
    int read;
    int i, l;

    dst += offset;
    for (l = 0; l < in_channels; l++) {
        buffer[l] = ringbuffer + l * buffer_length;
    }

    for (i = 0; i < in->nb_samples; i++) {
        const float *cur_ir = ir;

        *dst = 0;
        for (l = 0; l < in_channels; l++) {
            *(buffer[l] + wr) = src[l];
        }

        for (l = 0; l < in_channels; cur_ir += air_len, l++) {
            const float *const bptr = buffer[l];

            if (l == s->lfe_channel) {
                *dst += *(buffer[s->lfe_channel] + wr) * s->gain_lfe;
                continue;
            }

            read = (wr - (ir_len - 1)) & modulo;

            if (read + ir_len < buffer_length) {
                memcpy(temp_src, bptr + read, ir_len * sizeof(*temp_src));
            } else {
                int len = FFMIN(air_len - (read % ir_len), buffer_length - read);

                memcpy(temp_src, bptr + read, len * sizeof(*temp_src));
                memcpy(temp_src + len, bptr, (air_len - len) * sizeof(*temp_src));
            }

            dst[0] += s->scalarproduct_float(cur_ir, temp_src, FFALIGN(ir_len, 32));
        }

        if (fabsf(dst[0]) > 1)
            n_clippings[0]++;

        dst += 2;
        src += in_channels;
        wr   = (wr + 1) & modulo;
    }

    *write = wr;

    return 0;
}

static int headphone_fast_convolute(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    HeadphoneContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in, *out = td->out;
    int offset = jobnr;
    int *write = &td->write[jobnr];
    FFTComplex *hrtf = s->data_hrtf[jobnr];
    int *n_clippings = &td->n_clippings[jobnr];
    float *ringbuffer = td->ringbuffer[jobnr];
    const int ir_len = s->ir_len;
    const float *src = (const float *)in->data[0];
    float *dst = (float *)out->data[0];
    const int in_channels = in->channels;
    const int buffer_length = s->buffer_length;
    const uint32_t modulo = (uint32_t)buffer_length - 1;
    FFTComplex *fft_in = s->temp_fft[jobnr];
    FFTComplex *fft_acc = s->temp_afft[jobnr];
    FFTContext *ifft = s->ifft[jobnr];
    FFTContext *fft = s->fft[jobnr];
    const int n_fft = s->n_fft;
    const float fft_scale = 1.0f / s->n_fft;
    FFTComplex *hrtf_offset;
    int wr = *write;
    int n_read;
    int i, j;

    dst += offset;

    n_read = FFMIN(ir_len, in->nb_samples);
    for (j = 0; j < n_read; j++) {
        dst[2 * j]     = ringbuffer[wr];
        ringbuffer[wr] = 0.0;
        wr  = (wr + 1) & modulo;
    }

    for (j = n_read; j < in->nb_samples; j++) {
        dst[2 * j] = 0;
    }

    memset(fft_acc, 0, sizeof(FFTComplex) * n_fft);

    for (i = 0; i < in_channels; i++) {
        if (i == s->lfe_channel) {
            for (j = 0; j < in->nb_samples; j++) {
                dst[2 * j] += src[i + j * in_channels] * s->gain_lfe;
            }
            continue;
        }

        offset = i * n_fft;
        hrtf_offset = hrtf + offset;

        memset(fft_in, 0, sizeof(FFTComplex) * n_fft);

        for (j = 0; j < in->nb_samples; j++) {
            fft_in[j].re = src[j * in_channels + i];
        }

        av_fft_permute(fft, fft_in);
        av_fft_calc(fft, fft_in);
        for (j = 0; j < n_fft; j++) {
            const FFTComplex *hcomplex = hrtf_offset + j;
            const float re = fft_in[j].re;
            const float im = fft_in[j].im;

            fft_acc[j].re += re * hcomplex->re - im * hcomplex->im;
            fft_acc[j].im += re * hcomplex->im + im * hcomplex->re;
        }
    }

    av_fft_permute(ifft, fft_acc);
    av_fft_calc(ifft, fft_acc);

    for (j = 0; j < in->nb_samples; j++) {
        dst[2 * j] += fft_acc[j].re * fft_scale;
        if (fabsf(dst[2 * j]) > 1)
            n_clippings[0]++;
    }

    for (j = 0; j < ir_len - 1; j++) {
        int write_pos = (wr + j) & modulo;

        *(ringbuffer + write_pos) += fft_acc[in->nb_samples + j].re * fft_scale;
    }

    *write = wr;

    return 0;
}

static int check_ir(AVFilterLink *inlink, int input_number)
{
    AVFilterContext *ctx = inlink->dst;
    HeadphoneContext *s = ctx->priv;
    int ir_len, max_ir_len;

    ir_len = ff_inlink_queued_samples(inlink);
    max_ir_len = 65536;
    if (ir_len > max_ir_len) {
        av_log(ctx, AV_LOG_ERROR, "Too big length of IRs: %d > %d.\n", ir_len, max_ir_len);
        return AVERROR(EINVAL);
    }
    s->hrir_in[input_number].ir_len = ir_len;
    s->ir_len = FFMAX(ir_len, s->ir_len);

    return 0;
}

static int headphone_frame(HeadphoneContext *s, AVFrame *in, AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    int n_clippings[2] = { 0 };
    ThreadData td;
    AVFrame *out;

    out = ff_get_audio_buffer(outlink, in->nb_samples);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    out->pts = in->pts;

    td.in = in; td.out = out; td.write = s->write;
    td.ir = s->data_ir; td.n_clippings = n_clippings;
    td.ringbuffer = s->ringbuffer; td.temp_src = s->temp_src;
    td.temp_fft = s->temp_fft;
    td.temp_afft = s->temp_afft;

    if (s->type == TIME_DOMAIN) {
        ctx->internal->execute(ctx, headphone_convolute, &td, NULL, 2);
    } else {
        ctx->internal->execute(ctx, headphone_fast_convolute, &td, NULL, 2);
    }
    emms_c();

    if (n_clippings[0] + n_clippings[1] > 0) {
        av_log(ctx, AV_LOG_WARNING, "%d of %d samples clipped. Please reduce gain.\n",
               n_clippings[0] + n_clippings[1], out->nb_samples * 2);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int convert_coeffs(AVFilterContext *ctx, AVFilterLink *inlink)
{
    struct HeadphoneContext *s = ctx->priv;
    const int ir_len = s->ir_len;
    int nb_input_channels = ctx->inputs[0]->channels;
    float gain_lin = expf((s->gain - 3 * nb_input_channels) / 20 * M_LN10);
    AVFrame *frame;
    int ret = 0;
    int n_fft;
    int i, j, k;

    s->air_len = 1 << (32 - ff_clz(ir_len));
    if (s->type == TIME_DOMAIN) {
        s->air_len = FFALIGN(s->air_len, 32);
    }
    s->buffer_length = 1 << (32 - ff_clz(s->air_len));
    s->n_fft = n_fft = 1 << (32 - ff_clz(ir_len + s->size));

    if (s->type == FREQUENCY_DOMAIN) {
        s->fft[0] = av_fft_init(av_log2(s->n_fft), 0);
        s->fft[1] = av_fft_init(av_log2(s->n_fft), 0);
        s->ifft[0] = av_fft_init(av_log2(s->n_fft), 1);
        s->ifft[1] = av_fft_init(av_log2(s->n_fft), 1);

        if (!s->fft[0] || !s->fft[1] || !s->ifft[0] || !s->ifft[1]) {
            av_log(ctx, AV_LOG_ERROR, "Unable to create FFT contexts of size %d.\n", s->n_fft);
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (s->type == TIME_DOMAIN) {
        s->ringbuffer[0] = av_calloc(s->buffer_length, sizeof(float) * nb_input_channels);
        s->ringbuffer[1] = av_calloc(s->buffer_length, sizeof(float) * nb_input_channels);
    } else {
        s->ringbuffer[0] = av_calloc(s->buffer_length, sizeof(float));
        s->ringbuffer[1] = av_calloc(s->buffer_length, sizeof(float));
        s->temp_fft[0] = av_calloc(s->n_fft, sizeof(FFTComplex));
        s->temp_fft[1] = av_calloc(s->n_fft, sizeof(FFTComplex));
        s->temp_afft[0] = av_calloc(s->n_fft, sizeof(FFTComplex));
        s->temp_afft[1] = av_calloc(s->n_fft, sizeof(FFTComplex));
        if (!s->temp_fft[0] || !s->temp_fft[1] ||
            !s->temp_afft[0] || !s->temp_afft[1]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (!s->ringbuffer[0] || !s->ringbuffer[1]) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (s->type == TIME_DOMAIN) {
        s->temp_src[0] = av_calloc(s->air_len, sizeof(float));
        s->temp_src[1] = av_calloc(s->air_len, sizeof(float));

        s->data_ir[0] = av_calloc(nb_input_channels * s->air_len, sizeof(*s->data_ir[0]));
        s->data_ir[1] = av_calloc(nb_input_channels * s->air_len, sizeof(*s->data_ir[1]));
        if (!s->data_ir[0] || !s->data_ir[1] || !s->temp_src[0] || !s->temp_src[1]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    } else {
        s->data_hrtf[0] = av_calloc(n_fft, sizeof(*s->data_hrtf[0]) * nb_input_channels);
        s->data_hrtf[1] = av_calloc(n_fft, sizeof(*s->data_hrtf[1]) * nb_input_channels);
        if (!s->data_hrtf[0] || !s->data_hrtf[1]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    for (i = 0; i < s->nb_hrir_inputs; av_frame_free(&frame), i++) {
        int len = s->hrir_in[i].ir_len;
        float *ptr;

        ret = ff_inlink_consume_samples(ctx->inputs[i + 1], len, len, &frame);
        if (ret < 0)
            goto fail;
        ptr = (float *)frame->extended_data[0];

        if (s->hrir_fmt == HRIR_STEREO) {
            int idx = av_get_channel_layout_channel_index(inlink->channel_layout,
                                                          s->mapping[i]);
            if (idx < 0)
                continue;
            if (s->type == TIME_DOMAIN) {
                float *data_ir_l = s->data_ir[0] + idx * s->air_len;
                float *data_ir_r = s->data_ir[1] + idx * s->air_len;

                for (j = 0; j < len; j++) {
                    data_ir_l[j] = ptr[len * 2 - j * 2 - 2] * gain_lin;
                    data_ir_r[j] = ptr[len * 2 - j * 2 - 1] * gain_lin;
                }
            } else {
                FFTComplex *fft_in_l = s->data_hrtf[0] + idx * n_fft;
                FFTComplex *fft_in_r = s->data_hrtf[1] + idx * n_fft;

                for (j = 0; j < len; j++) {
                    fft_in_l[j].re = ptr[j * 2    ] * gain_lin;
                    fft_in_r[j].re = ptr[j * 2 + 1] * gain_lin;
                }

                av_fft_permute(s->fft[0], fft_in_l);
                av_fft_calc(s->fft[0], fft_in_l);
                av_fft_permute(s->fft[0], fft_in_r);
                av_fft_calc(s->fft[0], fft_in_r);
            }
        } else {
            int I, N = ctx->inputs[1]->channels;

            for (k = 0; k < N / 2; k++) {
                int idx = av_get_channel_layout_channel_index(inlink->channel_layout,
                                                              s->mapping[k]);
                if (idx < 0)
                    continue;

                I = k * 2;
                if (s->type == TIME_DOMAIN) {
                    float *data_ir_l = s->data_ir[0] + idx * s->air_len;
                    float *data_ir_r = s->data_ir[1] + idx * s->air_len;

                    for (j = 0; j < len; j++) {
                        data_ir_l[j] = ptr[len * N - j * N - N + I    ] * gain_lin;
                        data_ir_r[j] = ptr[len * N - j * N - N + I + 1] * gain_lin;
                    }
                } else {
                    FFTComplex *fft_in_l = s->data_hrtf[0] + idx * n_fft;
                    FFTComplex *fft_in_r = s->data_hrtf[1] + idx * n_fft;

                    for (j = 0; j < len; j++) {
                        fft_in_l[j].re = ptr[j * N + I    ] * gain_lin;
                        fft_in_r[j].re = ptr[j * N + I + 1] * gain_lin;
                    }

                    av_fft_permute(s->fft[0], fft_in_l);
                    av_fft_calc(s->fft[0], fft_in_l);
                    av_fft_permute(s->fft[0], fft_in_r);
                    av_fft_calc(s->fft[0], fft_in_r);
                }
            }
        }
    }

    s->have_hrirs = 1;

fail:
    return ret;
}

static int activate(AVFilterContext *ctx)
{
    HeadphoneContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *in = NULL;
    int i, ret;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[0], ctx);
    if (!s->eof_hrirs) {
        int eof = 1;
        for (i = 0; i < s->nb_hrir_inputs; i++) {
            AVFilterLink *input = ctx->inputs[i + 1];

            if (s->hrir_in[i].eof)
                continue;

            if ((ret = check_ir(input, i)) < 0)
                return ret;

            if (ff_outlink_get_status(input) == AVERROR_EOF) {
                if (!ff_inlink_queued_samples(input)) {
                    av_log(ctx, AV_LOG_ERROR, "No samples provided for "
                           "HRIR stream %d.\n", i);
                    return AVERROR_INVALIDDATA;
                }
                s->hrir_in[i].eof = 1;
            } else {
                if (ff_outlink_frame_wanted(ctx->outputs[0]))
                    ff_inlink_request_frame(input);
                eof = 0;
            }
        }
        if (!eof)
            return 0;
        s->eof_hrirs = 1;

        ret = convert_coeffs(ctx, inlink);
        if (ret < 0)
            return ret;
    } else if (!s->have_hrirs)
        return AVERROR_EOF;

    if ((ret = ff_inlink_consume_samples(ctx->inputs[0], s->size, s->size, &in)) > 0) {
        ret = headphone_frame(s, in, outlink);
        if (ret < 0)
            return ret;
    }

    if (ret < 0)
        return ret;

    FF_FILTER_FORWARD_STATUS(ctx->inputs[0], ctx->outputs[0]);
    if (ff_outlink_frame_wanted(ctx->outputs[0]))
        ff_inlink_request_frame(ctx->inputs[0]);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    struct HeadphoneContext *s = ctx->priv;
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    AVFilterChannelLayouts *stereo_layout = NULL;
    AVFilterChannelLayouts *hrir_layouts = NULL;
    int ret, i;

    ret = ff_add_format(&formats, AV_SAMPLE_FMT_FLT);
    if (ret)
        return ret;
    ret = ff_set_common_formats(ctx, formats);
    if (ret)
        return ret;

    layouts = ff_all_channel_layouts();
    if (!layouts)
        return AVERROR(ENOMEM);

    ret = ff_channel_layouts_ref(layouts, &ctx->inputs[0]->outcfg.channel_layouts);
    if (ret)
        return ret;

    ret = ff_add_channel_layout(&stereo_layout, AV_CH_LAYOUT_STEREO);
    if (ret)
        return ret;
    ret = ff_channel_layouts_ref(stereo_layout, &ctx->outputs[0]->incfg.channel_layouts);
    if (ret)
        return ret;

    if (s->hrir_fmt == HRIR_MULTI) {
        hrir_layouts = ff_all_channel_counts();
        if (!hrir_layouts)
            return AVERROR(ENOMEM);
        ret = ff_channel_layouts_ref(hrir_layouts, &ctx->inputs[1]->outcfg.channel_layouts);
        if (ret)
            return ret;
    } else {
        for (i = 1; i <= s->nb_hrir_inputs; i++) {
            ret = ff_channel_layouts_ref(stereo_layout, &ctx->inputs[i]->outcfg.channel_layouts);
            if (ret)
                return ret;
        }
    }

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_samplerates(ctx, formats);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    HeadphoneContext *s = ctx->priv;

    if (s->nb_irs < inlink->channels) {
        av_log(ctx, AV_LOG_ERROR, "Number of HRIRs must be >= %d.\n", inlink->channels);
        return AVERROR(EINVAL);
    }

    s->lfe_channel = av_get_channel_layout_channel_index(inlink->channel_layout,
                                                         AV_CH_LOW_FREQUENCY);
    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    HeadphoneContext *s = ctx->priv;
    int i, ret;

    AVFilterPad pad = {
        .name         = "in0",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    };
    if ((ret = ff_insert_inpad(ctx, 0, &pad)) < 0)
        return ret;

    if (!s->map) {
        av_log(ctx, AV_LOG_ERROR, "Valid mapping must be set.\n");
        return AVERROR(EINVAL);
    }

    parse_map(ctx);

    for (i = 0; i < s->nb_hrir_inputs; i++) {
        char *name = av_asprintf("hrir%d", i);
        AVFilterPad pad = {
            .name         = name,
            .type         = AVMEDIA_TYPE_AUDIO,
        };
        if (!name)
            return AVERROR(ENOMEM);
        if ((ret = ff_insert_inpad(ctx, i + 1, &pad)) < 0) {
            av_freep(&pad.name);
            return ret;
        }
    }

    if (s->type == TIME_DOMAIN) {
        AVFloatDSPContext *fdsp = avpriv_float_dsp_alloc(0);
        if (!fdsp)
            return AVERROR(ENOMEM);
        s->scalarproduct_float = fdsp->scalarproduct_float;
        av_free(fdsp);
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    HeadphoneContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    if (s->hrir_fmt == HRIR_MULTI) {
        AVFilterLink *hrir_link = ctx->inputs[1];

        if (hrir_link->channels < inlink->channels * 2) {
            av_log(ctx, AV_LOG_ERROR, "Number of channels in HRIR stream must be >= %d.\n", inlink->channels * 2);
            return AVERROR(EINVAL);
        }
    }

    s->gain_lfe = expf((s->gain - 3 * inlink->channels + s->lfe_gain) / 20 * M_LN10);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    HeadphoneContext *s = ctx->priv;

    av_fft_end(s->ifft[0]);
    av_fft_end(s->ifft[1]);
    av_fft_end(s->fft[0]);
    av_fft_end(s->fft[1]);
    av_freep(&s->data_ir[0]);
    av_freep(&s->data_ir[1]);
    av_freep(&s->ringbuffer[0]);
    av_freep(&s->ringbuffer[1]);
    av_freep(&s->temp_src[0]);
    av_freep(&s->temp_src[1]);
    av_freep(&s->temp_fft[0]);
    av_freep(&s->temp_fft[1]);
    av_freep(&s->temp_afft[0]);
    av_freep(&s->temp_afft[1]);
    av_freep(&s->data_hrtf[0]);
    av_freep(&s->data_hrtf[1]);

    for (unsigned i = 1; i < ctx->nb_inputs; i++)
        av_freep(&ctx->input_pads[i].name);
}

#define OFFSET(x) offsetof(HeadphoneContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption headphone_options[] = {
    { "map",       "set channels convolution mappings",  OFFSET(map),      AV_OPT_TYPE_STRING, {.str=NULL},            .flags = FLAGS },
    { "gain",      "set gain in dB",                     OFFSET(gain),     AV_OPT_TYPE_FLOAT,  {.dbl=0},     -20,  40, .flags = FLAGS },
    { "lfe",       "set lfe gain in dB",                 OFFSET(lfe_gain), AV_OPT_TYPE_FLOAT,  {.dbl=0},     -20,  40, .flags = FLAGS },
    { "type",      "set processing",                     OFFSET(type),     AV_OPT_TYPE_INT,    {.i64=1},       0,   1, .flags = FLAGS, "type" },
    { "time",      "time domain",                        0,                AV_OPT_TYPE_CONST,  {.i64=0},       0,   0, .flags = FLAGS, "type" },
    { "freq",      "frequency domain",                   0,                AV_OPT_TYPE_CONST,  {.i64=1},       0,   0, .flags = FLAGS, "type" },
    { "size",      "set frame size",                     OFFSET(size),     AV_OPT_TYPE_INT,    {.i64=1024},1024,96000, .flags = FLAGS },
    { "hrir",      "set hrir format",                    OFFSET(hrir_fmt), AV_OPT_TYPE_INT,    {.i64=HRIR_STEREO}, 0, 1, .flags = FLAGS, "hrir" },
    { "stereo",    "hrir files have exactly 2 channels", 0,                AV_OPT_TYPE_CONST,  {.i64=HRIR_STEREO}, 0, 0, .flags = FLAGS, "hrir" },
    { "multich",   "single multichannel hrir file",      0,                AV_OPT_TYPE_CONST,  {.i64=HRIR_MULTI},  0, 0, .flags = FLAGS, "hrir" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(headphone);

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_af_headphone = {
    .name          = "headphone",
    .description   = NULL_IF_CONFIG_SMALL("Apply headphone binaural spatialization with HRTFs in additional streams."),
    .priv_size     = sizeof(HeadphoneContext),
    .priv_class    = &headphone_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .activate      = activate,
    .inputs        = NULL,
    .outputs       = outputs,
    .flags         = AVFILTER_FLAG_SLICE_THREADS | AVFILTER_FLAG_DYNAMIC_INPUTS,
};
