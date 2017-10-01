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

#include "libavutil/audio_fifo.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/float_dsp.h"
#include "libavutil/intmath.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"

#include "avfilter.h"
#include "internal.h"
#include "audio.h"

#define TIME_DOMAIN      0
#define FREQUENCY_DOMAIN 1

typedef struct HeadphoneContext {
    const AVClass *class;

    char *map;
    int type;

    int lfe_channel;

    int have_hrirs;
    int eof_hrirs;
    int64_t pts;

    int ir_len;

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

    int *delay[2];
    float *data_ir[2];
    float *temp_src[2];
    FFTComplex *temp_fft[2];

    FFTContext *fft[2], *ifft[2];
    FFTComplex *data_hrtf[2];

    AVFloatDSPContext *fdsp;
    struct headphone_inputs {
        AVAudioFifo *fifo;
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
        if (parse_channel_name(s, s->nb_inputs - 1, &arg, &out_ch_id, buf)) {
            av_log(ctx, AV_LOG_WARNING, "Failed to parse \'%s\' as channel name.\n", buf);
            continue;
        }
        s->mapping[s->nb_inputs - 1] = out_ch_id;
        s->nb_inputs++;
    }
    s->nb_irs = s->nb_inputs - 1;

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
                temp_ir += FFALIGN(ir_len, 16);
                continue;
            }

            read = (wr - *(delay + l) - (ir_len - 1) + buffer_length) & modulo;

            if (read + ir_len < buffer_length) {
                memcpy(temp_src, bptr + read, ir_len * sizeof(*temp_src));
            } else {
                int len = FFMIN(ir_len - (read % ir_len), buffer_length - read);

                memcpy(temp_src, bptr + read, len * sizeof(*temp_src));
                memcpy(temp_src + len, bptr, (ir_len - len) * sizeof(*temp_src));
            }

            dst[0] += s->fdsp->scalarproduct_float(temp_ir, temp_src, ir_len);
            temp_ir += FFALIGN(ir_len, 16);
        }

        if (fabs(*dst) > 1)
            *n_clippings += 1;

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
    FFTContext *ifft = s->ifft[jobnr];
    FFTContext *fft = s->fft[jobnr];
    const int n_fft = s->n_fft;
    const float fft_scale = 1.0f / s->n_fft;
    FFTComplex *hrtf_offset;
    int wr = *write;
    int n_read;
    int i, j;

    dst += offset;

    n_read = FFMIN(s->ir_len, in->nb_samples);
    for (j = 0; j < n_read; j++) {
        dst[2 * j]     = ringbuffer[wr];
        ringbuffer[wr] = 0.0;
        wr  = (wr + 1) & modulo;
    }

    for (j = n_read; j < in->nb_samples; j++) {
        dst[2 * j] = 0;
    }

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

            fft_in[j].re = re * hcomplex->re - im * hcomplex->im;
            fft_in[j].im = re * hcomplex->im + im * hcomplex->re;
        }

        av_fft_permute(ifft, fft_in);
        av_fft_calc(ifft, fft_in);

        for (j = 0; j < in->nb_samples; j++) {
            dst[2 * j] += fft_in[j].re * fft_scale;
        }

        for (j = 0; j < ir_len - 1; j++) {
            int write_pos = (wr + j) & modulo;

            *(ringbuffer + write_pos) += fft_in[in->nb_samples + j].re * fft_scale;
        }
    }

    for (i = 0; i < out->nb_samples; i++) {
        if (fabs(*dst) > 1) {
            n_clippings[0]++;
        }

        dst += 2;
    }

    *write = wr;

    return 0;
}

static int read_ir(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    HeadphoneContext *s = ctx->priv;
    int ir_len, max_ir_len, input_number;

    for (input_number = 0; input_number < s->nb_inputs; input_number++)
        if (inlink == ctx->inputs[input_number])
            break;

    av_audio_fifo_write(s->in[input_number].fifo, (void **)frame->extended_data,
                        frame->nb_samples);
    av_frame_free(&frame);

    ir_len = av_audio_fifo_size(s->in[input_number].fifo);
    max_ir_len = 65536;
    if (ir_len > max_ir_len) {
        av_log(ctx, AV_LOG_ERROR, "Too big length of IRs: %d > %d.\n", ir_len, max_ir_len);
        return AVERROR(EINVAL);
    }
    s->in[input_number].ir_len = ir_len;
    s->ir_len = FFMAX(ir_len, s->ir_len);

    return 0;
}

