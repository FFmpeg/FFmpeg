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

#include "libavutil/avstring.h"
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

static void direct(const float *in, const FFTComplex *ir, int len, float *out)
{
    for (int n = 0; n < len; n++)
        for (int m = 0; m <= n; m++)
            out[n] += ir[m].re * in[n - m];
}

static void fir_fadd(AudioFIRContext *s, float *dst, const float *src, int nb_samples)
{
    if ((nb_samples & 15) == 0 && nb_samples >= 16) {
        s->fdsp->vector_fmac_scalar(dst, src, 1.f, nb_samples);
    } else {
        for (int n = 0; n < nb_samples; n++)
            dst[n] += src[n];
    }
}

static int fir_quantum(AVFilterContext *ctx, AVFrame *out, int ch, int offset)
{
    AudioFIRContext *s = ctx->priv;
    const float *in = (const float *)s->in->extended_data[ch] + offset;
    float *block, *buf, *ptr = (float *)out->extended_data[ch] + offset;
    const int nb_samples = FFMIN(s->min_part_size, out->nb_samples - offset);
    int n, i, j;

    for (int segment = 0; segment < s->nb_segments; segment++) {
        AudioFIRSegment *seg = &s->seg[segment];
        float *src = (float *)seg->input->extended_data[ch];
        float *dst = (float *)seg->output->extended_data[ch];
        float *sum = (float *)seg->sum->extended_data[ch];

        if (s->min_part_size >= 8) {
            s->fdsp->vector_fmul_scalar(src + seg->input_offset, in, s->dry_gain, FFALIGN(nb_samples, 4));
            emms_c();
        } else {
            for (n = 0; n < nb_samples; n++)
                src[seg->input_offset + n] = in[n] * s->dry_gain;
        }

        seg->output_offset[ch] += s->min_part_size;
        if (seg->output_offset[ch] == seg->part_size) {
            seg->output_offset[ch] = 0;
        } else {
            memmove(src, src + s->min_part_size, (seg->input_size - s->min_part_size) * sizeof(*src));

            dst += seg->output_offset[ch];
            fir_fadd(s, ptr, dst, nb_samples);
            continue;
        }

        if (seg->part_size < 8) {
            memset(dst, 0, sizeof(*dst) * seg->part_size * seg->nb_partitions);

            j = seg->part_index[ch];

            for (i = 0; i < seg->nb_partitions; i++) {
                const int coffset = j * seg->coeff_size;
                const FFTComplex *coeff = (const FFTComplex *)seg->coeff->extended_data[ch * !s->one2many] + coffset;

                direct(src, coeff, nb_samples, dst);

                if (j == 0)
                    j = seg->nb_partitions;
                j--;
            }

            seg->part_index[ch] = (seg->part_index[ch] + 1) % seg->nb_partitions;

            memmove(src, src + s->min_part_size, (seg->input_size - s->min_part_size) * sizeof(*src));

            for (n = 0; n < nb_samples; n++) {
                ptr[n] += dst[n];
            }
            continue;
        }

        memset(sum, 0, sizeof(*sum) * seg->fft_length);
        block = (float *)seg->block->extended_data[ch] + seg->part_index[ch] * seg->block_size;
        memset(block + seg->part_size, 0, sizeof(*block) * (seg->fft_length - seg->part_size));

        memcpy(block, src, sizeof(*src) * seg->part_size);

        av_rdft_calc(seg->rdft[ch], block);
        block[2 * seg->part_size] = block[1];
        block[1] = 0;

        j = seg->part_index[ch];

        for (i = 0; i < seg->nb_partitions; i++) {
            const int coffset = j * seg->coeff_size;
            const float *block = (const float *)seg->block->extended_data[ch] + i * seg->block_size;
            const FFTComplex *coeff = (const FFTComplex *)seg->coeff->extended_data[ch * !s->one2many] + coffset;

            s->afirdsp.fcmul_add(sum, block, (const float *)coeff, seg->part_size);

            if (j == 0)
                j = seg->nb_partitions;
            j--;
        }

        sum[1] = sum[2 * seg->part_size];
        av_rdft_calc(seg->irdft[ch], sum);

        buf = (float *)seg->buffer->extended_data[ch];
        fir_fadd(s, buf, sum, seg->part_size);

        memcpy(dst, buf, seg->part_size * sizeof(*dst));

        buf = (float *)seg->buffer->extended_data[ch];
        memcpy(buf, sum + seg->part_size, seg->part_size * sizeof(*buf));

        seg->part_index[ch] = (seg->part_index[ch] + 1) % seg->nb_partitions;

        memmove(src, src + s->min_part_size, (seg->input_size - s->min_part_size) * sizeof(*src));

        fir_fadd(s, ptr, dst, nb_samples);
    }

    if (s->min_part_size >= 8) {
        s->fdsp->vector_fmul_scalar(ptr, ptr, s->wet_gain, FFALIGN(nb_samples, 4));
        emms_c();
    } else {
        for (n = 0; n < nb_samples; n++)
            ptr[n] *= s->wet_gain;
    }

    return 0;
}

