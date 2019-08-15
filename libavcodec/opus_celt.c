/*
 * Copyright (c) 2012 Andrew D'Addesio
 * Copyright (c) 2013-2014 Mozilla Corporation
 * Copyright (c) 2016 Rostislav Pehlivanov <atomnuker@gmail.com>
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
 * Opus CELT decoder
 */

#include "opus_celt.h"
#include "opustab.h"
#include "opus_pvq.h"

/* Use the 2D z-transform to apply prediction in both the time domain (alpha)
 * and the frequency domain (beta) */
static void celt_decode_coarse_energy(CeltFrame *f, OpusRangeCoder *rc)
{
    int i, j;
    float prev[2] = { 0 };
    float alpha = ff_celt_alpha_coef[f->size];
    float beta  = ff_celt_beta_coef[f->size];
    const uint8_t *model = ff_celt_coarse_energy_dist[f->size][0];

    /* intra frame */
    if (opus_rc_tell(rc) + 3 <= f->framebits && ff_opus_rc_dec_log(rc, 3)) {
        alpha = 0.0f;
        beta  = 1.0f - (4915.0f/32768.0f);
        model = ff_celt_coarse_energy_dist[f->size][1];
    }

    for (i = 0; i < CELT_MAX_BANDS; i++) {
        for (j = 0; j < f->channels; j++) {
            CeltBlock *block = &f->block[j];
            float value;
            int available;

            if (i < f->start_band || i >= f->end_band) {
                block->energy[i] = 0.0;
                continue;
            }

            available = f->framebits - opus_rc_tell(rc);
            if (available >= 15) {
                /* decode using a Laplace distribution */
                int k = FFMIN(i, 20) << 1;
                value = ff_opus_rc_dec_laplace(rc, model[k] << 7, model[k+1] << 6);
            } else if (available >= 2) {
                int x = ff_opus_rc_dec_cdf(rc, ff_celt_model_energy_small);
                value = (x>>1) ^ -(x&1);
            } else if (available >= 1) {
                value = -(float)ff_opus_rc_dec_log(rc, 1);
            } else value = -1;

            block->energy[i] = FFMAX(-9.0f, block->energy[i]) * alpha + prev[j] + value;
            prev[j] += beta * value;
        }
    }
}

static void celt_decode_fine_energy(CeltFrame *f, OpusRangeCoder *rc)
{
    int i;
    for (i = f->start_band; i < f->end_band; i++) {
        int j;
        if (!f->fine_bits[i])
            continue;

        for (j = 0; j < f->channels; j++) {
            CeltBlock *block = &f->block[j];
            int q2;
            float offset;
            q2 = ff_opus_rc_get_raw(rc, f->fine_bits[i]);
            offset = (q2 + 0.5f) * (1 << (14 - f->fine_bits[i])) / 16384.0f - 0.5f;
            block->energy[i] += offset;
        }
    }
}

static void celt_decode_final_energy(CeltFrame *f, OpusRangeCoder *rc)
{
    int priority, i, j;
    int bits_left = f->framebits - opus_rc_tell(rc);

    for (priority = 0; priority < 2; priority++) {
        for (i = f->start_band; i < f->end_band && bits_left >= f->channels; i++) {
            if (f->fine_priority[i] != priority || f->fine_bits[i] >= CELT_MAX_FINE_BITS)
                continue;

            for (j = 0; j < f->channels; j++) {
                int q2;
                float offset;
                q2 = ff_opus_rc_get_raw(rc, 1);
                offset = (q2 - 0.5f) * (1 << (14 - f->fine_bits[i] - 1)) / 16384.0f;
                f->block[j].energy[i] += offset;
                bits_left--;
            }
        }
    }
}

