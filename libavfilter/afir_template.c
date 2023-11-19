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
#include "internal.h"
#include "audio.h"

#undef ctype
#undef ftype
#undef SQRT
#undef HYPOT
#undef SAMPLE_FORMAT
#undef TX_TYPE
#undef FABS
#undef POW
#if DEPTH == 32
#define SAMPLE_FORMAT float
#define SQRT sqrtf
#define HYPOT hypotf
#define ctype AVComplexFloat
#define ftype float
#define TX_TYPE AV_TX_FLOAT_RDFT
#define FABS fabsf
#define POW powf
#else
#define SAMPLE_FORMAT double
#define SQRT sqrt
#define HYPOT hypot
#define ctype AVComplexDouble
#define ftype double
#define TX_TYPE AV_TX_DOUBLE_RDFT
#define FABS fabs
#define POW pow
#endif

#define fn3(a,b)   a##_##b
#define fn2(a,b)   fn3(a,b)
#define fn(a)      fn2(a, SAMPLE_FORMAT)

static ftype fn(ir_gain)(AVFilterContext *ctx, AudioFIRContext *s,
                         int cur_nb_taps, const ftype *time)
{
    ftype ch_gain, sum = 0;

    if (s->ir_norm < 0.f) {
        ch_gain = 1;
    } else if (s->ir_norm == 0.f) {
        for (int i = 0; i < cur_nb_taps; i++)
            sum += time[i];
        ch_gain = 1. / sum;
    } else {
        ftype ir_norm = s->ir_norm;

        for (int i = 0; i < cur_nb_taps; i++)
            sum += POW(FABS(time[i]), ir_norm);
        ch_gain = 1. / POW(sum, 1. / ir_norm);
    }

    return ch_gain;
}

static void fn(ir_scale)(AVFilterContext *ctx, AudioFIRContext *s,
                         int cur_nb_taps, int ch,
                         ftype *time, ftype ch_gain)
{
    if (ch_gain != 1. || s->ir_gain != 1.) {
        ftype gain = ch_gain * s->ir_gain;

        av_log(ctx, AV_LOG_DEBUG, "ch%d gain %f\n", ch, gain);
#if DEPTH == 32
        s->fdsp->vector_fmul_scalar(time, time, gain, FFALIGN(cur_nb_taps, 4));
#else
        s->fdsp->vector_dmul_scalar(time, time, gain, FFALIGN(cur_nb_taps, 8));
#endif
    }
}