static int fir_channel(AVFilterContext *ctx, AVFrame *out, int ch)
{
    AudioFIRContext *s = ctx->priv;

    for (int offset = 0; offset < out->nb_samples; offset += s->min_part_size) {
        fir_quantum(ctx, out, ch, offset);
    }

    return 0;
}

static int fir_channels(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AVFrame *out = arg;
    const int start = (out->channels * jobnr) / nb_jobs;
    const int end = (out->channels * (jobnr+1)) / nb_jobs;

    for (int ch = start; ch < end; ch++) {
        fir_channel(ctx, out, ch);
    }

    return 0;
}

static int fir_frame(AudioFIRContext *s, AVFrame *in, AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFrame *out = NULL;

    out = ff_get_audio_buffer(outlink, in->nb_samples);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    if (s->pts == AV_NOPTS_VALUE)
        s->pts = in->pts;
    s->in = in;
    ctx->internal->execute(ctx, fir_channels, out, NULL, FFMIN(outlink->channels,
                                                               ff_filter_get_nb_threads(ctx)));

    out->pts = s->pts;
    if (s->pts != AV_NOPTS_VALUE)
        s->pts += av_rescale_q(out->nb_samples, (AVRational){1, outlink->sample_rate}, outlink->time_base);

    av_frame_free(&in);
    s->in = NULL;

    return ff_filter_frame(outlink, out);
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
    float *mag, *phase, *delay, min = FLT_MAX, max = FLT_MIN;
    float min_delay = FLT_MAX, max_delay = FLT_MIN;
    int prev_ymag = -1, prev_yphase = -1, prev_ydelay = -1;
    char text[32];
    int channel, i, x;

    memset(out->data[0], 0, s->h * out->linesize[0]);

    phase = av_malloc_array(s->w, sizeof(*phase));
    mag = av_malloc_array(s->w, sizeof(*mag));
    delay = av_malloc_array(s->w, sizeof(*delay));
    if (!mag || !phase || !delay)
        goto end;

    channel = av_clip(s->ir_channel, 0, s->ir[s->selir]->channels - 1);
    for (i = 0; i < s->w; i++) {
        const float *src = (const float *)s->ir[s->selir]->extended_data[channel];
        double w = i * M_PI / (s->w - 1);
        double div, real_num = 0., imag_num = 0., real = 0., imag = 0.;

        for (x = 0; x < s->nb_taps; x++) {
            real += cos(-x * w) * src[x];
            imag += sin(-x * w) * src[x];
            real_num += cos(-x * w) * src[x] * x;
            imag_num += sin(-x * w) * src[x] * x;
        }

        mag[i] = hypot(real, imag);
        phase[i] = atan2(imag, real);
        div = real * real + imag * imag;
        delay[i] = (real_num * real + imag_num * imag) / div;
        min = fminf(min, mag[i]);
        max = fmaxf(max, mag[i]);
        min_delay = fminf(min_delay, delay[i]);
        max_delay = fmaxf(max_delay, delay[i]);
    }

    for (i = 0; i < s->w; i++) {
        int ymag = mag[i] / max * (s->h - 1);
        int ydelay = (delay[i] - min_delay) / (max_delay - min_delay) * (s->h - 1);
        int yphase = (0.5 * (1. + phase[i] / M_PI)) * (s->h - 1);

        ymag = s->h - 1 - av_clip(ymag, 0, s->h - 1);
        yphase = s->h - 1 - av_clip(yphase, 0, s->h - 1);
        ydelay = s->h - 1 - av_clip(ydelay, 0, s->h - 1);

        if (prev_ymag < 0)
            prev_ymag = ymag;
        if (prev_yphase < 0)
            prev_yphase = yphase;
        if (prev_ydelay < 0)
            prev_ydelay = ydelay;

        draw_line(out, i,   ymag, FFMAX(i - 1, 0),   prev_ymag, 0xFFFF00FF);
        draw_line(out, i, yphase, FFMAX(i - 1, 0), prev_yphase, 0xFF00FF00);
        draw_line(out, i, ydelay, FFMAX(i - 1, 0), prev_ydelay, 0xFF00FFFF);

        prev_ymag   = ymag;
        prev_yphase = yphase;
        prev_ydelay = ydelay;
    }

    if (s->w > 400 && s->h > 100) {
        drawtext(out, 2, 2, "Max Magnitude:", 0xDDDDDDDD);
        snprintf(text, sizeof(text), "%.2f", max);
        drawtext(out, 15 * 8 + 2, 2, text, 0xDDDDDDDD);

        drawtext(out, 2, 12, "Min Magnitude:", 0xDDDDDDDD);
        snprintf(text, sizeof(text), "%.2f", min);
        drawtext(out, 15 * 8 + 2, 12, text, 0xDDDDDDDD);

        drawtext(out, 2, 22, "Max Delay:", 0xDDDDDDDD);
        snprintf(text, sizeof(text), "%.2f", max_delay);
        drawtext(out, 11 * 8 + 2, 22, text, 0xDDDDDDDD);

        drawtext(out, 2, 32, "Min Delay:", 0xDDDDDDDD);
        snprintf(text, sizeof(text), "%.2f", min_delay);
        drawtext(out, 11 * 8 + 2, 32, text, 0xDDDDDDDD);
    }

end:
    av_free(delay);
    av_free(phase);
    av_free(mag);
}

