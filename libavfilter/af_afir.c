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

#include "libavutil/cpu.h"
#include "libavutil/tx.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/float_dsp.h"
#include "libavutil/frame.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/rational.h"
#include "libavutil/xga_font_data.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "internal.h"
#include "af_afir.h"
#include "af_afirdsp.h"

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

#define DEPTH 32
#include "afir_template.c"

#undef DEPTH
#define DEPTH 64
#include "afir_template.c"

static int fir_channel(AVFilterContext *ctx, AVFrame *out, int ch)
{
    AudioFIRContext *s = ctx->priv;
    const int min_part_size = s->min_part_size;

    for (int offset = 0; offset < out->nb_samples; offset += min_part_size) {
        switch (s->format) {
        case AV_SAMPLE_FMT_FLTP:
            fir_quantum_float(ctx, out, ch, offset);
            break;
        case AV_SAMPLE_FMT_DBLP:
            fir_quantum_double(ctx, out, ch, offset);
            break;
        }
    }

    return 0;
}

static int fir_channels(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AVFrame *out = arg;
    const int start = (out->ch_layout.nb_channels * jobnr) / nb_jobs;
    const int end = (out->ch_layout.nb_channels * (jobnr+1)) / nb_jobs;

    for (int ch = start; ch < end; ch++)
        fir_channel(ctx, out, ch);

    return 0;
}

static int fir_frame(AudioFIRContext *s, AVFrame *in, AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFrame *out;

    out = ff_get_audio_buffer(outlink, in->nb_samples);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);
    out->pts = s->pts = in->pts;

    s->in = in;
    ff_filter_execute(ctx, fir_channels, out, NULL,
                      FFMIN(outlink->ch_layout.nb_channels, ff_filter_get_nb_threads(ctx)));

    av_frame_free(&in);
    s->in = NULL;

    return ff_filter_frame(outlink, out);
}

static int init_segment(AVFilterContext *ctx, AudioFIRSegment *seg, int selir,
                        int offset, int nb_partitions, int part_size, int index)
{
    AudioFIRContext *s = ctx->priv;
    const size_t cpu_align = av_cpu_max_align();
    union { double d; float f; } cscale, scale, iscale;
    enum AVTXType tx_type;
    int ret;

    seg->tx  = av_calloc(ctx->inputs[0]->ch_layout.nb_channels, sizeof(*seg->tx));
    seg->ctx = av_calloc(ctx->inputs[0]->ch_layout.nb_channels, sizeof(*seg->ctx));
    seg->itx = av_calloc(ctx->inputs[0]->ch_layout.nb_channels, sizeof(*seg->itx));
    if (!seg->tx || !seg->ctx || !seg->itx)
        return AVERROR(ENOMEM);

    seg->fft_length    = (part_size + 1) * 2;
    seg->part_size     = part_size;
    seg->block_size    = FFALIGN(seg->fft_length, cpu_align);
    seg->coeff_size    = FFALIGN(seg->part_size + 1, cpu_align);
    seg->nb_partitions = nb_partitions;
    seg->input_size    = offset + s->min_part_size;
    seg->input_offset  = offset;

    seg->loading       = av_calloc(ctx->inputs[0]->ch_layout.nb_channels, sizeof(*seg->loading));
    seg->part_index    = av_calloc(ctx->inputs[0]->ch_layout.nb_channels, sizeof(*seg->part_index));
    seg->output_offset = av_calloc(ctx->inputs[0]->ch_layout.nb_channels, sizeof(*seg->output_offset));
    if (!seg->part_index || !seg->output_offset || !seg->loading)
        return AVERROR(ENOMEM);

    switch (s->format) {
    case AV_SAMPLE_FMT_FLTP:
        cscale.f = 1.f;
        scale.f  = 1.f / sqrtf(2.f * part_size);
        iscale.f = 1.f / sqrtf(2.f * part_size);
        tx_type  = AV_TX_FLOAT_RDFT;
        break;
    case AV_SAMPLE_FMT_DBLP:
        cscale.d = 1.0;
        scale.d  = 1.0 / sqrt(2.0 * part_size);
        iscale.d = 1.0 / sqrt(2.0 * part_size);
        tx_type  = AV_TX_DOUBLE_RDFT;
        break;
    }

    for (int ch = 0; ch < ctx->inputs[0]->ch_layout.nb_channels && part_size >= 1; ch++) {
        ret = av_tx_init(&seg->ctx[ch], &seg->ctx_fn, tx_type,
                         0, 2 * part_size, &cscale,  0);
        if (ret < 0)
            return ret;

        ret = av_tx_init(&seg->tx[ch],  &seg->tx_fn,  tx_type,
                         0, 2 * part_size, &scale,  0);
        if (ret < 0)
            return ret;
        ret = av_tx_init(&seg->itx[ch], &seg->itx_fn, tx_type,
                         1, 2 * part_size, &iscale, 0);
        if (ret < 0)
            return ret;
    }

    seg->sumin  = ff_get_audio_buffer(ctx->inputs[0], seg->fft_length);
    seg->sumout = ff_get_audio_buffer(ctx->inputs[0], seg->fft_length);
    seg->blockout = ff_get_audio_buffer(ctx->inputs[0], seg->block_size * seg->nb_partitions);
    seg->tempin = ff_get_audio_buffer(ctx->inputs[0], seg->block_size);
    seg->tempout = ff_get_audio_buffer(ctx->inputs[0], seg->block_size);
    seg->buffer = ff_get_audio_buffer(ctx->inputs[0], seg->part_size);
    seg->input  = ff_get_audio_buffer(ctx->inputs[0], seg->input_size);
    seg->output = ff_get_audio_buffer(ctx->inputs[0], seg->part_size * 5);
    if (!seg->buffer || !seg->sumin || !seg->sumout || !seg->blockout ||
        !seg->input || !seg->output || !seg->tempin || !seg->tempout)
        return AVERROR(ENOMEM);

    return 0;
}

