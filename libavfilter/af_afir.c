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

#include <float.h>

#include "libavutil/common.h"
#include "libavutil/float_dsp.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/xga_font_data.h"
#include "libavcodec/avfft.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"
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

static int fir_frame(AudioFIRContext *s, AVFrame *in, AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFrame *out = NULL;
    int ret;

    s->nb_samples = in->nb_samples;

    if (!s->want_skip) {
        out = ff_get_audio_buffer(outlink, s->nb_samples);
        if (!out)
            return AVERROR(ENOMEM);
    }

    if (s->pts == AV_NOPTS_VALUE)
        s->pts = in->pts;
    s->in[0] = in;
    ctx->internal->execute(ctx, fir_channel, out, NULL, outlink->channels);

    s->part_index = (s->part_index + 1) % s->nb_partitions;

    if (!s->want_skip) {
        out->pts = s->pts;
        if (s->pts != AV_NOPTS_VALUE)
            s->pts += av_rescale_q(out->nb_samples, (AVRational){1, outlink->sample_rate}, outlink->time_base);
    }

    s->index++;
    if (s->index == 3)
        s->index = 0;

    av_frame_free(&in);

    if (s->want_skip == 1) {
        s->want_skip = 0;
        ret = 0;
    } else {
        ret = ff_filter_frame(outlink, out);
    }

    return ret;
}

static void drawtext(AVFrame *pic, int x, int y, const char *txt, uint32_t color)
{
    const uint8_t *font;
    int font_height;
    int i;

    font = avpriv_cga_font, font_height = 8;

    for (i = 0; txt[i]; i++) {
        int char_y, mask;

        uint8_t *p = pic->data[0] + y * pic->linesize[0] + (x + i * 8) * 4;
        for (char_y = 0; char_y < font_height; char_y++) {
            for (mask = 0x80; mask; mask >>= 1) {
                if (font[txt[i] * font_height + char_y] & mask)
                    AV_WL32(p, color);
                p += 4;
            }
            p += pic->linesize[0] - 8 * 4;
        }
    }
}

static void draw_line(AVFrame *out, int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = FFABS(x1-x0);
    int dy = FFABS(y1-y0), sy = y0 < y1 ? 1 : -1;
    int err = (dx>dy ? dx : -dy) / 2, e2;

    for (;;) {
        AV_WL32(out->data[0] + y0 * out->linesize[0] + x0 * 4, color);

        if (x0 == x1 && y0 == y1)
            break;

        e2 = err;

        if (e2 >-dx) {
            err -= dy;
            x0--;
        }

        if (e2 < dy) {
            err += dx;
            y0 += sy;
        }
    }
}

static void draw_response(AVFilterContext *ctx, AVFrame *out)
{
    AudioFIRContext *s = ctx->priv;
    float *mag, *phase, min = FLT_MAX, max = FLT_MIN;
    int prev_ymag = -1, prev_yphase = -1;
    char text[32];
    int channel, i, x;

    memset(out->data[0], 0, s->h * out->linesize[0]);

    phase = av_malloc_array(s->w, sizeof(*phase));
    mag = av_malloc_array(s->w, sizeof(*mag));
    if (!mag || !phase)
        goto end;

    channel = av_clip(s->ir_channel, 0, s->in[1]->channels - 1);
    for (i = 0; i < s->w; i++) {
        const float *src = (const float *)s->in[1]->extended_data[channel];
        double w = i * M_PI / (s->w - 1);
        double real = 0.;
        double imag = 0.;

        for (x = 0; x < s->nb_taps; x++) {
            real += cos(-x * w) * src[x];
            imag += sin(-x * w) * src[x];
        }

        mag[i] = hypot(real, imag);
        phase[i] = atan2(imag, real);
        min = fminf(min, mag[i]);
        max = fmaxf(max, mag[i]);
    }

    for (i = 0; i < s->w; i++) {
        int ymag = mag[i] / max * (s->h - 1);
        int yphase = (0.5 * (1. + phase[i] / M_PI)) * (s->h - 1);

        ymag = s->h - 1 - av_clip(ymag, 0, s->h - 1);
        yphase = s->h - 1 - av_clip(yphase, 0, s->h - 1);

        if (prev_ymag < 0)
            prev_ymag = ymag;
        if (prev_yphase < 0)
            prev_yphase = yphase;

        draw_line(out, i,   ymag, FFMAX(i - 1, 0),   prev_ymag, 0xFFFF00FF);
        draw_line(out, i, yphase, FFMAX(i - 1, 0), prev_yphase, 0xFF00FF00);

        prev_ymag   = ymag;
        prev_yphase = yphase;
    }

    if (s->w > 400 && s->h > 100) {
        drawtext(out, 2, 2, "Max Magnitude:", 0xDDDDDDDD);
        snprintf(text, sizeof(text), "%.2f", max);
        drawtext(out, 15 * 8 + 2, 2, text, 0xDDDDDDDD);

        drawtext(out, 2, 12, "Min Magnitude:", 0xDDDDDDDD);
        snprintf(text, sizeof(text), "%.2f", min);
        drawtext(out, 15 * 8 + 2, 12, text, 0xDDDDDDDD);
    }

end:
    av_free(phase);
    av_free(mag);
}