static void fn(convert_channel)(AVFilterContext *ctx, AudioFIRContext *s, int ch,
                                AudioFIRSegment *seg, int coeff_partition, int selir)
{
    const int coffset = coeff_partition * seg->coeff_size;
    const int nb_taps = s->nb_taps[selir];
    ftype *time = (ftype *)s->norm_ir[selir]->extended_data[ch];
    ftype *tempin = (ftype *)seg->tempin->extended_data[ch];
    ftype *tempout = (ftype *)seg->tempout->extended_data[ch];
    ctype *coeff = (ctype *)seg->coeff->extended_data[ch];
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

static int fn(fir_quantum)(AVFilterContext *ctx, AVFrame *out, int ch, int ioffset, int offset, int selir)
{
    AudioFIRContext *s = ctx->priv;
    const ftype *in = (const ftype *)s->in->extended_data[ch] + ioffset;
    ftype *blockout, *ptr = (ftype *)out->extended_data[ch] + offset;
    const int min_part_size = s->min_part_size;
    const int nb_samples = FFMIN(min_part_size, out->nb_samples - offset);
    const int nb_segments = s->nb_segments[selir];
    const float dry_gain = s->dry_gain;
    const float wet_gain = s->wet_gain;

    for (int segment = 0; segment < nb_segments; segment++) {
        AudioFIRSegment *seg = &s->seg[selir][segment];
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
        if (dry_gain == 1.f) {
            memcpy(src + input_offset, in, nb_samples * sizeof(*src));
        } else if (min_part_size >= 8) {
#if DEPTH == 32
            s->fdsp->vector_fmul_scalar(src + input_offset, in, dry_gain, FFALIGN(nb_samples, 4));
#else
            s->fdsp->vector_dmul_scalar(src + input_offset, in, dry_gain, FFALIGN(nb_samples, 8));
#endif
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

        blockout = (ftype *)seg->blockout->extended_data[ch] + seg->part_index[ch] * seg->block_size;
        memset(tempin + part_size, 0, sizeof(*tempin) * (seg->block_size - part_size));
        memcpy(tempin, src, sizeof(*src) * part_size);
        seg->tx_fn(seg->tx[ch], blockout, tempin, sizeof(ftype));

        j = seg->part_index[ch];
        for (int i = 0; i < nb_partitions; i++) {
            const int input_partition = j;
            const int coeff_partition = i;
            const int coffset = coeff_partition * seg->coeff_size;
            const ftype *blockout = (const ftype *)seg->blockout->extended_data[ch] + input_partition * seg->block_size;
            const ctype *coeff = ((const ctype *)seg->coeff->extended_data[ch]) + coffset;

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

        fn(fir_fadd)(s, buf, sumout, part_size);
        memcpy(dst, buf, part_size * sizeof(*dst));
        memcpy(buf, sumout + part_size, part_size * sizeof(*buf));

        fn(fir_fadd)(s, ptr, dst, nb_samples);

        if (part_size != min_part_size)
            memmove(src, src + min_part_size, (seg->input_size - min_part_size) * sizeof(*src));

        seg->part_index[ch] = (seg->part_index[ch] + 1) % nb_partitions;
    }

    if (wet_gain == 1.f)
        return 0;

    if (min_part_size >= 8) {
#if DEPTH == 32
        s->fdsp->vector_fmul_scalar(ptr, ptr, wet_gain, FFALIGN(nb_samples, 4));
#else
        s->fdsp->vector_dmul_scalar(ptr, ptr, wet_gain, FFALIGN(nb_samples, 8));
#endif
    } else {
        for (int n = 0; n < nb_samples; n++)
            ptr[n] *= wet_gain;
    }

    return 0;
}

static void fn(fir_quantums)(AVFilterContext *ctx, AudioFIRContext *s, AVFrame *out,
                             int min_part_size, int ch, int offset,
                             int prev_selir, int selir)
{
    if (ctx->is_disabled || s->prev_is_disabled) {
        const ftype *in = (const ftype *)s->in->extended_data[ch] + offset;
        const ftype *xfade0 = (const ftype *)s->xfade[0]->extended_data[ch];
        const ftype *xfade1 = (const ftype *)s->xfade[1]->extended_data[ch];
        ftype *src0 = (ftype *)s->fadein[0]->extended_data[ch];
        ftype *src1 = (ftype *)s->fadein[1]->extended_data[ch];
        ftype *dst = ((ftype *)out->extended_data[ch]) + offset;

        if (ctx->is_disabled && !s->prev_is_disabled) {
            memset(src0, 0, min_part_size * sizeof(ftype));
            fn(fir_quantum)(ctx, s->fadein[0], ch, offset, 0, selir);
            for (int n = 0; n < min_part_size; n++)
                dst[n] = xfade1[n] * src0[n] + xfade0[n] * in[n];
        } else if (!ctx->is_disabled && s->prev_is_disabled) {
            memset(src1, 0, min_part_size * sizeof(ftype));
            fn(fir_quantum)(ctx, s->fadein[1], ch, offset, 0, selir);
            for (int n = 0; n < min_part_size; n++)
                dst[n] = xfade1[n] * in[n] + xfade0[n] * src1[n];
        } else {
            memcpy(dst, in, sizeof(ftype) * min_part_size);
        }
    } else if (prev_selir != selir && s->loading[ch] != 0) {
        const ftype *xfade0 = (const ftype *)s->xfade[0]->extended_data[ch];
        const ftype *xfade1 = (const ftype *)s->xfade[1]->extended_data[ch];
        ftype *src0 = (ftype *)s->fadein[0]->extended_data[ch];
        ftype *src1 = (ftype *)s->fadein[1]->extended_data[ch];
        ftype *dst = ((ftype *)out->extended_data[ch]) + offset;

        memset(src0, 0, min_part_size * sizeof(ftype));
        memset(src1, 0, min_part_size * sizeof(ftype));

        fn(fir_quantum)(ctx, s->fadein[0], ch, offset, 0, prev_selir);
        fn(fir_quantum)(ctx, s->fadein[1], ch, offset, 0, selir);

        if (s->loading[ch] > s->max_offset[selir]) {
            for (int n = 0; n < min_part_size; n++)
                dst[n] = xfade1[n] * src0[n] + xfade0[n] * src1[n];
            s->loading[ch] = 0;
        } else {
            memcpy(dst, src0, min_part_size * sizeof(ftype));
        }
    } else {
        fn(fir_quantum)(ctx, out, ch, offset, offset, selir);
    }
}