static void uninit_segment(AVFilterContext *ctx, AudioFIRSegment *seg)
{
    AudioFIRContext *s = ctx->priv;

    if (seg->ctx) {
        for (int ch = 0; ch < s->nb_channels; ch++)
            av_tx_uninit(&seg->ctx[ch]);
    }
    av_freep(&seg->ctx);

    if (seg->tx) {
        for (int ch = 0; ch < s->nb_channels; ch++)
            av_tx_uninit(&seg->tx[ch]);
    }
    av_freep(&seg->tx);

    if (seg->itx) {
        for (int ch = 0; ch < s->nb_channels; ch++)
            av_tx_uninit(&seg->itx[ch]);
    }
    av_freep(&seg->itx);

    av_freep(&seg->loading);
    av_freep(&seg->output_offset);
    av_freep(&seg->part_index);

    av_frame_free(&seg->tempin);
    av_frame_free(&seg->tempout);
    av_frame_free(&seg->blockout);
    av_frame_free(&seg->sumin);
    av_frame_free(&seg->sumout);
    av_frame_free(&seg->buffer);
    av_frame_free(&seg->input);
    av_frame_free(&seg->output);
    seg->input_size = 0;

    for (int i = 0; i < MAX_IR_STREAMS; i++)
        av_frame_free(&seg->coeff[i]);
}

