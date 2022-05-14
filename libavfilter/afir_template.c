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

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "audio.h"

#undef ctype
#undef ftype
#undef SQRT
#undef SAMPLE_FORMAT
#if DEPTH == 32
#define SAMPLE_FORMAT float
#define SQRT sqrtf
#define ctype AVComplexFloat
#define ftype float
#else
#define SAMPLE_FORMAT double
#define SQRT sqrt
#define ctype AVComplexDouble
#define ftype double
#endif

#define fn3(a,b)   a##_##b
#define fn2(a,b)   fn3(a,b)
#define fn(a)      fn2(a, SAMPLE_FORMAT)

static void fn(draw_response)(AVFilterContext *ctx, AVFrame *out)
{
    AudioFIRContext *s = ctx->priv;
    ftype *mag, *phase, *delay, min = FLT_MAX, max = FLT_MIN;
    ftype min_delay = FLT_MAX, max_delay = FLT_MIN;
    int prev_ymag = -1, prev_yphase = -1, prev_ydelay = -1;
    char text[32];
    int channel, i, x;

    memset(out->data[0], 0, s->h * out->linesize[0]);

    phase = av_malloc_array(s->w, sizeof(*phase));
    mag = av_malloc_array(s->w, sizeof(*mag));
    delay = av_malloc_array(s->w, sizeof(*delay));
    if (!mag || !phase || !delay)
        goto end;

    channel = av_clip(s->ir_channel, 0, s->ir[s->selir]->ch_layout.nb_channels - 1);
    for (i = 0; i < s->w; i++) {
        const ftype *src = (const ftype *)s->ir[s->selir]->extended_data[channel];
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

static void fn(convert_channels)(AVFilterContext *ctx, AudioFIRContext *s)
{
    for (int ch = 0; ch < ctx->inputs[1 + s->selir]->ch_layout.nb_channels; ch++) {
        ftype *time = (ftype *)s->ir[s->selir]->extended_data[!s->one2many * ch];
        int toffset = 0;

        for (int i = FFMAX(1, s->length * s->nb_taps); i < s->nb_taps; i++)
            time[i] = 0;

        av_log(ctx, AV_LOG_DEBUG, "channel: %d\n", ch);

        for (int segment = 0; segment < s->nb_segments; segment++) {
            AudioFIRSegment *seg = &s->seg[segment];
            ftype *blockin = (ftype *)seg->blockin->extended_data[ch];
            ftype *blockout = (ftype *)seg->blockout->extended_data[ch];
            ctype *coeff = (ctype *)seg->coeff->extended_data[ch];

            av_log(ctx, AV_LOG_DEBUG, "segment: %d\n", segment);

            for (int i = 0; i < seg->nb_partitions; i++) {
                const int coffset = i * seg->coeff_size;
                const int remaining = s->nb_taps - toffset;
                const int size = remaining >= seg->part_size ? seg->part_size : remaining;

                if (size < 8) {
                    for (int n = 0; n < size; n++)
                        coeff[coffset + n].re = time[toffset + n];

                    toffset += size;
                    continue;
                }

                memset(blockin, 0, sizeof(*blockin) * seg->fft_length);
                memcpy(blockin, time + toffset, size * sizeof(*blockin));

                seg->tx_fn(seg->tx[0], blockout, blockin, sizeof(ftype));

                for (int n = 0; n < seg->part_size + 1; n++) {
                    coeff[coffset + n].re = blockout[2 * n];
                    coeff[coffset + n].im = blockout[2 * n + 1];
                }

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
}

static int fn(get_power)(AVFilterContext *ctx, AudioFIRContext *s, int cur_nb_taps)
{
    ftype power = 0;
    int ch;

    switch (s->gtype) {
    case -1:
        /* nothing to do */
        break;
    case 0:
        for (ch = 0; ch < ctx->inputs[1 + s->selir]->ch_layout.nb_channels; ch++) {
            ftype *time = (ftype *)s->ir[s->selir]->extended_data[!s->one2many * ch];

            for (int i = 0; i < cur_nb_taps; i++)
                power += FFABS(time[i]);
        }
        s->gain = ctx->inputs[1 + s->selir]->ch_layout.nb_channels / power;
        break;
    case 1:
        for (ch = 0; ch < ctx->inputs[1 + s->selir]->ch_layout.nb_channels; ch++) {
            ftype *time = (ftype *)s->ir[s->selir]->extended_data[!s->one2many * ch];

            for (int i = 0; i < cur_nb_taps; i++)
                power += time[i];
        }
        s->gain = ctx->inputs[1 + s->selir]->ch_layout.nb_channels / power;
        break;
    case 2:
        for (ch = 0; ch < ctx->inputs[1 + s->selir]->ch_layout.nb_channels; ch++) {
            ftype *time = (ftype *)s->ir[s->selir]->extended_data[!s->one2many * ch];

            for (int i = 0; i < cur_nb_taps; i++)
                power += time[i] * time[i];
        }
        s->gain = SQRT(ch / power);
        break;
    default:
        return AVERROR_BUG;
    }

    s->gain = FFMIN(s->gain * s->ir_gain, 1.);

    av_log(ctx, AV_LOG_DEBUG, "power %f, gain %f\n", power, s->gain);

    for (int ch = 0; ch < ctx->inputs[1 + s->selir]->ch_layout.nb_channels; ch++) {
        ftype *time = (ftype *)s->ir[s->selir]->extended_data[!s->one2many * ch];

#if DEPTH == 32
        s->fdsp->vector_fmul_scalar(time, time, s->gain, FFALIGN(cur_nb_taps, 4));
#else
        s->fdsp->vector_dmul_scalar(time, time, s->gain, FFALIGN(cur_nb_taps, 8));
#endif
    }

    return 0;
}

static void fn(direct)(const ftype *in, const ctype *ir, int len, ftype *out)
{
    for (int n = 0; n < len; n++)
        for (int m = 0; m <= n; m++)
            out[n] += ir[m].re * in[n - m];
}

static void fn(fir_fadd)(AudioFIRContext *s, ftype *dst, const ftype *src, int nb_samples)
{
    if ((nb_samples & 15) == 0 && nb_samples >= 16) {
#if DEPTH == 32
        s->fdsp->vector_fmac_scalar(dst, src, 1.f, nb_samples);
#else
        s->fdsp->vector_dmac_scalar(dst, src, 1.0, nb_samples);
#endif
    } else {
        for (int n = 0; n < nb_samples; n++)
            dst[n] += src[n];
    }
}

static int fn(fir_quantum)(AVFilterContext *ctx, AVFrame *out, int ch, int offset)
{
    AudioFIRContext *s = ctx->priv;
    const ftype *in = (const ftype *)s->in->extended_data[ch] + offset;
    ftype *blockin, *blockout, *buf, *ptr = (ftype *)out->extended_data[ch] + offset;
    const int nb_samples = FFMIN(s->min_part_size, out->nb_samples - offset);
    int n, i, j;

    for (int segment = 0; segment < s->nb_segments; segment++) {
        AudioFIRSegment *seg = &s->seg[segment];
        ftype *src = (ftype *)seg->input->extended_data[ch];
        ftype *dst = (ftype *)seg->output->extended_data[ch];
        ftype *sumin = (ftype *)seg->sumin->extended_data[ch];
        ftype *sumout = (ftype *)seg->sumout->extended_data[ch];

        if (s->min_part_size >= 8) {
#if DEPTH == 32
            s->fdsp->vector_fmul_scalar(src + seg->input_offset, in, s->dry_gain, FFALIGN(nb_samples, 4));
#else
            s->fdsp->vector_dmul_scalar(src + seg->input_offset, in, s->dry_gain, FFALIGN(nb_samples, 8));
#endif
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
            fn(fir_fadd)(s, ptr, dst, nb_samples);
            continue;
        }

        if (seg->part_size < 8) {
            memset(dst, 0, sizeof(*dst) * seg->part_size * seg->nb_partitions);

            j = seg->part_index[ch];

            for (i = 0; i < seg->nb_partitions; i++) {
                const int coffset = j * seg->coeff_size;
                const ctype *coeff = (const ctype *)seg->coeff->extended_data[ch * !s->one2many] + coffset;

                fn(direct)(src, coeff, nb_samples, dst);

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

        memset(sumin, 0, sizeof(*sumin) * seg->fft_length);
        blockin = (ftype *)seg->blockin->extended_data[ch] + seg->part_index[ch] * seg->block_size;
        blockout = (ftype *)seg->blockout->extended_data[ch] + seg->part_index[ch] * seg->block_size;
        memset(blockin + seg->part_size, 0, sizeof(*blockin) * (seg->fft_length - seg->part_size));

        memcpy(blockin, src, sizeof(*src) * seg->part_size);

        seg->tx_fn(seg->tx[ch], blockout, blockin, sizeof(ftype));

        j = seg->part_index[ch];

        for (i = 0; i < seg->nb_partitions; i++) {
            const int coffset = j * seg->coeff_size;
            const ftype *blockout = (const ftype *)seg->blockout->extended_data[ch] + i * seg->block_size;
            const ctype *coeff = (const ctype *)seg->coeff->extended_data[ch * !s->one2many] + coffset;

#if DEPTH == 32
            s->afirdsp.fcmul_add(sumin, blockout, (const ftype *)coeff, seg->part_size);
#else
            s->afirdsp.dcmul_add(sumin, blockout, (const ftype *)coeff, seg->part_size);
#endif

            if (j == 0)
                j = seg->nb_partitions;
            j--;
        }

        seg->itx_fn(seg->itx[ch], sumout, sumin, sizeof(ftype));

        buf = (ftype *)seg->buffer->extended_data[ch];
        fn(fir_fadd)(s, buf, sumout, seg->part_size);

        memcpy(dst, buf, seg->part_size * sizeof(*dst));

        buf = (ftype *)seg->buffer->extended_data[ch];
        memcpy(buf, sumout + seg->part_size, seg->part_size * sizeof(*buf));

        seg->part_index[ch] = (seg->part_index[ch] + 1) % seg->nb_partitions;

        memmove(src, src + s->min_part_size, (seg->input_size - s->min_part_size) * sizeof(*src));

        fn(fir_fadd)(s, ptr, dst, nb_samples);
    }

    if (s->min_part_size >= 8) {
#if DEPTH == 32
        s->fdsp->vector_fmul_scalar(ptr, ptr, s->wet_gain, FFALIGN(nb_samples, 4));
#else
        s->fdsp->vector_dmul_scalar(ptr, ptr, s->wet_gain, FFALIGN(nb_samples, 8));
#endif
        emms_c();
    } else {
        for (n = 0; n < nb_samples; n++)
            ptr[n] *= s->wet_gain;
    }

    return 0;
}