static void celt_decode_tf_changes(CeltFrame *f, OpusRangeCoder *rc)
{
    int i, diff = 0, tf_select = 0, tf_changed = 0, tf_select_bit;
    int consumed, bits = f->transient ? 2 : 4;

    consumed = opus_rc_tell(rc);
    tf_select_bit = (f->size != 0 && consumed+bits+1 <= f->framebits);

    for (i = f->start_band; i < f->end_band; i++) {
        if (consumed+bits+tf_select_bit <= f->framebits) {
            diff ^= ff_opus_rc_dec_log(rc, bits);
            consumed = opus_rc_tell(rc);
            tf_changed |= diff;
        }
        f->tf_change[i] = diff;
        bits = f->transient ? 4 : 5;
    }

    if (tf_select_bit && ff_celt_tf_select[f->size][f->transient][0][tf_changed] !=
                         ff_celt_tf_select[f->size][f->transient][1][tf_changed])
        tf_select = ff_opus_rc_dec_log(rc, 1);

    for (i = f->start_band; i < f->end_band; i++) {
        f->tf_change[i] = ff_celt_tf_select[f->size][f->transient][tf_select][f->tf_change[i]];
    }
}

static void celt_denormalize(CeltFrame *f, CeltBlock *block, float *data)
{
    int i, j;

    for (i = f->start_band; i < f->end_band; i++) {
        float *dst = data + (ff_celt_freq_bands[i] << f->size);
        float log_norm = block->energy[i] + ff_celt_mean_energy[i];
        float norm = exp2f(FFMIN(log_norm, 32.0f));

        for (j = 0; j < ff_celt_freq_range[i] << f->size; j++)
            dst[j] *= norm;
    }
}

static void celt_postfilter_apply_transition(CeltBlock *block, float *data)
{
    const int T0 = block->pf_period_old;
    const int T1 = block->pf_period;

    float g00, g01, g02;
    float g10, g11, g12;

    float x0, x1, x2, x3, x4;

    int i;

    if (block->pf_gains[0]     == 0.0 &&
        block->pf_gains_old[0] == 0.0)
        return;

    g00 = block->pf_gains_old[0];
    g01 = block->pf_gains_old[1];
    g02 = block->pf_gains_old[2];
    g10 = block->pf_gains[0];
    g11 = block->pf_gains[1];
    g12 = block->pf_gains[2];

    x1 = data[-T1 + 1];
    x2 = data[-T1];
    x3 = data[-T1 - 1];
    x4 = data[-T1 - 2];

    for (i = 0; i < CELT_OVERLAP; i++) {
        float w = ff_celt_window2[i];
        x0 = data[i - T1 + 2];

        data[i] +=  (1.0 - w) * g00 * data[i - T0]                          +
                    (1.0 - w) * g01 * (data[i - T0 - 1] + data[i - T0 + 1]) +
                    (1.0 - w) * g02 * (data[i - T0 - 2] + data[i - T0 + 2]) +
                    w         * g10 * x2                                    +
                    w         * g11 * (x1 + x3)                             +
                    w         * g12 * (x0 + x4);
        x4 = x3;
        x3 = x2;
        x2 = x1;
        x1 = x0;
    }
}

static void celt_postfilter(CeltFrame *f, CeltBlock *block)
{
    int len = f->blocksize * f->blocks;
    const int filter_len = len - 2 * CELT_OVERLAP;

    celt_postfilter_apply_transition(block, block->buf + 1024);

    block->pf_period_old = block->pf_period;
    memcpy(block->pf_gains_old, block->pf_gains, sizeof(block->pf_gains));

    block->pf_period = block->pf_period_new;
    memcpy(block->pf_gains, block->pf_gains_new, sizeof(block->pf_gains));

    if (len > CELT_OVERLAP) {
        celt_postfilter_apply_transition(block, block->buf + 1024 + CELT_OVERLAP);

        if (block->pf_gains[0] > FLT_EPSILON && filter_len > 0)
            f->opusdsp.postfilter(block->buf + 1024 + 2 * CELT_OVERLAP,
                                  block->pf_period, block->pf_gains,
                                  filter_len);

        block->pf_period_old = block->pf_period;
        memcpy(block->pf_gains_old, block->pf_gains, sizeof(block->pf_gains));
    }

    memmove(block->buf, block->buf + len, (1024 + CELT_OVERLAP / 2) * sizeof(float));
}