static int convert_coeffs(AVFilterContext *ctx)
{
    AudioFIRContext *s = ctx->priv;
    int ret, i, ch, n, N;
    float power = 0;

    s->nb_taps = ff_inlink_queued_samples(ctx->inputs[1]);
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

    s->buffer = ff_get_audio_buffer(ctx->inputs[0], s->part_size * 3);
    if (!s->buffer)
        return AVERROR(ENOMEM);

    ret = ff_inlink_consume_samples(ctx->inputs[1], s->nb_taps, s->nb_taps, &s->in[1]);
    if (ret < 0)
        return ret;
    if (ret == 0)
        return AVERROR_BUG;

    if (s->response)
        draw_response(ctx, s->video);

    s->gain = 1;

    switch (s->gtype) {
    case -1:
        /* nothinkg to do */
        break;
    case 0:
        for (ch = 0; ch < ctx->inputs[1]->channels; ch++) {
            float *time = (float *)s->in[1]->extended_data[!s->one2many * ch];

            for (i = 0; i < s->nb_taps; i++)
                power += FFABS(time[i]);
        }
        s->gain = ctx->inputs[1]->channels / power;
        break;
    case 1:
        for (ch = 0; ch < ctx->inputs[1]->channels; ch++) {
            float *time = (float *)s->in[1]->extended_data[!s->one2many * ch];

            for (i = 0; i < s->nb_taps; i++)
                power += time[i];
        }
        s->gain = ctx->inputs[1]->channels / power;
        break;
    case 2:
        for (ch = 0; ch < ctx->inputs[1]->channels; ch++) {
            float *time = (float *)s->in[1]->extended_data[!s->one2many * ch];

            for (i = 0; i < s->nb_taps; i++)
                power += time[i] * time[i];
        }
        s->gain = sqrtf(ch / power);
        break;
    default:
        return AVERROR_BUG;
    }

    s->gain = FFMIN(s->gain * s->ir_gain, 1.f);
    av_log(ctx, AV_LOG_DEBUG, "power %f, gain %f\n", power, s->gain);
    for (ch = 0; ch < ctx->inputs[1]->channels; ch++) {
        float *time = (float *)s->in[1]->extended_data[!s->one2many * ch];

        s->fdsp->vector_fmul_scalar(time, time, s->gain, FFALIGN(s->nb_taps, 4));
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

static int check_ir(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *ctx = link->dst;
    AudioFIRContext *s = ctx->priv;
    int nb_taps, max_nb_taps;

    nb_taps = ff_inlink_queued_samples(link);
    max_nb_taps = s->max_ir_len * ctx->outputs[0]->sample_rate;
    if (nb_taps > max_nb_taps) {
        av_log(ctx, AV_LOG_ERROR, "Too big number of coefficients: %d > %d.\n", nb_taps, max_nb_taps);
        return AVERROR(EINVAL);
    }

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AudioFIRContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *in = NULL;
    int ret, status;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[0], ctx);
    if (s->response)
        FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[1], ctx);
    if (!s->eof_coeffs) {
        AVFrame *ir = NULL;

        ret = check_ir(ctx->inputs[1], ir);
        if (ret < 0)
            return ret;

        if (ff_outlink_get_status(ctx->inputs[1]) == AVERROR_EOF)
            s->eof_coeffs = 1;

        if (!s->eof_coeffs) {
            if (ff_outlink_frame_wanted(ctx->outputs[0]))
                ff_inlink_request_frame(ctx->inputs[1]);
            return 0;
        }
    }

    if (!s->have_coeffs && s->eof_coeffs) {
        ret = convert_coeffs(ctx);
        if (ret < 0)
            return ret;
    }

    if (s->need_padding) {
        in = ff_get_audio_buffer(outlink, s->part_size);
        if (!in)
            return AVERROR(ENOMEM);
        s->need_padding = 0;
        ret = 1;
    } else {
        ret = ff_inlink_consume_samples(ctx->inputs[0], s->part_size, s->part_size, &in);
    }

    if (ret > 0) {
        ret = fir_frame(s, in, outlink);
        if (ret < 0)
            return ret;
    }

    if (ret < 0)
        return ret;

    if (s->response && s->have_coeffs) {
        if (ff_outlink_frame_wanted(ctx->outputs[1])) {
            s->video->pts = s->pts;
            ret = ff_filter_frame(ctx->outputs[1], av_frame_clone(s->video));
            if (ret < 0)
                return ret;
        }
    }

    if (ff_inlink_acknowledge_status(ctx->inputs[0], &status, &pts)) {
        if (status == AVERROR_EOF) {
            ff_outlink_set_status(ctx->outputs[0], status, pts);
            if (s->response)
                ff_outlink_set_status(ctx->outputs[1], status, pts);
            return 0;
        }
    }

    if (ff_outlink_frame_wanted(ctx->outputs[0])) {
        ff_inlink_request_frame(ctx->inputs[0]);
        return 0;
    }

    if (s->response && ff_outlink_frame_wanted(ctx->outputs[1])) {
        ff_inlink_request_frame(ctx->inputs[0]);
        return 0;
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AudioFIRContext *s = ctx->priv;
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_NONE
    };
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGB0,
        AV_PIX_FMT_NONE
    };
    int ret;

    if (s->response) {
        AVFilterLink *videolink = ctx->outputs[1];
        formats = ff_make_format_list(pix_fmts);
        if ((ret = ff_formats_ref(formats, &videolink->in_formats)) < 0)
            return ret;
    }

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);

    if (s->ir_format) {
        ret = ff_set_common_channel_layouts(ctx, layouts);
        if (ret < 0)
            return ret;
    } else {
        AVFilterChannelLayouts *mono = NULL;

        ret = ff_add_channel_layout(&mono, AV_CH_LAYOUT_MONO);
        if (ret)
            return ret;

        if ((ret = ff_channel_layouts_ref(layouts, &ctx->inputs[0]->out_channel_layouts)) < 0)
            return ret;
        if ((ret = ff_channel_layouts_ref(layouts, &ctx->outputs[0]->in_channel_layouts)) < 0)
            return ret;
        if ((ret = ff_channel_layouts_ref(mono, &ctx->inputs[1]->out_channel_layouts)) < 0)
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

    s->one2many = ctx->inputs[1]->channels == 1;
    outlink->sample_rate = ctx->inputs[0]->sample_rate;
    outlink->time_base   = ctx->inputs[0]->time_base;
    outlink->channel_layout = ctx->inputs[0]->channel_layout;
    outlink->channels = ctx->inputs[0]->channels;

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

    av_frame_free(&s->in[1]);
    av_frame_free(&s->buffer);

    av_freep(&s->fdsp);

    for (int i = 0; i < ctx->nb_outputs; i++)
        av_freep(&ctx->output_pads[i].name);
    av_frame_free(&s->video);
}

