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

    int mapping[64];

    int nb_inputs;

    int nb_irs;

    float gain;
    float lfe_gain, gain_lfe;

    float *ringbuffer[2];
    int write[2];

    int buffer_length;
    int n_fft;
    int size;
    int hrir_fmt;

    int *delay[2];
    float *data_ir[2];
    float *temp_src[2];
    FFTComplex *temp_fft[2];
    FFTComplex *temp_afft[2];

    FFTContext *fft[2], *ifft[2];
    FFTComplex *data_hrtf[2];

    AVFloatDSPContext *fdsp;
    struct headphone_inputs {
        AVFrame     *frame;
        int          ir_len;
        int          delay_l;
        int          delay_r;
        int          eof;
    } *in;
} HeadphoneContext;

static int parse_channel_name(HeadphoneContext *s, int x, char **arg, int *rchannel, char *buf)
{
    int len, i, channel_id = 0;
    int64_t layout, layout0;

    if (sscanf(*arg, "%7[A-Z]%n", buf, &len)) {
        layout0 = layout = av_get_channel_layout(buf);
        if (layout == AV_CH_LOW_FREQUENCY)
            s->lfe_channel = x;
        for (i = 32; i > 0; i >>= 1) {
            if (layout >= 1LL << i) {
                channel_id += i;
                layout >>= i;
            }
        }
        if (channel_id >= 64 || layout0 != 1LL << channel_id)
            return AVERROR(EINVAL);
        *rchannel = channel_id;
        *arg += len;
        return 0;
    }
    return AVERROR(EINVAL);
}

static void parse_map(AVFilterContext *ctx)
{
    HeadphoneContext *s = ctx->priv;
    char *arg, *tokenizer, *p, *args = av_strdup(s->map);
    int i;

    if (!args)
        return;
    p = args;

    s->lfe_channel = -1;
    s->nb_inputs = 1;

    for (i = 0; i < 64; i++) {
        s->mapping[i] = -1;
    }

    while ((arg = av_strtok(p, "|", &tokenizer))) {
        int out_ch_id;
        char buf[8];

        p = NULL;
        if (parse_channel_name(s, s->nb_irs, &arg, &out_ch_id, buf)) {
            av_log(ctx, AV_LOG_WARNING, "Failed to parse \'%s\' as channel name.\n", buf);
            continue;
        }
        s->mapping[s->nb_irs] = out_ch_id;
        s->nb_irs++;
    }

    if (s->hrir_fmt == HRIR_MULTI)
        s->nb_inputs = 2;
    else
        s->nb_inputs = s->nb_irs + 1;

    av_free(args);
}