static int parse_postfilter(CeltFrame *f, OpusRangeCoder *rc, int consumed)
{
    int i;

    memset(f->block[0].pf_gains_new, 0, sizeof(f->block[0].pf_gains_new));
    memset(f->block[1].pf_gains_new, 0, sizeof(f->block[1].pf_gains_new));

    if (f->start_band == 0 && consumed + 16 <= f->framebits) {
        int has_postfilter = ff_opus_rc_dec_log(rc, 1);
        if (has_postfilter) {
            float gain;
            int tapset, octave, period;

            octave = ff_opus_rc_dec_uint(rc, 6);
            period = (16 << octave) + ff_opus_rc_get_raw(rc, 4 + octave) - 1;
            gain   = 0.09375f * (ff_opus_rc_get_raw(rc, 3) + 1);
            tapset = (opus_rc_tell(rc) + 2 <= f->framebits) ?
                     ff_opus_rc_dec_cdf(rc, ff_celt_model_tapset) : 0;

            for (i = 0; i < 2; i++) {
                CeltBlock *block = &f->block[i];

                block->pf_period_new = FFMAX(period, CELT_POSTFILTER_MINPERIOD);
                block->pf_gains_new[0] = gain * ff_celt_postfilter_taps[tapset][0];
                block->pf_gains_new[1] = gain * ff_celt_postfilter_taps[tapset][1];
                block->pf_gains_new[2] = gain * ff_celt_postfilter_taps[tapset][2];
            }
        }

        consumed = opus_rc_tell(rc);
    }

    return consumed;
}

static void process_anticollapse(CeltFrame *f, CeltBlock *block, float *X)
{
    int i, j, k;

    for (i = f->start_band; i < f->end_band; i++) {
        int renormalize = 0;
        float *xptr;
        float prev[2];
        float Ediff, r;
        float thresh, sqrt_1;
        int depth;

        /* depth in 1/8 bits */
        depth = (1 + f->pulses[i]) / (ff_celt_freq_range[i] << f->size);
        thresh = exp2f(-1.0 - 0.125f * depth);
        sqrt_1 = 1.0f / sqrtf(ff_celt_freq_range[i] << f->size);

        xptr = X + (ff_celt_freq_bands[i] << f->size);

        prev[0] = block->prev_energy[0][i];
        prev[1] = block->prev_energy[1][i];
        if (f->channels == 1) {
            CeltBlock *block1 = &f->block[1];

            prev[0] = FFMAX(prev[0], block1->prev_energy[0][i]);
            prev[1] = FFMAX(prev[1], block1->prev_energy[1][i]);
        }
        Ediff = block->energy[i] - FFMIN(prev[0], prev[1]);
        Ediff = FFMAX(0, Ediff);

        /* r needs to be multiplied by 2 or 2*sqrt(2) depending on LM because
        short blocks don't have the same energy as long */
        r = exp2f(1 - Ediff);
        if (f->size == 3)
            r *= M_SQRT2;
        r = FFMIN(thresh, r) * sqrt_1;
        for (k = 0; k < 1 << f->size; k++) {
            /* Detect collapse */
            if (!(block->collapse_masks[i] & 1 << k)) {
                /* Fill with noise */
                for (j = 0; j < ff_celt_freq_range[i]; j++)
                    xptr[(j << f->size) + k] = (celt_rng(f) & 0x8000) ? r : -r;
                renormalize = 1;
            }
        }

        /* We just added some energy, so we need to renormalize */
        if (renormalize)
            celt_renormalize_vector(xptr, ff_celt_freq_range[i] << f->size, 1.0f);
    }
}