static int config_video(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioFIRContext *s = ctx->priv;

    outlink->sample_aspect_ratio = (AVRational){1,1};
    outlink->w = s->w;
    outlink->h = s->h;

    av_frame_free(&s->video);
    s->video = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!s->video)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    AudioFIRContext *s = ctx->priv;
    AVFilterPad pad, vpad;
    int ret;

    pad = (AVFilterPad){
        .name          = av_strdup("default"),
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
    };

    if (!pad.name)
        return AVERROR(ENOMEM);

    if (s->response) {
        vpad = (AVFilterPad){
            .name         = av_strdup("filter_response"),
            .type         = AVMEDIA_TYPE_VIDEO,
            .config_props = config_video,
        };
        if (!vpad.name)
            return AVERROR(ENOMEM);
    }

    ret = ff_insert_outpad(ctx, 0, &pad);
    if (ret < 0) {
        av_freep(&pad.name);
        return ret;
    }

    if (s->response) {
        ret = ff_insert_outpad(ctx, 1, &vpad);
        if (ret < 0) {
            av_freep(&vpad.name);
            return ret;
        }
    }

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
    },{
        .name           = "ir",
        .type           = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define VF AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define OFFSET(x) offsetof(AudioFIRContext, x)

static const AVOption afir_options[] = {
    { "dry",    "set dry gain",      OFFSET(dry_gain),   AV_OPT_TYPE_FLOAT, {.dbl=1},    0, 10, AF },
    { "wet",    "set wet gain",      OFFSET(wet_gain),   AV_OPT_TYPE_FLOAT, {.dbl=1},    0, 10, AF },
    { "length", "set IR length",     OFFSET(length),     AV_OPT_TYPE_FLOAT, {.dbl=1},    0,  1, AF },
    { "gtype",  "set IR auto gain type",OFFSET(gtype),   AV_OPT_TYPE_INT,   {.i64=0},   -1,  2, AF, "gtype" },
    {  "none",  "without auto gain", 0,                  AV_OPT_TYPE_CONST, {.i64=-1},   0,  0, AF, "gtype" },
    {  "peak",  "peak gain",         0,                  AV_OPT_TYPE_CONST, {.i64=0},    0,  0, AF, "gtype" },
    {  "dc",    "DC gain",           0,                  AV_OPT_TYPE_CONST, {.i64=1},    0,  0, AF, "gtype" },
    {  "gn",    "gain to noise",     0,                  AV_OPT_TYPE_CONST, {.i64=2},    0,  0, AF, "gtype" },
    { "irgain", "set IR gain",       OFFSET(ir_gain),    AV_OPT_TYPE_FLOAT, {.dbl=1},    0,  1, AF },
    { "irfmt",  "set IR format",     OFFSET(ir_format),  AV_OPT_TYPE_INT,   {.i64=1},    0,  1, AF, "irfmt" },
    {  "mono",  "single channel",    0,                  AV_OPT_TYPE_CONST, {.i64=0},    0,  0, AF, "irfmt" },
    {  "input", "same as input",     0,                  AV_OPT_TYPE_CONST, {.i64=1},    0,  0, AF, "irfmt" },
    { "maxir",  "set max IR length", OFFSET(max_ir_len), AV_OPT_TYPE_FLOAT, {.dbl=30}, 0.1, 60, AF },
    { "response", "show IR frequency response", OFFSET(response), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, VF },
    { "channel", "set IR channel to display frequency response", OFFSET(ir_channel), AV_OPT_TYPE_INT, {.i64=0}, 0, 1024, VF },
    { "size",   "set video size",    OFFSET(w),          AV_OPT_TYPE_IMAGE_SIZE, {.str = "hd720"}, 0, 0, VF },
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
    .activate      = activate,
    .uninit        = uninit,
    .inputs        = afir_inputs,
    .flags         = AVFILTER_FLAG_DYNAMIC_OUTPUTS |
                     AVFILTER_FLAG_SLICE_THREADS,
};