static int init_segment(AVFilterContext *ctx, AudioFIRSegment *seg,
                        int offset, int nb_partitions, int part_size)
{
    AudioFIRContext *s = ctx->priv;

    seg->rdft  = av_calloc(ctx->inputs[0]->channels, sizeof(*seg->rdft));
    seg->irdft = av_calloc(ctx->inputs[0]->channels, sizeof(*seg->irdft));
    if (!seg->rdft || !seg->irdft)
        return AVERROR(ENOMEM);

    seg->fft_length    = part_size * 2 + 1;
    seg->part_size     = part_size;
    seg->block_size    = FFALIGN(seg->fft_length, 32);
    seg->coeff_size    = FFALIGN(seg->part_size + 1, 32);
    seg->nb_partitions = nb_partitions;
    seg->input_size    = offset + s->min_part_size;
    seg->input_offset  = offset;

    seg->part_index    = av_calloc(ctx->inputs[0]->channels, sizeof(*seg->part_index));
    seg->output_offset = av_calloc(ctx->inputs[0]->channels, sizeof(*seg->output_offset));
    if (!seg->part_index || !seg->output_offset)
        return AVERROR(ENOMEM);

    for (int ch = 0; ch < ctx->inputs[0]->channels && part_size >= 8; ch++) {
        seg->rdft[ch]  = av_rdft_init(av_log2(2 * part_size), DFT_R2C);
        seg->irdft[ch] = av_rdft_init(av_log2(2 * part_size), IDFT_C2R);
        if (!seg->rdft[ch] || !seg->irdft[ch])
            return AVERROR(ENOMEM);
    }

    seg->sum    = ff_get_audio_buffer(ctx->inputs[0], seg->fft_length);
    seg->block  = ff_get_audio_buffer(ctx->inputs[0], seg->nb_partitions * seg->block_size);
    seg->buffer = ff_get_audio_buffer(ctx->inputs[0], seg->part_size);
    seg->coeff  = ff_get_audio_buffer(ctx->inputs[1 + s->selir], seg->nb_partitions * seg->coeff_size * 2);
    seg->input  = ff_get_audio_buffer(ctx->inputs[0], seg->input_size);
    seg->output = ff_get_audio_buffer(ctx->inputs[0], seg->part_size);
    if (!seg->buffer || !seg->sum || !seg->block || !seg->coeff || !seg->input || !seg->output)
        return AVERROR(ENOMEM);

    return 0;
}