static int convert_coeffs(AVFilterContext *ctx, int selir)
{
    AudioFIRContext *s = ctx->priv;
    int ret, nb_taps, cur_nb_taps;

    if (!s->nb_taps[selir]) {
        int part_size, max_part_size;
        int left, offset = 0;

        s->nb_taps[selir] = ff_inlink_queued_samples(ctx->inputs[1 + selir]);
        if (s->nb_taps[selir] <= 0)
            return AVERROR(EINVAL);

        if (s->minp > s->maxp)
            s->maxp = s->minp;

        if (s->nb_segments)
            goto skip;

        left = s->nb_taps[selir];
        part_size = 1 << av_log2(s->minp);
        max_part_size = 1 << av_log2(s->maxp);

        s->min_part_size = part_size;

        for (int i = 0; left > 0; i++) {
            int step = part_size == max_part_size ? INT_MAX : 1 + (i == 0);
            int nb_partitions = FFMIN(step, (left + part_size - 1) / part_size);

            s->nb_segments = i + 1;
            ret = init_segment(ctx, &s->seg[i], selir, offset, nb_partitions, part_size, i);
            if (ret < 0)
                return ret;
            offset += nb_partitions * part_size;
            left -= nb_partitions * part_size;
            part_size *= 2;
            part_size = FFMIN(part_size, max_part_size);
        }
    }

skip:
    if (!s->ir[selir]) {
        ret = ff_inlink_consume_samples(ctx->inputs[1 + selir], s->nb_taps[selir], s->nb_taps[selir], &s->ir[selir]);
        if (ret < 0)
            return ret;
        if (ret == 0)
            return AVERROR_BUG;
    }

    if (s->response) {
        switch (s->format) {
        case AV_SAMPLE_FMT_FLTP:
            draw_response_float(ctx, s->video);
            break;
        case AV_SAMPLE_FMT_DBLP:
            draw_response_double(ctx, s->video);
            break;
        }
    }

    cur_nb_taps  = s->ir[selir]->nb_samples;
    nb_taps      = cur_nb_taps;

    if (!s->norm_ir[selir] || s->norm_ir[selir]->nb_samples < nb_taps) {
        av_frame_free(&s->norm_ir[selir]);
        s->norm_ir[selir] = ff_get_audio_buffer(ctx->inputs[0], FFALIGN(nb_taps, 8));
        if (!s->norm_ir[selir])
            return AVERROR(ENOMEM);
    }

    av_log(ctx, AV_LOG_DEBUG, "nb_taps: %d\n", cur_nb_taps);
    av_log(ctx, AV_LOG_DEBUG, "nb_segments: %d\n", s->nb_segments);

    switch (s->format) {
    case AV_SAMPLE_FMT_FLTP:
        for (int ch = 0; ch < s->nb_channels; ch++) {
            const float *tsrc = (const float *)s->ir[selir]->extended_data[!s->one2many * ch];
            float *time = (float *)s->norm_ir[selir]->extended_data[ch];

            memcpy(time, tsrc, sizeof(*time) * nb_taps);
            for (int i = FFMAX(1, s->length * nb_taps); i < nb_taps; i++)
                time[i] = 0;

            get_power_float(ctx, s, nb_taps, ch, time);

            for (int n = 0; n < s->nb_segments; n++) {
                AudioFIRSegment *seg = &s->seg[n];

                if (!seg->coeff[selir])
                    seg->coeff[selir] = ff_get_audio_buffer(ctx->inputs[0], seg->nb_partitions * seg->coeff_size * 2);
                if (!seg->coeff[selir])
                    return AVERROR(ENOMEM);

                for (int i = 0; i < seg->nb_partitions; i++)
                    convert_channel_float(ctx, s, ch, seg, i, selir);
            }
        }
        break;
    case AV_SAMPLE_FMT_DBLP:
        for (int ch = 0; ch < s->nb_channels; ch++) {
            const double *tsrc = (const double *)s->ir[selir]->extended_data[!s->one2many * ch];
            double *time = (double *)s->norm_ir[selir]->extended_data[ch];

            memcpy(time, tsrc, sizeof(*time) * nb_taps);
            for (int i = FFMAX(1, s->length * nb_taps); i < nb_taps; i++)
                time[i] = 0;

            get_power_double(ctx, s, nb_taps, ch, time);
            for (int n = 0; n < s->nb_segments; n++) {
                AudioFIRSegment *seg = &s->seg[n];

                if (!seg->coeff[selir])
                    seg->coeff[selir] = ff_get_audio_buffer(ctx->inputs[0], seg->nb_partitions * seg->coeff_size * 2);
                if (!seg->coeff[selir])
                    return AVERROR(ENOMEM);

                for (int i = 0; i < seg->nb_partitions; i++)
                    convert_channel_double(ctx, s, ch, seg, i, selir);
            }
        }
        break;
    }

    s->have_coeffs[selir] = 1;

    return 0;
}