static int headphone_frame(HeadphoneContext *s, AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFrame *in = s->in[0].frame;
    int n_clippings[2] = { 0 };
    ThreadData td;
    AVFrame *out;

    av_audio_fifo_read(s->in[0].fifo, (void **)in->extended_data, s->size);

    out = ff_get_audio_buffer(outlink, in->nb_samples);
    if (!out)
        return AVERROR(ENOMEM);
    out->pts = s->pts;
    if (s->pts != AV_NOPTS_VALUE)
        s->pts += av_rescale_q(out->nb_samples, (AVRational){1, outlink->sample_rate}, outlink->time_base);

    td.in = in; td.out = out; td.write = s->write;
    td.delay = s->delay; td.ir = s->data_ir; td.n_clippings = n_clippings;
    td.ringbuffer = s->ringbuffer; td.temp_src = s->temp_src;
    td.temp_fft = s->temp_fft;

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
    int i, j;

    s->buffer_length = 1 << (32 - ff_clz(s->ir_len));
    s->n_fft = n_fft = 1 << (32 - ff_clz(s->ir_len + inlink->sample_rate));

    if (s->type == FREQUENCY_DOMAIN) {
        fft_in_l = av_calloc(n_fft, sizeof(*fft_in_l));
        fft_in_r = av_calloc(n_fft, sizeof(*fft_in_r));
        if (!fft_in_l || !fft_in_r) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        av_fft_end(s->fft[0]);
        av_fft_end(s->fft[1]);
        s->fft[0] = av_fft_init(log2(s->n_fft), 0);
        s->fft[1] = av_fft_init(log2(s->n_fft), 0);
        av_fft_end(s->ifft[0]);
        av_fft_end(s->ifft[1]);
        s->ifft[0] = av_fft_init(log2(s->n_fft), 1);
        s->ifft[1] = av_fft_init(log2(s->n_fft), 1);

        if (!s->fft[0] || !s->fft[1] || !s->ifft[0] || !s->ifft[1]) {
            av_log(ctx, AV_LOG_ERROR, "Unable to create FFT contexts of size %d.\n", s->n_fft);
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    s->data_ir[0] = av_calloc(FFALIGN(s->ir_len, 16), sizeof(float) * s->nb_irs);
    s->data_ir[1] = av_calloc(FFALIGN(s->ir_len, 16), sizeof(float) * s->nb_irs);
    s->delay[0] = av_malloc_array(s->nb_irs, sizeof(float));
    s->delay[1] = av_malloc_array(s->nb_irs, sizeof(float));

    if (s->type == TIME_DOMAIN) {
        s->ringbuffer[0] = av_calloc(s->buffer_length, sizeof(float) * nb_input_channels);
        s->ringbuffer[1] = av_calloc(s->buffer_length, sizeof(float) * nb_input_channels);
    } else {
        s->ringbuffer[0] = av_calloc(s->buffer_length, sizeof(float));
        s->ringbuffer[1] = av_calloc(s->buffer_length, sizeof(float));
        s->temp_fft[0] = av_malloc_array(s->n_fft, sizeof(FFTComplex));
        s->temp_fft[1] = av_malloc_array(s->n_fft, sizeof(FFTComplex));
        if (!s->temp_fft[0] || !s->temp_fft[1]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (!s->data_ir[0] || !s->data_ir[1] ||
        !s->ringbuffer[0] || !s->ringbuffer[1]) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    s->in[0].frame = ff_get_audio_buffer(ctx->inputs[0], s->size);
    if (!s->in[0].frame) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    for (i = 0; i < s->nb_irs; i++) {
        s->in[i + 1].frame = ff_get_audio_buffer(ctx->inputs[i + 1], s->ir_len);
        if (!s->in[i + 1].frame) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (s->type == TIME_DOMAIN) {
        s->temp_src[0] = av_calloc(FFALIGN(ir_len, 16), sizeof(float));
        s->temp_src[1] = av_calloc(FFALIGN(ir_len, 16), sizeof(float));

        data_ir_l = av_calloc(nb_irs * FFALIGN(ir_len, 16), sizeof(*data_ir_l));
        data_ir_r = av_calloc(nb_irs * FFALIGN(ir_len, 16), sizeof(*data_ir_r));
        if (!data_ir_r || !data_ir_l || !s->temp_src[0] || !s->temp_src[1]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    } else {
        data_hrtf_l = av_malloc_array(n_fft, sizeof(*data_hrtf_l) * nb_irs);
        data_hrtf_r = av_malloc_array(n_fft, sizeof(*data_hrtf_r) * nb_irs);
        if (!data_hrtf_r || !data_hrtf_l) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    for (i = 0; i < s->nb_irs; i++) {
        int len = s->in[i + 1].ir_len;
        int delay_l = s->in[i + 1].delay_l;
        int delay_r = s->in[i + 1].delay_r;
        int idx = -1;
        float *ptr;

        for (j = 0; j < inlink->channels; j++) {
            if (s->mapping[i] < 0) {
                continue;
            }

            if ((av_channel_layout_extract_channel(inlink->channel_layout, j)) == (1LL << s->mapping[i])) {
                idx = j;
                break;
            }
        }
        if (idx == -1)
            continue;

        av_audio_fifo_read(s->in[i + 1].fifo, (void **)s->in[i + 1].frame->extended_data, len);
        ptr = (float *)s->in[i + 1].frame->extended_data[0];

        if (s->type == TIME_DOMAIN) {
            offset = idx * FFALIGN(len, 16);
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
    }

    if (s->type == TIME_DOMAIN) {
        memcpy(s->data_ir[0], data_ir_l, sizeof(float) * nb_irs * FFALIGN(ir_len, 16));
        memcpy(s->data_ir[1], data_ir_r, sizeof(float) * nb_irs * FFALIGN(ir_len, 16));
    } else {
        s->data_hrtf[0] = av_malloc_array(n_fft * s->nb_irs, sizeof(FFTComplex));
        s->data_hrtf[1] = av_malloc_array(n_fft * s->nb_irs, sizeof(FFTComplex));
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

    av_freep(&data_ir_l);
    av_freep(&data_ir_r);

    av_freep(&data_hrtf_l);
    av_freep(&data_hrtf_r);

    av_freep(&fft_in_l);
    av_freep(&fft_in_r);

    return ret;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    HeadphoneContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int ret = 0;

    av_audio_fifo_write(s->in[0].fifo, (void **)in->extended_data,
                        in->nb_samples);
    if (s->pts == AV_NOPTS_VALUE)
        s->pts = in->pts;

    av_frame_free(&in);

    if (!s->have_hrirs && s->eof_hrirs) {
        ret = convert_coeffs(ctx, inlink);
        if (ret < 0)
            return ret;
    }

    if (s->have_hrirs) {
        while (av_audio_fifo_size(s->in[0].fifo) >= s->size) {
            ret = headphone_frame(s, outlink);
            if (ret < 0)
                break;
        }
    }
    return ret;
}

static int query_formats(AVFilterContext *ctx)
{
    struct HeadphoneContext *s = ctx->priv;
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
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

    layouts = NULL;
    ret = ff_add_channel_layout(&layouts, AV_CH_LAYOUT_STEREO);
    if (ret)
        return ret;

    for (i = 1; i < s->nb_inputs; i++) {
        ret = ff_channel_layouts_ref(layouts, &ctx->inputs[i]->out_channel_layouts);
        if (ret)
            return ret;
    }

    ret = ff_channel_layouts_ref(layouts, &ctx->outputs[0]->in_channel_layouts);
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

    if (s->type == FREQUENCY_DOMAIN) {
        inlink->partial_buf_size =
        inlink->min_samples =
        inlink->max_samples = inlink->sample_rate;
    }

    if (s->nb_irs < inlink->channels) {
        av_log(ctx, AV_LOG_ERROR, "Number of inputs must be >= %d.\n", inlink->channels + 1);
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
        .filter_frame = filter_frame,
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
            .filter_frame = read_ir,
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
    s->pts = AV_NOPTS_VALUE;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    HeadphoneContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int i;

    if (s->type == TIME_DOMAIN)
        s->size = 1024;
    else
        s->size = inlink->sample_rate;

    for (i = 0; i < s->nb_inputs; i++) {
        s->in[i].fifo = av_audio_fifo_alloc(ctx->inputs[i]->format, ctx->inputs[i]->channels, 1024);
        if (!s->in[i].fifo)
            return AVERROR(ENOMEM);
    }
    s->gain_lfe = expf((s->gain - 3 * inlink->channels - 6 + s->lfe_gain) / 20 * M_LN10);

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    HeadphoneContext *s = ctx->priv;
    int i, ret;

    for (i = 1; !s->eof_hrirs && i < s->nb_inputs; i++) {
        if (!s->in[i].eof) {
            ret = ff_request_frame(ctx->inputs[i]);
            if (ret == AVERROR_EOF) {
                s->in[i].eof = 1;
                ret = 0;
            }
            return ret;
        } else {
            if (i == s->nb_inputs - 1)
                s->eof_hrirs = 1;
        }
    }
    return ff_request_frame(ctx->inputs[0]);
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
    av_freep(&s->data_hrtf[0]);
    av_freep(&s->data_hrtf[1]);
    av_freep(&s->fdsp);

    for (i = 0; i < s->nb_inputs; i++) {
        av_frame_free(&s->in[i].frame);
        av_audio_fifo_free(s->in[i].fifo);
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
    { NULL }
};

AVFILTER_DEFINE_CLASS(headphone);

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
        .request_frame = request_frame,
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
    .inputs        = NULL,
    .outputs       = outputs,
    .flags         = AVFILTER_FLAG_SLICE_THREADS | AVFILTER_FLAG_DYNAMIC_INPUTS,
};