typedef struct ThreadData {
    AVFrame *in, *out;
    int *write;
    int **delay;
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
    const int *const delay = td->delay[jobnr];
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
    float *buffer[16];
    int wr = *write;
    int read;
    int i, l;

    dst += offset;
    for (l = 0; l < in_channels; l++) {
        buffer[l] = ringbuffer + l * buffer_length;
    }

    for (i = 0; i < in->nb_samples; i++) {
        const float *temp_ir = ir;

        *dst = 0;
        for (l = 0; l < in_channels; l++) {
            *(buffer[l] + wr) = src[l];
        }

        for (l = 0; l < in_channels; l++) {
            const float *const bptr = buffer[l];

            if (l == s->lfe_channel) {
                *dst += *(buffer[s->lfe_channel] + wr) * s->gain_lfe;
                temp_ir += air_len;
                continue;
            }

            read = (wr - *(delay + l) - (ir_len - 1) + buffer_length) & modulo;

            if (read + ir_len < buffer_length) {
                memcpy(temp_src, bptr + read, ir_len * sizeof(*temp_src));
            } else {
                int len = FFMIN(air_len - (read % ir_len), buffer_length - read);

                memcpy(temp_src, bptr + read, len * sizeof(*temp_src));
                memcpy(temp_src + len, bptr, (air_len - len) * sizeof(*temp_src));
            }

            dst[0] += s->fdsp->scalarproduct_float(temp_ir, temp_src, FFALIGN(ir_len, 32));
            temp_ir += air_len;
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
    }

    for (j = 0; j < ir_len - 1; j++) {
        int write_pos = (wr + j) & modulo;

        *(ringbuffer + write_pos) += fft_acc[in->nb_samples + j].re * fft_scale;
    }

    for (i = 0; i < out->nb_samples; i++) {
        if (fabsf(dst[0]) > 1) {
            n_clippings[0]++;
        }

        dst += 2;
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
    s->in[input_number].ir_len = ir_len;
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
    td.delay = s->delay; td.ir = s->data_ir; td.n_clippings = n_clippings;
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
    int nb_irs = s->nb_irs;
    int nb_input_channels = ctx->inputs[0]->channels;
    float gain_lin = expf((s->gain - 3 * nb_input_channels) / 20 * M_LN10);
    FFTComplex *data_hrtf_l = NULL;
    FFTComplex *data_hrtf_r = NULL;
    FFTComplex *fft_in_l = NULL;
    FFTComplex *fft_in_r = NULL;
    float *data_ir_l = NULL;
    float *data_ir_r = NULL;
    int offset = 0, ret = 0;
    int n_fft;
    int i, j, k;

    s->air_len = 1 << (32 - ff_clz(ir_len));
    s->buffer_length = 1 << (32 - ff_clz(s->air_len));
    s->n_fft = n_fft = 1 << (32 - ff_clz(ir_len + s->size));

    if (s->type == FREQUENCY_DOMAIN) {
        fft_in_l = av_calloc(n_fft, sizeof(*fft_in_l));
        fft_in_r = av_calloc(n_fft, sizeof(*fft_in_r));
        if (!fft_in_l || !fft_in_r) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        av_fft_end(s->fft[0]);
        av_fft_end(s->fft[1]);
        s->fft[0] = av_fft_init(av_log2(s->n_fft), 0);
        s->fft[1] = av_fft_init(av_log2(s->n_fft), 0);
        av_fft_end(s->ifft[0]);
        av_fft_end(s->ifft[1]);
        s->ifft[0] = av_fft_init(av_log2(s->n_fft), 1);
        s->ifft[1] = av_fft_init(av_log2(s->n_fft), 1);

        if (!s->fft[0] || !s->fft[1] || !s->ifft[0] || !s->ifft[1]) {
            av_log(ctx, AV_LOG_ERROR, "Unable to create FFT contexts of size %d.\n", s->n_fft);
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    s->data_ir[0] = av_calloc(s->air_len, sizeof(float) * s->nb_irs);
    s->data_ir[1] = av_calloc(s->air_len, sizeof(float) * s->nb_irs);
    s->delay[0] = av_calloc(s->nb_irs, sizeof(float));
    s->delay[1] = av_calloc(s->nb_irs, sizeof(float));

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

    if (!s->data_ir[0] || !s->data_ir[1] ||
        !s->ringbuffer[0] || !s->ringbuffer[1]) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (s->type == TIME_DOMAIN) {
        s->temp_src[0] = av_calloc(s->air_len, sizeof(float));
        s->temp_src[1] = av_calloc(s->air_len, sizeof(float));

        data_ir_l = av_calloc(nb_irs * s->air_len, sizeof(*data_ir_l));
        data_ir_r = av_calloc(nb_irs * s->air_len, sizeof(*data_ir_r));
        if (!data_ir_r || !data_ir_l || !s->temp_src[0] || !s->temp_src[1]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    } else {
        data_hrtf_l = av_calloc(n_fft, sizeof(*data_hrtf_l) * nb_irs);
        data_hrtf_r = av_calloc(n_fft, sizeof(*data_hrtf_r) * nb_irs);
        if (!data_hrtf_r || !data_hrtf_l) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    for (i = 0; i < s->nb_inputs - 1; i++) {
        int len = s->in[i + 1].ir_len;
        int delay_l = s->in[i + 1].delay_l;
        int delay_r = s->in[i + 1].delay_r;
        float *ptr;

        ret = ff_inlink_consume_samples(ctx->inputs[i + 1], len, len, &s->in[i + 1].frame);
        if (ret < 0)
            goto fail;
        ptr = (float *)s->in[i + 1].frame->extended_data[0];

        if (s->hrir_fmt == HRIR_STEREO) {
            int idx = -1;

            for (j = 0; j < inlink->channels; j++) {
                if (s->mapping[i] < 0) {
                    continue;
                }

                if ((av_channel_layout_extract_channel(inlink->channel_layout, j)) == (1LL << s->mapping[i])) {
                    idx = i;
                    break;
                }
            }

            if (idx == -1)
                continue;
            if (s->type == TIME_DOMAIN) {
                offset = idx * s->air_len;
                for (j = 0; j < len; j++) {
                    data_ir_l[offset + j] = ptr[len * 2 - j * 2 - 2] * gain_lin;
                    data_ir_r[offset + j] = ptr[len * 2 - j * 2 - 1] * gain_lin;
                }
            } else {
                memset(fft_in_l, 0, n_fft * sizeof(*fft_in_l));
                memset(fft_in_r, 0, n_fft * sizeof(*fft_in_r));

                offset = idx * n_fft;
                for (j = 0; j < len; j++) {
                    fft_in_l[delay_l + j].re = ptr[j * 2    ] * gain_lin;
                    fft_in_r[delay_r + j].re = ptr[j * 2 + 1] * gain_lin;
                }

                av_fft_permute(s->fft[0], fft_in_l);
                av_fft_calc(s->fft[0], fft_in_l);
                memcpy(data_hrtf_l + offset, fft_in_l, n_fft * sizeof(*fft_in_l));
                av_fft_permute(s->fft[0], fft_in_r);
                av_fft_calc(s->fft[0], fft_in_r);
                memcpy(data_hrtf_r + offset, fft_in_r, n_fft * sizeof(*fft_in_r));
            }
        } else {
            int I, N = ctx->inputs[1]->channels;

            for (k = 0; k < N / 2; k++) {
                int idx = -1;

                for (j = 0; j < inlink->channels; j++) {
                    if (s->mapping[k] < 0) {
                        continue;
                    }

                    if ((av_channel_layout_extract_channel(inlink->channel_layout, j)) == (1LL << s->mapping[k])) {
                        idx = k;
                        break;
                    }
                }
                if (idx == -1)
                    continue;

                I = idx * 2;
                if (s->type == TIME_DOMAIN) {
                    offset = idx * s->air_len;
                    for (j = 0; j < len; j++) {
                        data_ir_l[offset + j] = ptr[len * N - j * N - N + I    ] * gain_lin;
                        data_ir_r[offset + j] = ptr[len * N - j * N - N + I + 1] * gain_lin;
                    }
                } else {
                    memset(fft_in_l, 0, n_fft * sizeof(*fft_in_l));
                    memset(fft_in_r, 0, n_fft * sizeof(*fft_in_r));

                    offset = idx * n_fft;
                    for (j = 0; j < len; j++) {
                        fft_in_l[delay_l + j].re = ptr[j * N + I    ] * gain_lin;
                        fft_in_r[delay_r + j].re = ptr[j * N + I + 1] * gain_lin;
                    }

                    av_fft_permute(s->fft[0], fft_in_l);
                    av_fft_calc(s->fft[0], fft_in_l);
                    memcpy(data_hrtf_l + offset, fft_in_l, n_fft * sizeof(*fft_in_l));
                    av_fft_permute(s->fft[0], fft_in_r);
                    av_fft_calc(s->fft[0], fft_in_r);
                    memcpy(data_hrtf_r + offset, fft_in_r, n_fft * sizeof(*fft_in_r));
                }
            }
        }

        av_frame_free(&s->in[i + 1].frame);
    }

    if (s->type == TIME_DOMAIN) {
        memcpy(s->data_ir[0], data_ir_l, sizeof(float) * nb_irs * s->air_len);
        memcpy(s->data_ir[1], data_ir_r, sizeof(float) * nb_irs * s->air_len);
    } else {
        s->data_hrtf[0] = av_calloc(n_fft * s->nb_irs, sizeof(FFTComplex));
        s->data_hrtf[1] = av_calloc(n_fft * s->nb_irs, sizeof(FFTComplex));
        if (!s->data_hrtf[0] || !s->data_hrtf[1]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        memcpy(s->data_hrtf[0], data_hrtf_l,
            sizeof(FFTComplex) * nb_irs * n_fft);
        memcpy(s->data_hrtf[1], data_hrtf_r,
            sizeof(FFTComplex) * nb_irs * n_fft);
    }

    s->have_hrirs = 1;

fail:

    for (i = 0; i < s->nb_inputs - 1; i++)
        av_frame_free(&s->in[i + 1].frame);

    av_freep(&data_ir_l);
    av_freep(&data_ir_r);

    av_freep(&data_hrtf_l);
    av_freep(&data_hrtf_r);

    av_freep(&fft_in_l);
    av_freep(&fft_in_r);

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
        for (i = 1; i < s->nb_inputs; i++) {
            if (s->in[i].eof)
                continue;

            if ((ret = check_ir(ctx->inputs[i], i)) < 0)
                return ret;

            if (!s->in[i].eof) {
                if (ff_outlink_get_status(ctx->inputs[i]) == AVERROR_EOF)
                    s->in[i].eof = 1;
            }
        }

        for (i = 1; i < s->nb_inputs; i++) {
            if (!s->in[i].eof)
                break;
        }

        if (i != s->nb_inputs) {
            if (ff_outlink_frame_wanted(ctx->outputs[0])) {
                for (i = 1; i < s->nb_inputs; i++) {
                    if (!s->in[i].eof)
                        ff_inlink_request_frame(ctx->inputs[i]);
                }
            }

            return 0;
        } else {
            s->eof_hrirs = 1;
        }
    }

    if (!s->have_hrirs && s->eof_hrirs) {
        ret = convert_coeffs(ctx, inlink);
        if (ret < 0)
            return ret;
    }

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

    ret = ff_channel_layouts_ref(layouts, &ctx->inputs[0]->out_channel_layouts);
    if (ret)
        return ret;

    ret = ff_add_channel_layout(&stereo_layout, AV_CH_LAYOUT_STEREO);
    if (ret)
        return ret;

    if (s->hrir_fmt == HRIR_MULTI) {
        hrir_layouts = ff_all_channel_counts();
        if (!hrir_layouts)
            return AVERROR(ENOMEM);
        ret = ff_channel_layouts_ref(hrir_layouts, &ctx->inputs[1]->out_channel_layouts);
        if (ret)
            return ret;
    } else {
        for (i = 1; i < s->nb_inputs; i++) {
            ret = ff_channel_layouts_ref(stereo_layout, &ctx->inputs[i]->out_channel_layouts);
            if (ret)
                return ret;
        }
    }

    ret = ff_channel_layouts_ref(stereo_layout, &ctx->outputs[0]->in_channel_layouts);
    if (ret)
        return ret;

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

    s->in = av_calloc(s->nb_inputs, sizeof(*s->in));
    if (!s->in)
        return AVERROR(ENOMEM);

    for (i = 1; i < s->nb_inputs; i++) {
        char *name = av_asprintf("hrir%d", i - 1);
        AVFilterPad pad = {
            .name         = name,
            .type         = AVMEDIA_TYPE_AUDIO,
        };
        if (!name)
            return AVERROR(ENOMEM);
        if ((ret = ff_insert_inpad(ctx, i, &pad)) < 0) {
            av_freep(&pad.name);
            return ret;
        }
    }

    s->fdsp = avpriv_float_dsp_alloc(0);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

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
    int i;

    av_fft_end(s->ifft[0]);
    av_fft_end(s->ifft[1]);
    av_fft_end(s->fft[0]);
    av_fft_end(s->fft[1]);
    av_freep(&s->delay[0]);
    av_freep(&s->delay[1]);
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
    av_freep(&s->fdsp);

    for (i = 0; i < s->nb_inputs; i++) {
        if (ctx->input_pads && i)
            av_freep(&ctx->input_pads[i].name);
    }
    av_freep(&s->in);
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