static int check_ir(AVFilterLink *link)
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
    int ret, status, available, wanted;
    AVFrame *in = NULL;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[0], ctx);
    if (s->response)
        FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[1], ctx);
    if (!s->eof_coeffs[s->selir]) {
        ret = check_ir(ctx->inputs[1 + s->selir]);
        if (ret < 0)
            return ret;

        if (ff_outlink_get_status(ctx->inputs[1 + s->selir]) == AVERROR_EOF)
            s->eof_coeffs[s->selir] = 1;

        if (!s->eof_coeffs[s->selir]) {
            if (ff_outlink_frame_wanted(ctx->outputs[0]))
                ff_inlink_request_frame(ctx->inputs[1 + s->selir]);
            else if (s->response && ff_outlink_frame_wanted(ctx->outputs[1]))
                ff_inlink_request_frame(ctx->inputs[1 + s->selir]);
            return 0;
        }
    }

    if (!s->have_coeffs[s->selir] && s->eof_coeffs[s->selir]) {
        ret = convert_coeffs(ctx, s->selir);
        if (ret < 0)
            return ret;
    }

    available = ff_inlink_queued_samples(ctx->inputs[0]);
    wanted = FFMAX(s->min_part_size, (available / s->min_part_size) * s->min_part_size);
    ret = ff_inlink_consume_samples(ctx->inputs[0], wanted, wanted, &in);
    if (ret > 0)
        ret = fir_frame(s, in, outlink);

    if (ret < 0)
        return ret;

    if (s->response && s->have_coeffs[s->selir]) {
        int64_t old_pts = s->video->pts;
        int64_t new_pts = av_rescale_q(s->pts, ctx->inputs[0]->time_base, ctx->outputs[1]->time_base);

        if (ff_outlink_frame_wanted(ctx->outputs[1]) && old_pts < new_pts) {
            AVFrame *clone;
            s->video->pts = new_pts;
            clone = av_frame_clone(s->video);
            if (!clone)
                return AVERROR(ENOMEM);
            return ff_filter_frame(ctx->outputs[1], clone);
        }
    }

    if (ff_inlink_queued_samples(ctx->inputs[0]) >= s->min_part_size) {
        ff_filter_set_ready(ctx, 10);
        return 0;
    }

    if (ff_inlink_acknowledge_status(ctx->inputs[0], &status, &pts)) {
        if (status == AVERROR_EOF) {
            ff_outlink_set_status(ctx->outputs[0], status, pts);
            if (s->response)
                ff_outlink_set_status(ctx->outputs[1], status, pts);
            return 0;
        }
    }

    if (ff_outlink_frame_wanted(ctx->outputs[0]) &&
        !ff_outlink_get_status(ctx->inputs[0])) {
        ff_inlink_request_frame(ctx->inputs[0]);
        return 0;
    }

    if (s->response &&
        ff_outlink_frame_wanted(ctx->outputs[1]) &&
        !ff_outlink_get_status(ctx->inputs[0])) {
        ff_inlink_request_frame(ctx->inputs[0]);
        return 0;
    }

    return FFERROR_NOT_READY;
}

static int query_formats(AVFilterContext *ctx)
{
    AudioFIRContext *s = ctx->priv;
    static const enum AVSampleFormat sample_fmts[3][3] = {
        { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_DBLP, AV_SAMPLE_FMT_NONE },
        { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE },
        { AV_SAMPLE_FMT_DBLP, AV_SAMPLE_FMT_NONE },
    };
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGB0,
        AV_PIX_FMT_NONE
    };
    int ret;

    if (s->response) {
        AVFilterLink *videolink = ctx->outputs[1];
        AVFilterFormats *formats = ff_make_format_list(pix_fmts);
        if ((ret = ff_formats_ref(formats, &videolink->incfg.formats)) < 0)
            return ret;
    }

    if (s->ir_format) {
        ret = ff_set_common_all_channel_counts(ctx);
        if (ret < 0)
            return ret;
    } else {
        AVFilterChannelLayouts *mono = NULL;
        AVFilterChannelLayouts *layouts = ff_all_channel_counts();

        if ((ret = ff_channel_layouts_ref(layouts, &ctx->inputs[0]->outcfg.channel_layouts)) < 0)
            return ret;
        if ((ret = ff_channel_layouts_ref(layouts, &ctx->outputs[0]->incfg.channel_layouts)) < 0)
            return ret;

        ret = ff_add_channel_layout(&mono, &(AVChannelLayout)AV_CHANNEL_LAYOUT_MONO);
        if (ret)
            return ret;
        for (int i = 1; i < ctx->nb_inputs; i++) {
            if ((ret = ff_channel_layouts_ref(mono, &ctx->inputs[i]->outcfg.channel_layouts)) < 0)
                return ret;
        }
    }

    if ((ret = ff_set_common_formats_from_list(ctx, sample_fmts[s->precision])) < 0)
        return ret;

    return ff_set_common_all_samplerates(ctx);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioFIRContext *s = ctx->priv;
    int ret;

    s->one2many = ctx->inputs[1 + s->selir]->ch_layout.nb_channels == 1;
    outlink->sample_rate = ctx->inputs[0]->sample_rate;
    outlink->time_base   = ctx->inputs[0]->time_base;