static void uninit_segment(AVFilterContext *ctx, AudioFIRSegment *seg)
{
    AudioFIRContext *s = ctx->priv;

    if (seg->rdft) {
        for (int ch = 0; ch < s->nb_channels; ch++) {
            av_rdft_end(seg->rdft[ch]);
        }
    }
    av_freep(&seg->rdft);

    if (seg->irdft) {
        for (int ch = 0; ch < s->nb_channels; ch++) {
            av_rdft_end(seg->irdft[ch]);
        }
    }
    av_freep(&seg->irdft);

    av_freep(&seg->output_offset);
    av_freep(&seg->part_index);

    av_frame_free(&seg->block);
    av_frame_free(&seg->sum);
    av_frame_free(&seg->buffer);
    av_frame_free(&seg->coeff);
    av_frame_free(&seg->input);
    av_frame_free(&seg->output);
    seg->input_size = 0;
}

static int convert_coeffs(AVFilterContext *ctx)
{
    AudioFIRContext *s = ctx->priv;
    int ret, i, ch, n, cur_nb_taps;
    float power = 0;

    if (!s->nb_taps) {
        int part_size, max_part_size;
        int left, offset = 0;

        s->nb_taps = ff_inlink_queued_samples(ctx->inputs[1 + s->selir]);
        if (s->nb_taps <= 0)
            return AVERROR(EINVAL);

        if (s->minp > s->maxp) {
            s->maxp = s->minp;
        }

        left = s->nb_taps;
        part_size = 1 << av_log2(s->minp);
        max_part_size = 1 << av_log2(s->maxp);

        s->min_part_size = part_size;

        for (i = 0; left > 0; i++) {
            int step = part_size == max_part_size ? INT_MAX : 1 + (i == 0);
            int nb_partitions = FFMIN(step, (left + part_size - 1) / part_size);

            s->nb_segments = i + 1;
            ret = init_segment(ctx, &s->seg[i], offset, nb_partitions, part_size);
            if (ret < 0)
                return ret;
            offset += nb_partitions * part_size;
            left -= nb_partitions * part_size;
            part_size *= 2;
            part_size = FFMIN(part_size, max_part_size);
        }
    }

    if (!s->ir[s->selir]) {
        ret = ff_inlink_consume_samples(ctx->inputs[1 + s->selir], s->nb_taps, s->nb_taps, &s->ir[s->selir]);
        if (ret < 0)
            return ret;
        if (ret == 0)
            return AVERROR_BUG;
    }

    if (s->response)
        draw_response(ctx, s->video);

    s->gain = 1;
    cur_nb_taps = s->ir[s->selir]->nb_samples;

    switch (s->gtype) {
    case -1:
        /* nothing to do */
        break;
    case 0:
        for (ch = 0; ch < ctx->inputs[1 + s->selir]->channels; ch++) {
            float *time = (float *)s->ir[s->selir]->extended_data[!s->one2many * ch];

            for (i = 0; i < cur_nb_taps; i++)
                power += FFABS(time[i]);
        }
        s->gain = ctx->inputs[1 + s->selir]->channels / power;
        break;
    case 1:
        for (ch = 0; ch < ctx->inputs[1 + s->selir]->channels; ch++) {
            float *time = (float *)s->ir[s->selir]->extended_data[!s->one2many * ch];

            for (i = 0; i < cur_nb_taps; i++)
                power += time[i];
        }
        s->gain = ctx->inputs[1 + s->selir]->channels / power;
        break;
    case 2:
        for (ch = 0; ch < ctx->inputs[1 + s->selir]->channels; ch++) {
            float *time = (float *)s->ir[s->selir]->extended_data[!s->one2many * ch];

            for (i = 0; i < cur_nb_taps; i++)
                power += time[i] * time[i];
        }
        s->gain = sqrtf(ch / power);
        break;
    default:
        return AVERROR_BUG;
    }

    s->gain = FFMIN(s->gain * s->ir_gain, 1.f);
    av_log(ctx, AV_LOG_DEBUG, "power %f, gain %f\n", power, s->gain);
    for (ch = 0; ch < ctx->inputs[1 + s->selir]->channels; ch++) {
        float *time = (float *)s->ir[s->selir]->extended_data[!s->one2many * ch];

        s->fdsp->vector_fmul_scalar(time, time, s->gain, FFALIGN(cur_nb_taps, 4));
    }

    av_log(ctx, AV_LOG_DEBUG, "nb_taps: %d\n", cur_nb_taps);
    av_log(ctx, AV_LOG_DEBUG, "nb_segments: %d\n", s->nb_segments);

    for (ch = 0; ch < ctx->inputs[1 + s->selir]->channels; ch++) {
        float *time = (float *)s->ir[s->selir]->extended_data[!s->one2many * ch];
        int toffset = 0;

        for (i = FFMAX(1, s->length * s->nb_taps); i < s->nb_taps; i++)
            time[i] = 0;

        av_log(ctx, AV_LOG_DEBUG, "channel: %d\n", ch);

        for (int segment = 0; segment < s->nb_segments; segment++) {
            AudioFIRSegment *seg = &s->seg[segment];
            float *block = (float *)seg->block->extended_data[ch];
            FFTComplex *coeff = (FFTComplex *)seg->coeff->extended_data[ch];

            av_log(ctx, AV_LOG_DEBUG, "segment: %d\n", segment);

            for (i = 0; i < seg->nb_partitions; i++) {
                const float scale = 1.f / seg->part_size;
                const int coffset = i * seg->coeff_size;
                const int remaining = s->nb_taps - toffset;
                const int size = remaining >= seg->part_size ? seg->part_size : remaining;

                if (size < 8) {
                    for (n = 0; n < size; n++)
                        coeff[coffset + n].re = time[toffset + n];

                    toffset += size;
                    continue;
                }

                memset(block, 0, sizeof(*block) * seg->fft_length);
                memcpy(block, time + toffset, size * sizeof(*block));

                av_rdft_calc(seg->rdft[0], block);

                coeff[coffset].re = block[0] * scale;
                coeff[coffset].im = 0;
                for (n = 1; n < seg->part_size; n++) {
                    coeff[coffset + n].re = block[2 * n] * scale;
                    coeff[coffset + n].im = block[2 * n + 1] * scale;
                }
                coeff[coffset + seg->part_size].re = block[1] * scale;
                coeff[coffset + seg->part_size].im = 0;

                toffset += size;
            }

            av_log(ctx, AV_LOG_DEBUG, "nb_partitions: %d\n", seg->nb_partitions);
            av_log(ctx, AV_LOG_DEBUG, "partition size: %d\n", seg->part_size);
            av_log(ctx, AV_LOG_DEBUG, "block size: %d\n", seg->block_size);
            av_log(ctx, AV_LOG_DEBUG, "fft_length: %d\n", seg->fft_length);
            av_log(ctx, AV_LOG_DEBUG, "coeff_size: %d\n", seg->coeff_size);
            av_log(ctx, AV_LOG_DEBUG, "input_size: %d\n", seg->input_size);
            av_log(ctx, AV_LOG_DEBUG, "input_offset: %d\n", seg->input_offset);
        }
    }

    s->have_coeffs = 1;

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

    if (!s->have_coeffs && s->eof_coeffs[s->selir]) {
        ret = convert_coeffs(ctx);
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

    if (s->response && s->have_coeffs) {
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
        if ((ret = ff_formats_ref(formats, &videolink->incfg.formats)) < 0)
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

        if ((ret = ff_channel_layouts_ref(layouts, &ctx->inputs[0]->outcfg.channel_layouts)) < 0)
            return ret;
        if ((ret = ff_channel_layouts_ref(layouts, &ctx->outputs[0]->incfg.channel_layouts)) < 0)
            return ret;

        ret = ff_add_channel_layout(&mono, AV_CH_LAYOUT_MONO);
        if (ret)
            return ret;
        for (int i = 1; i < ctx->nb_inputs; i++) {
            if ((ret = ff_channel_layouts_ref(mono, &ctx->inputs[i]->outcfg.channel_layouts)) < 0)
                return ret;
        }
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

    s->one2many = ctx->inputs[1 + s->selir]->channels == 1;
    outlink->sample_rate = ctx->inputs[0]->sample_rate;
    outlink->time_base   = ctx->inputs[0]->time_base;
    outlink->channel_layout = ctx->inputs[0]->channel_layout;
    outlink->channels = ctx->inputs[0]->channels;

    s->nb_channels = outlink->channels;
    s->nb_coef_channels = ctx->inputs[1 + s->selir]->channels;
    s->pts = AV_NOPTS_VALUE;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioFIRContext *s = ctx->priv;

    for (int i = 0; i < s->nb_segments; i++) {
        uninit_segment(ctx, &s->seg[i]);
    }

    av_freep(&s->fdsp);

    for (int i = 0; i < s->nb_irs; i++) {
        av_frame_free(&s->ir[i]);
    }

    for (unsigned i = 1; i < ctx->nb_inputs; i++)
        av_freep(&ctx->input_pads[i].name);

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

void ff_afir_init(AudioFIRDSPContext *dsp)
{
    dsp->fcmul_add = fcmul_add_c;

    if (ARCH_X86)
        ff_afir_init_x86(dsp);
}

static av_cold int init(AVFilterContext *ctx)
{
    AudioFIRContext *s = ctx->priv;
    AVFilterPad pad, vpad;
    int ret;

    pad = (AVFilterPad) {
        .name = "main",
        .type = AVMEDIA_TYPE_AUDIO,
    };

    ret = ff_insert_inpad(ctx, 0, &pad);
    if (ret < 0)
        return ret;

    for (int n = 0; n < s->nb_irs; n++) {
        pad = (AVFilterPad) {
            .name = av_asprintf("ir%d", n),
            .type = AVMEDIA_TYPE_AUDIO,
        };

        if (!pad.name)
            return AVERROR(ENOMEM);

        ret = ff_insert_inpad(ctx, n + 1, &pad);
        if (ret < 0) {
            av_freep(&pad.name);
            return ret;
        }
    }

    pad = (AVFilterPad) {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
    };

    ret = ff_insert_outpad(ctx, 0, &pad);
    if (ret < 0)
        return ret;

    if (s->response) {
        vpad = (AVFilterPad){
            .name         = "filter_response",
            .type         = AVMEDIA_TYPE_VIDEO,
            .config_props = config_video,
        };

        ret = ff_insert_outpad(ctx, 1, &vpad);
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
    int prev_ir = s->selir;
    int ret = ff_filter_process_command(ctx, cmd, arg, res, res_len, flags);

    if (ret < 0)
        return ret;

    s->selir = FFMIN(s->nb_irs - 1, s->selir);

    if (prev_ir != s->selir) {
        s->have_coeffs = 0;
    }

    return 0;
}

#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define AFR AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
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
    { "rate",   "set video rate",    OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT32_MAX, VF },
    { "minp",   "set min partition size", OFFSET(minp),  AV_OPT_TYPE_INT,   {.i64=8192}, 1, 32768, AF },
    { "maxp",   "set max partition size", OFFSET(maxp),  AV_OPT_TYPE_INT,   {.i64=8192}, 8, 32768, AF },
    { "nbirs",  "set number of input IRs",OFFSET(nb_irs),AV_OPT_TYPE_INT,   {.i64=1},    1,    32, AF },
    { "ir",     "select IR",              OFFSET(selir), AV_OPT_TYPE_INT,   {.i64=0},    0,    31, AFR },
    { NULL }
};

AVFILTER_DEFINE_CLASS(afir);

AVFilter ff_af_afir = {
    .name          = "afir",
    .description   = NULL_IF_CONFIG_SMALL("Apply Finite Impulse Response filter with supplied coefficients in additional stream(s)."),
    .priv_size     = sizeof(AudioFIRContext),
    .priv_class    = &afir_class,
    .query_formats = query_formats,
    .init          = init,
    .activate      = activate,
    .uninit        = uninit,
    .process_command = process_command,
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS  |
                     AVFILTER_FLAG_DYNAMIC_OUTPUTS |
                     AVFILTER_FLAG_SLICE_THREADS,
};