int ff_celt_decode_frame(CeltFrame *f, OpusRangeCoder *rc,
                         float **output, int channels, int frame_size,
                         int start_band,  int end_band)
{
    int i, j, downmix = 0;
    int consumed;           // bits of entropy consumed thus far for this frame
    MDCT15Context *imdct;

    if (channels != 1 && channels != 2) {
        av_log(f->avctx, AV_LOG_ERROR, "Invalid number of coded channels: %d\n",
               channels);
        return AVERROR_INVALIDDATA;
    }
    if (start_band < 0 || start_band > end_band || end_band > CELT_MAX_BANDS) {
        av_log(f->avctx, AV_LOG_ERROR, "Invalid start/end band: %d %d\n",
               start_band, end_band);
        return AVERROR_INVALIDDATA;
    }

    f->silence        = 0;
    f->transient      = 0;
    f->anticollapse   = 0;
    f->flushed        = 0;
    f->channels       = channels;
    f->start_band     = start_band;
    f->end_band       = end_band;
    f->framebits      = rc->rb.bytes * 8;

    f->size = av_log2(frame_size / CELT_SHORT_BLOCKSIZE);
    if (f->size > CELT_MAX_LOG_BLOCKS ||
        frame_size != CELT_SHORT_BLOCKSIZE * (1 << f->size)) {
        av_log(f->avctx, AV_LOG_ERROR, "Invalid CELT frame size: %d\n",
               frame_size);
        return AVERROR_INVALIDDATA;
    }

    if (!f->output_channels)
        f->output_channels = channels;

    for (i = 0; i < f->channels; i++) {
        memset(f->block[i].coeffs,         0, sizeof(f->block[i].coeffs));
        memset(f->block[i].collapse_masks, 0, sizeof(f->block[i].collapse_masks));
    }

    consumed = opus_rc_tell(rc);

    /* obtain silence flag */
    if (consumed >= f->framebits)
        f->silence = 1;
    else if (consumed == 1)
        f->silence = ff_opus_rc_dec_log(rc, 15);


    if (f->silence) {
        consumed = f->framebits;
        rc->total_bits += f->framebits - opus_rc_tell(rc);
    }

    /* obtain post-filter options */
    consumed = parse_postfilter(f, rc, consumed);

    /* obtain transient flag */
    if (f->size != 0 && consumed+3 <= f->framebits)
        f->transient = ff_opus_rc_dec_log(rc, 3);

    f->blocks    = f->transient ? 1 << f->size : 1;
    f->blocksize = frame_size / f->blocks;

    imdct = f->imdct[f->transient ? 0 : f->size];

    if (channels == 1) {
        for (i = 0; i < CELT_MAX_BANDS; i++)
            f->block[0].energy[i] = FFMAX(f->block[0].energy[i], f->block[1].energy[i]);
    }

    celt_decode_coarse_energy(f, rc);
    celt_decode_tf_changes   (f, rc);
    ff_celt_bitalloc         (f, rc, 0);
    celt_decode_fine_energy  (f, rc);
    ff_celt_quant_bands      (f, rc);

    if (f->anticollapse_needed)
        f->anticollapse = ff_opus_rc_get_raw(rc, 1);

    celt_decode_final_energy(f, rc);

    /* apply anti-collapse processing and denormalization to
     * each coded channel */
    for (i = 0; i < f->channels; i++) {
        CeltBlock *block = &f->block[i];

        if (f->anticollapse)
            process_anticollapse(f, block, f->block[i].coeffs);

        celt_denormalize(f, block, f->block[i].coeffs);
    }

    /* stereo -> mono downmix */
    if (f->output_channels < f->channels) {
        f->dsp->vector_fmac_scalar(f->block[0].coeffs, f->block[1].coeffs, 1.0, FFALIGN(frame_size, 16));
        downmix = 1;
    } else if (f->output_channels > f->channels)
        memcpy(f->block[1].coeffs, f->block[0].coeffs, frame_size * sizeof(float));

    if (f->silence) {
        for (i = 0; i < 2; i++) {
            CeltBlock *block = &f->block[i];

            for (j = 0; j < FF_ARRAY_ELEMS(block->energy); j++)
                block->energy[j] = CELT_ENERGY_SILENCE;
        }
        memset(f->block[0].coeffs, 0, sizeof(f->block[0].coeffs));
        memset(f->block[1].coeffs, 0, sizeof(f->block[1].coeffs));
    }

    /* transform and output for each output channel */
    for (i = 0; i < f->output_channels; i++) {
        CeltBlock *block = &f->block[i];

        /* iMDCT and overlap-add */
        for (j = 0; j < f->blocks; j++) {
            float *dst  = block->buf + 1024 + j * f->blocksize;

            imdct->imdct_half(imdct, dst + CELT_OVERLAP / 2, f->block[i].coeffs + j,
                              f->blocks);
            f->dsp->vector_fmul_window(dst, dst, dst + CELT_OVERLAP / 2,
                                       ff_celt_window, CELT_OVERLAP / 2);
        }

        if (downmix)
            f->dsp->vector_fmul_scalar(&block->buf[1024], &block->buf[1024], 0.5f, frame_size);

        /* postfilter */
        celt_postfilter(f, block);

        /* deemphasis */
        block->emph_coeff = f->opusdsp.deemphasis(output[i],
                                                  &block->buf[1024 - frame_size],
                                                  block->emph_coeff, frame_size);
    }

    if (channels == 1)
        memcpy(f->block[1].energy, f->block[0].energy, sizeof(f->block[0].energy));

    for (i = 0; i < 2; i++ ) {
        CeltBlock *block = &f->block[i];

        if (!f->transient) {
            memcpy(block->prev_energy[1], block->prev_energy[0], sizeof(block->prev_energy[0]));
            memcpy(block->prev_energy[0], block->energy,         sizeof(block->prev_energy[0]));
        } else {
            for (j = 0; j < CELT_MAX_BANDS; j++)
                block->prev_energy[0][j] = FFMIN(block->prev_energy[0][j], block->energy[j]);
        }

        for (j = 0; j < f->start_band; j++) {
            block->prev_energy[0][j] = CELT_ENERGY_SILENCE;
            block->energy[j]         = 0.0;
        }
        for (j = f->end_band; j < CELT_MAX_BANDS; j++) {
            block->prev_energy[0][j] = CELT_ENERGY_SILENCE;
            block->energy[j]         = 0.0;
        }
    }

    f->seed = rc->range;

    return 0;
}

