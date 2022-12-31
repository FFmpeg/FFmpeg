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

#include "libavutil/tx.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "audio.h"

#undef ctype
#undef ftype
#undef SQRT
#undef HYPOT
#undef SAMPLE_FORMAT
#undef TX_TYPE
#if DEPTH == 32
#define SAMPLE_FORMAT float
#define SQRT sqrtf
#define HYPOT hypotf
#define ctype AVComplexFloat
#define ftype float
#define TX_TYPE AV_TX_FLOAT_RDFT
#else
#define SAMPLE_FORMAT double
#define SQRT sqrt
#define HYPOT hypot
#define ctype AVComplexDouble
#define ftype double
#define TX_TYPE AV_TX_DOUBLE_RDFT
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

    for (int y = 0; y < s->h; y++)
        memset(out->data[0] + y * out->linesize[0], 0, s->w * 4);

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

        for (x = 0; x < s->nb_taps[s->selir]; x++) {
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

static int fn(get_power)(AVFilterContext *ctx, AudioFIRContext *s,
                         int cur_nb_taps, int ch,
                         ftype *time)
{
    ftype ch_gain = 1;

    switch (s->gtype) {
    case -1:
        ch_gain = 1;
        break;
    case 0:
        {
            ftype sum = 0;

            for (int i = 0; i < cur_nb_taps; i++)
                sum += FFABS(time[i]);
            ch_gain = 1. / sum;
        }
        break;
    case 1:
        {
            ftype sum = 0;

            for (int i = 0; i < cur_nb_taps; i++)
                sum += time[i];
            ch_gain = 1. / sum;
        }
        break;
    case 2:
        {
            ftype sum = 0;

            for (int i = 0; i < cur_nb_taps; i++)
                sum += time[i] * time[i];
            ch_gain = 1. / SQRT(sum);
        }
        break;
    case 3:
    case 4:
        {
            ftype *inc, *outc, scale, power;
            AVTXContext *tx;
            av_tx_fn tx_fn;
            int ret, size;

            size = 1 << av_ceil_log2_c(cur_nb_taps);
            inc = av_calloc(size + 2, sizeof(SAMPLE_FORMAT));
            outc = av_calloc(size + 2, sizeof(SAMPLE_FORMAT));
            if (!inc || !outc) {
                av_free(outc);
                av_free(inc);
                break;
            }

            scale = 1.;
            ret = av_tx_init(&tx, &tx_fn, TX_TYPE, 0, size, &scale, 0);
            if (ret < 0) {
                av_free(outc);
                av_free(inc);
                break;
            }

            {
                memcpy(inc, time, cur_nb_taps * sizeof(SAMPLE_FORMAT));
                tx_fn(tx, outc, inc, sizeof(SAMPLE_FORMAT));

                power = 0;
                if (s->gtype == 3) {
                    for (int i = 0; i < size / 2 + 1; i++)
                        power = FFMAX(power, HYPOT(outc[i * 2], outc[i * 2 + 1]));
                } else {
                    ftype sum = 0;
                    for (int i = 0; i < size / 2 + 1; i++)
                        sum += HYPOT(outc[i * 2], outc[i * 2 + 1]);
                    power = SQRT(sum / (size / 2 + 1));
                }

                ch_gain = 1. / power;
            }

            av_tx_uninit(&tx);
            av_free(outc);
            av_free(inc);
        }
        break;
    default:
        return AVERROR_BUG;
    }

    if (ch_gain != 1. || s->ir_gain != 1.) {
        ftype gain = ch_gain * s->ir_gain;

        av_log(ctx, AV_LOG_DEBUG, "ch%d gain %f\n", ch, gain);
#if DEPTH == 32
        s->fdsp->vector_fmul_scalar(time, time, gain, FFALIGN(cur_nb_taps, 4));
#else
        s->fdsp->vector_dmul_scalar(time, time, gain, FFALIGN(cur_nb_taps, 8));
#endif
    }

    return 0;
}

static void fn(convert_channel)(AVFilterContext *ctx, AudioFIRContext *s, int ch,
                                AudioFIRSegment *seg, int coeff_partition, int selir)
{
    const int coffset = coeff_partition * seg->coeff_size;
    const int nb_taps = s->nb_taps[selir];
    ftype *time = (ftype *)s->norm_ir[selir]->extended_data[ch];
    ftype *tempin = (ftype *)seg->tempin->extended_data[ch];
    ftype *tempout = (ftype *)seg->tempout->extended_data[ch];
    ctype *coeff = (ctype *)seg->coeff[selir]->extended_data[ch];
    const int remaining = nb_taps - (seg->input_offset + coeff_partition * seg->part_size);
    const int size = remaining >= seg->part_size ? seg->part_size : remaining;

    memset(tempin + size, 0, sizeof(*tempin) * (seg->block_size - size));
    memcpy(tempin, time + seg->input_offset + coeff_partition * seg->part_size,
           size * sizeof(*tempin));
    seg->ctx_fn(seg->ctx[ch], tempout, tempin, sizeof(*tempin));
    memcpy(coeff + coffset, tempout, seg->coeff_size * sizeof(*coeff));

    av_log(ctx, AV_LOG_DEBUG, "channel: %d\n", ch);
    av_log(ctx, AV_LOG_DEBUG, "nb_partitions: %d\n", seg->nb_partitions);
    av_log(ctx, AV_LOG_DEBUG, "partition size: %d\n", seg->part_size);
    av_log(ctx, AV_LOG_DEBUG, "block size: %d\n", seg->block_size);
    av_log(ctx, AV_LOG_DEBUG, "fft_length: %d\n", seg->fft_length);
    av_log(ctx, AV_LOG_DEBUG, "coeff_size: %d\n", seg->coeff_size);
    av_log(ctx, AV_LOG_DEBUG, "input_size: %d\n", seg->input_size);
    av_log(ctx, AV_LOG_DEBUG, "input_offset: %d\n", seg->input_offset);
}

static void fn(fir_fadd)(AudioFIRContext *s, ftype *dst, const ftype *src, int nb_samples)
{
    if ((nb_samples & 15) == 0 && nb_samples >= 8) {
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
    ftype *blockout, *ptr = (ftype *)out->extended_data[ch] + offset;
    const int min_part_size = s->min_part_size;
    const int nb_samples = FFMIN(min_part_size, out->nb_samples - offset);
    const int nb_segments = s->nb_segments;
    const float dry_gain = s->dry_gain;
    const int selir = s->selir;

    for (int segment = 0; segment < nb_segments; segment++) {
        AudioFIRSegment *seg = &s->seg[segment];
        ftype *src = (ftype *)seg->input->extended_data[ch];
        ftype *dst = (ftype *)seg->output->extended_data[ch];
        ftype *sumin = (ftype *)seg->sumin->extended_data[ch];
        ftype *sumout = (ftype *)seg->sumout->extended_data[ch];
        ftype *tempin = (ftype *)seg->tempin->extended_data[ch];
        ftype *buf = (ftype *)seg->buffer->extended_data[ch];
        int *output_offset = &seg->output_offset[ch];
        const int nb_partitions = seg->nb_partitions;
        const int input_offset = seg->input_offset;
        const int part_size = seg->part_size;
        int j;

        seg->part_index[ch] = seg->part_index[ch] % nb_partitions;
        if (min_part_size >= 8) {
#if DEPTH == 32
            s->fdsp->vector_fmul_scalar(src + input_offset, in, dry_gain, FFALIGN(nb_samples, 4));
#else
            s->fdsp->vector_dmul_scalar(src + input_offset, in, dry_gain, FFALIGN(nb_samples, 8));
#endif
            emms_c();
        } else {
            ftype *src2 = src + input_offset;
            for (int n = 0; n < nb_samples; n++)
                src2[n] = in[n] * dry_gain;
        }

        output_offset[0] += min_part_size;
        if (output_offset[0] >= part_size) {
            output_offset[0] = 0;
        } else {
            memmove(src, src + min_part_size, (seg->input_size - min_part_size) * sizeof(*src));

            dst += output_offset[0];
            fn(fir_fadd)(s, ptr, dst, nb_samples);
            continue;
        }

        memset(sumin, 0, sizeof(*sumin) * seg->fft_length);

        if (seg->loading[ch] < nb_partitions) {
            j = seg->part_index[ch] <= 0 ? nb_partitions - 1 : seg->part_index[ch] - 1;
            for (int i = 0; i < nb_partitions; i++) {
                const int input_partition = j;
                const int coeff_partition = i;
                const int coffset = coeff_partition * seg->coeff_size;
                const ftype *blockout = (const ftype *)seg->blockout->extended_data[ch] + input_partition * seg->block_size;
                const ctype *coeff = ((const ctype *)seg->coeff[selir]->extended_data[ch]) + coffset;

                if (j == 0)
                    j = nb_partitions;
                j--;

#if DEPTH == 32
                s->afirdsp.fcmul_add(sumin, blockout, (const ftype *)coeff, part_size);
#else
                s->afirdsp.dcmul_add(sumin, blockout, (const ftype *)coeff, part_size);
#endif
            }

            seg->itx_fn(seg->itx[ch], sumout, sumin, sizeof(ctype));
            memcpy(dst + part_size, sumout + part_size, part_size * sizeof(*buf));
            memset(sumin, 0, sizeof(*sumin) * seg->fft_length);
        }

        blockout = (ftype *)seg->blockout->extended_data[ch] + seg->part_index[ch] * seg->block_size;
        memset(tempin + part_size, 0, sizeof(*tempin) * (seg->block_size - part_size));
        memcpy(tempin, src, sizeof(*src) * part_size);
        seg->tx_fn(seg->tx[ch], blockout, tempin, sizeof(ftype));

        if (seg->loading[ch] < nb_partitions) {
            const int selir = s->prev_selir;

            j = seg->part_index[ch];
            for (int i = 0; i < nb_partitions; i++) {
                const int input_partition = j;
                const int coeff_partition = i;
                const int coffset = coeff_partition * seg->coeff_size;
                const ftype *blockout = (const ftype *)seg->blockout->extended_data[ch] + input_partition * seg->block_size;
                const ctype *coeff = ((const ctype *)seg->coeff[selir]->extended_data[ch]) + coffset;

                if (j == 0)
                    j = nb_partitions;
                j--;

#if DEPTH == 32
                s->afirdsp.fcmul_add(sumin, blockout, (const ftype *)coeff, part_size);
#else
                s->afirdsp.dcmul_add(sumin, blockout, (const ftype *)coeff, part_size);
#endif
            }

            seg->itx_fn(seg->itx[ch], sumout, sumin, sizeof(ctype));
            memcpy(dst + 2 * part_size, sumout, 2 * part_size * sizeof(*dst));
            memset(sumin, 0, sizeof(*sumin) * seg->fft_length);
        }

        j = seg->part_index[ch];
        for (int i = 0; i < nb_partitions; i++) {
            const int input_partition = j;
            const int coeff_partition = i;
            const int coffset = coeff_partition * seg->coeff_size;
            const ftype *blockout = (const ftype *)seg->blockout->extended_data[ch] + input_partition * seg->block_size;
            const ctype *coeff = ((const ctype *)seg->coeff[selir]->extended_data[ch]) + coffset;

            if (j == 0)
                j = nb_partitions;
            j--;

#if DEPTH == 32
            s->afirdsp.fcmul_add(sumin, blockout, (const ftype *)coeff, part_size);
#else
            s->afirdsp.dcmul_add(sumin, blockout, (const ftype *)coeff, part_size);
#endif
        }

        seg->itx_fn(seg->itx[ch], sumout, sumin, sizeof(ctype));

        if (seg->loading[ch] < nb_partitions) {
            ftype *ptr1 = dst + part_size;
            ftype *ptr2 = dst + part_size * 2;
            ftype *ptr3 = dst + part_size * 3;
            ftype *ptr4 = dst + part_size * 4;
            if (seg->loading[ch] == 0)
                memcpy(ptr4, buf, sizeof(*ptr4) * part_size);
            for (int n = 0; n < part_size; n++)
                ptr2[n] += ptr4[n];

            if (seg->loading[ch] < nb_partitions - 1)
                memcpy(ptr4, ptr3, part_size * sizeof(*dst));
            for (int n = 0; n < part_size; n++)
                ptr1[n] += sumout[n];

            if (seg->loading[ch] == nb_partitions - 1)
                memcpy(buf, sumout + part_size, part_size * sizeof(*buf));

            for (int i = 0; i < part_size; i++) {
                const ftype factor = (part_size * seg->loading[ch] + i) / (ftype)(part_size * nb_partitions);
                const ftype ifactor = 1 - factor;
                dst[i] = ptr1[i] * factor + ptr2[i] * ifactor;
            }
        } else {
            fn(fir_fadd)(s, buf, sumout, part_size);
            memcpy(dst, buf, part_size * sizeof(*dst));
            memcpy(buf, sumout + part_size, part_size * sizeof(*buf));
        }

        fn(fir_fadd)(s, ptr, dst, nb_samples);

        if (part_size != min_part_size)
            memmove(src, src + min_part_size, (seg->input_size - min_part_size) * sizeof(*src));

        seg->part_index[ch] = (seg->part_index[ch] + 1) % nb_partitions;
        if (seg->loading[ch] < nb_partitions)
            seg->loading[ch]++;
    }

    if (s->wet_gain == 1.f)
        return 0;

    if (min_part_size >= 8) {
#if DEPTH == 32
        s->fdsp->vector_fmul_scalar(ptr, ptr, s->wet_gain, FFALIGN(nb_samples, 4));
#else
        s->fdsp->vector_dmul_scalar(ptr, ptr, s->wet_gain, FFALIGN(nb_samples, 8));
#endif
        emms_c();
    } else {
        for (int n = 0; n < nb_samples; n++)
            ptr[n] *= s->wet_gain;
    }

    return 0;
}