#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
    outlink->channel_layout = ctx->inputs[0]->channel_layout;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    if ((ret = av_channel_layout_copy(&outlink->ch_layout, &ctx->inputs[0]->ch_layout)) < 0)
        return ret;
    outlink->ch_layout.nb_channels = ctx->inputs[0]->ch_layout.nb_channels;

    s->format = outlink->format;
    s->nb_channels = outlink->ch_layout.nb_channels;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioFIRContext *s = ctx->priv;

    for (int i = 0; i < s->nb_segments; i++)
        uninit_segment(ctx, &s->seg[i]);

    av_freep(&s->fdsp);

    for (int i = 0; i < s->nb_irs; i++) {
        av_frame_free(&s->ir[i]);
        av_frame_free(&s->norm_ir[i]);
    }

    av_frame_free(&s->video);
}

static int config_video(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioFIRContext *s = ctx->priv;

    outlink->sample_aspect_ratio = (AVRational){1,1};
    outlink->w = s->w;
    outlink->h = s->h;
    outlink->frame_rate = s->frame_rate;
    outlink->time_base = av_inv_q(outlink->frame_rate);

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

    s->prev_selir = FFMIN(s->nb_irs - 1, s->selir);

    pad = (AVFilterPad) {
        .name = "main",
        .type = AVMEDIA_TYPE_AUDIO,
    };

    ret = ff_append_inpad(ctx, &pad);
    if (ret < 0)
        return ret;

    for (int n = 0; n < s->nb_irs; n++) {
        pad = (AVFilterPad) {
            .name = av_asprintf("ir%d", n),
            .type = AVMEDIA_TYPE_AUDIO,
        };

        if (!pad.name)
            return AVERROR(ENOMEM);

        ret = ff_append_inpad_free_name(ctx, &pad);
        if (ret < 0)
            return ret;
    }

    pad = (AVFilterPad) {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
    };

    ret = ff_append_outpad(ctx, &pad);
    if (ret < 0)
        return ret;

    if (s->response) {
        vpad = (AVFilterPad){
            .name         = "filter_response",
            .type         = AVMEDIA_TYPE_VIDEO,
            .config_props = config_video,
        };

        ret = ff_append_outpad(ctx, &vpad);
        if (ret < 0)
            return ret;
    }

    s->fdsp = avpriv_float_dsp_alloc(0);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    ff_afir_init(&s->afirdsp);

    return 0;
}

static int process_command(AVFilterContext *ctx,
                           const char *cmd,
                           const char *arg,
                           char *res,
                           int res_len,
                           int flags)
{
    AudioFIRContext *s = ctx->priv;
    int prev_selir, ret;

    prev_selir = s->selir;
    ret = ff_filter_process_command(ctx, cmd, arg, res, res_len, flags);
    if (ret < 0)
        return ret;

    s->selir = FFMIN(s->nb_irs - 1, s->selir);
    if (s->selir != prev_selir) {
        s->prev_selir = prev_selir;
        for (int n = 0; n < s->nb_segments; n++) {
            AudioFIRSegment *seg = &s->seg[n];

            for (int ch = 0; ch < s->nb_channels; ch++)
                seg->loading[ch] = 0;
        }
    }

    return 0;
}

#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define AFR AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
#define VF AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define OFFSET(x) offsetof(AudioFIRContext, x)