void ff_celt_flush(CeltFrame *f)
{
    int i, j;

    if (f->flushed)
        return;

    for (i = 0; i < 2; i++) {
        CeltBlock *block = &f->block[i];

        for (j = 0; j < CELT_MAX_BANDS; j++)
            block->prev_energy[0][j] = block->prev_energy[1][j] = CELT_ENERGY_SILENCE;

        memset(block->energy, 0, sizeof(block->energy));
        memset(block->buf,    0, sizeof(block->buf));

        memset(block->pf_gains,     0, sizeof(block->pf_gains));
        memset(block->pf_gains_old, 0, sizeof(block->pf_gains_old));
        memset(block->pf_gains_new, 0, sizeof(block->pf_gains_new));

        /* libopus uses CELT_EMPH_COEFF on init, but 0 is better since there's
         * a lesser discontinuity when seeking.
         * The deemphasis functions differ from libopus in that they require
         * an initial state divided by the coefficient. */
        block->emph_coeff = 0.0f / CELT_EMPH_COEFF;
    }
    f->seed = 0;

    f->flushed = 1;
}

void ff_celt_free(CeltFrame **f)
{
    CeltFrame *frm = *f;
    int i;

    if (!frm)
        return;

    for (i = 0; i < FF_ARRAY_ELEMS(frm->imdct); i++)
        ff_mdct15_uninit(&frm->imdct[i]);

    ff_celt_pvq_uninit(&frm->pvq);

    av_freep(&frm->dsp);
    av_freep(f);
}

int ff_celt_init(AVCodecContext *avctx, CeltFrame **f, int output_channels,
                 int apply_phase_inv)
{
    CeltFrame *frm;
    int i, ret;

    if (output_channels != 1 && output_channels != 2) {
        av_log(avctx, AV_LOG_ERROR, "Invalid number of output channels: %d\n",
               output_channels);
        return AVERROR(EINVAL);
    }

    frm = av_mallocz(sizeof(*frm));
    if (!frm)
        return AVERROR(ENOMEM);

    frm->avctx           = avctx;
    frm->output_channels = output_channels;
    frm->apply_phase_inv = apply_phase_inv;

    for (i = 0; i < FF_ARRAY_ELEMS(frm->imdct); i++)
        if ((ret = ff_mdct15_init(&frm->imdct[i], 1, i + 3, -1.0f/32768)) < 0)
            goto fail;

    if ((ret = ff_celt_pvq_init(&frm->pvq, 0)) < 0)
        goto fail;

    frm->dsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!frm->dsp) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ff_opus_dsp_init(&frm->opusdsp);
    ff_celt_flush(frm);

    *f = frm;

    return 0;
fail:
    ff_celt_free(&frm);
    return ret;
}