static const AVOption afir_options[] = {
    { "dry",    "set dry gain",      OFFSET(dry_gain),   AV_OPT_TYPE_FLOAT, {.dbl=1},    0, 10, AFR },
    { "wet",    "set wet gain",      OFFSET(wet_gain),   AV_OPT_TYPE_FLOAT, {.dbl=1},    0, 10, AFR },
    { "length", "set IR length",     OFFSET(length),     AV_OPT_TYPE_FLOAT, {.dbl=1},    0,  1, AF },
    { "gtype",  "set IR auto gain type",OFFSET(gtype),   AV_OPT_TYPE_INT,   {.i64=0},   -1,  4, AF, "gtype" },
    {  "none",  "without auto gain", 0,                  AV_OPT_TYPE_CONST, {.i64=-1},   0,  0, AF, "gtype" },
    {  "peak",  "peak gain",         0,                  AV_OPT_TYPE_CONST, {.i64=0},    0,  0, AF, "gtype" },
    {  "dc",    "DC gain",           0,                  AV_OPT_TYPE_CONST, {.i64=1},    0,  0, AF, "gtype" },
    {  "gn",    "gain to noise",     0,                  AV_OPT_TYPE_CONST, {.i64=2},    0,  0, AF, "gtype" },
    {  "ac",    "AC gain",           0,                  AV_OPT_TYPE_CONST, {.i64=3},    0,  0, AF, "gtype" },
    {  "rms",   "RMS gain",          0,                  AV_OPT_TYPE_CONST, {.i64=4},    0,  0, AF, "gtype" },
    { "irgain", "set IR gain",       OFFSET(ir_gain),    AV_OPT_TYPE_FLOAT, {.dbl=1},    0,  1, AF },
    { "irfmt",  "set IR format",     OFFSET(ir_format),  AV_OPT_TYPE_INT,   {.i64=1},    0,  1, AF, "irfmt" },
    {  "mono",  "single channel",    0,                  AV_OPT_TYPE_CONST, {.i64=0},    0,  0, AF, "irfmt" },
    {  "input", "same as input",     0,                  AV_OPT_TYPE_CONST, {.i64=1},    0,  0, AF, "irfmt" },
    { "maxir",  "set max IR length", OFFSET(max_ir_len), AV_OPT_TYPE_FLOAT, {.dbl=30}, 0.1, 60, AF },
    { "response", "show IR frequency response", OFFSET(response), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, VF },
    { "channel", "set IR channel to display frequency response", OFFSET(ir_channel), AV_OPT_TYPE_INT, {.i64=0}, 0, 1024, VF },
    { "size",   "set video size",    OFFSET(w),          AV_OPT_TYPE_IMAGE_SIZE, {.str = "hd720"}, 0, 0, VF },
    { "rate",   "set video rate",    OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT32_MAX, VF },
    { "minp",   "set min partition size", OFFSET(minp),  AV_OPT_TYPE_INT,   {.i64=8192}, 1, 65536, AF },
    { "maxp",   "set max partition size", OFFSET(maxp),  AV_OPT_TYPE_INT,   {.i64=8192}, 8, 65536, AF },
    { "nbirs",  "set number of input IRs",OFFSET(nb_irs),AV_OPT_TYPE_INT,   {.i64=1},    1,    32, AF },
    { "ir",     "select IR",              OFFSET(selir), AV_OPT_TYPE_INT,   {.i64=0},    0,    31, AFR },
    { "precision", "set processing precision",    OFFSET(precision), AV_OPT_TYPE_INT,   {.i64=0}, 0, 2, AF, "precision" },
    {  "auto", "set auto processing precision",                   0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, AF, "precision" },
    {  "float", "set single-floating point processing precision", 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, AF, "precision" },
    {  "double","set double-floating point processing precision", 0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, AF, "precision" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(afir);

const AVFilter ff_af_afir = {
    .name          = "afir",
    .description   = NULL_IF_CONFIG_SMALL("Apply Finite Impulse Response filter with supplied coefficients in additional stream(s)."),
    .priv_size     = sizeof(AudioFIRContext),
    .priv_class    = &afir_class,
    FILTER_QUERY_FUNC(query_formats),
    .init          = init,
    .activate      = activate,
    .uninit        = uninit,
    .process_command = process_command,
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS  |
                     AVFILTER_FLAG_DYNAMIC_OUTPUTS |
                     AVFILTER_FLAG_SLICE_THREADS,
};
