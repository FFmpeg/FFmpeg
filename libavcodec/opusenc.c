/*
 * Opus encoder
 * Copyright (c) 2017 Rostislav Pehlivanov <atomnuker@gmail.com>
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

#include "opus_celt.h"
#include "opus_pvq.h"
#include "opustab.h"

#include "libavutil/float_dsp.h"
#include "libavutil/opt.h"
#include "internal.h"
#include "bytestream.h"
#include "audio_frame_queue.h"

/* Determines the maximum delay the psychoacoustic system will use for lookahead */
#define FF_BUFQUEUE_SIZE 145
#include "libavfilter/bufferqueue.h"

#define OPUS_MAX_LOOKAHEAD ((FF_BUFQUEUE_SIZE - 1)*2.5f)

#define OPUS_MAX_CHANNELS 2

/* 120 ms / 2.5 ms = 48 frames (extremely improbable, but the encoder'll work) */
#define OPUS_MAX_FRAMES_PER_PACKET 48

#define OPUS_BLOCK_SIZE(x) (2 * 15 * (1 << ((x) + 2)))

#define OPUS_SAMPLES_TO_BLOCK_SIZE(x) (ff_log2((x) / (2 * 15)) - 2)

typedef struct OpusEncOptions {
    float max_delay_ms;
} OpusEncOptions;

typedef struct OpusEncContext {
    AVClass *av_class;
    OpusEncOptions options;
    AVCodecContext *avctx;
    AudioFrameQueue afq;
    AVFloatDSPContext *dsp;
    MDCT15Context *mdct[CELT_BLOCK_NB];
    CeltPVQ *pvq;
    struct FFBufQueue bufqueue;

    enum OpusMode mode;
    enum OpusBandwidth bandwidth;
    int pkt_framesize;
    int pkt_frames;

    int channels;

    CeltFrame *frame;
    OpusRangeCoder *rc;

    /* Actual energy the decoder will have */
    float last_quantized_energy[OPUS_MAX_CHANNELS][CELT_MAX_BANDS];

    DECLARE_ALIGNED(32, float, scratch)[2048];
} OpusEncContext;

static void opus_write_extradata(AVCodecContext *avctx)
{
    uint8_t *bs = avctx->extradata;

    bytestream_put_buffer(&bs, "OpusHead", 8);
    bytestream_put_byte  (&bs, 0x1);
    bytestream_put_byte  (&bs, avctx->channels);
    bytestream_put_le16  (&bs, avctx->initial_padding);
    bytestream_put_le32  (&bs, avctx->sample_rate);
    bytestream_put_le16  (&bs, 0x0);
    bytestream_put_byte  (&bs, 0x0); /* Default layout */
}

static int opus_gen_toc(OpusEncContext *s, uint8_t *toc, int *size, int *fsize_needed)
{
    int i, tmp = 0x0, extended_toc = 0;
    static const int toc_cfg[][OPUS_MODE_NB][OPUS_BANDWITH_NB] = {
        /*  Silk                    Hybrid                  Celt                    Layer     */
        /*  NB  MB  WB SWB  FB      NB  MB  WB SWB  FB      NB  MB  WB SWB  FB      Bandwidth */
        { {  0,  0,  0,  0,  0 }, {  0,  0,  0,  0,  0 }, { 17,  0, 21, 25, 29 } }, /* 2.5 ms */
        { {  0,  0,  0,  0,  0 }, {  0,  0,  0,  0,  0 }, { 18,  0, 22, 26, 30 } }, /*   5 ms */
        { {  1,  5,  9,  0,  0 }, {  0,  0,  0, 13, 15 }, { 19,  0, 23, 27, 31 } }, /*  10 ms */
        { {  2,  6, 10,  0,  0 }, {  0,  0,  0, 14, 16 }, { 20,  0, 24, 28, 32 } }, /*  20 ms */
        { {  3,  7, 11,  0,  0 }, {  0,  0,  0,  0,  0 }, {  0,  0,  0,  0,  0 } }, /*  40 ms */
        { {  4,  8, 12,  0,  0 }, {  0,  0,  0,  0,  0 }, {  0,  0,  0,  0,  0 } }, /*  60 ms */
    };
    int cfg = toc_cfg[s->pkt_framesize][s->mode][s->bandwidth];
    *fsize_needed = 0;
    if (!cfg)
        return 1;
    if (s->pkt_frames == 2) {                                          /* 2 packets */
        if (s->frame[0].framebits == s->frame[1].framebits) {          /* same size */
            tmp = 0x1;
        } else {                                                  /* different size */
            tmp = 0x2;
            *fsize_needed = 1;                     /* put frame sizes in the packet */
        }
    } else if (s->pkt_frames > 2) {
        tmp = 0x3;
        extended_toc = 1;
    }
    tmp |= (s->channels > 1) << 2;                                /* Stereo or mono */
    tmp |= (cfg - 1)         << 3;                           /* codec configuration */
    *toc++ = tmp;
    if (extended_toc) {
        for (i = 0; i < (s->pkt_frames - 1); i++)
            *fsize_needed |= (s->frame[i].framebits != s->frame[i + 1].framebits);
        tmp = (*fsize_needed) << 7;                                     /* vbr flag */
        tmp |= s->pkt_frames;                    /* frame number - can be 0 as well */
        *toc++ = tmp;
    }
    *size = 1 + extended_toc;
    return 0;
}

static void celt_frame_setup_input(OpusEncContext *s, CeltFrame *f)
{
    int sf, ch;
    AVFrame *cur = NULL;
    const int subframesize = s->avctx->frame_size;
    int subframes = OPUS_BLOCK_SIZE(s->pkt_framesize) / subframesize;

    cur = ff_bufqueue_get(&s->bufqueue);

    for (ch = 0; ch < f->channels; ch++) {
        CeltBlock *b = &f->block[ch];
        const void *input = cur->extended_data[ch];
        size_t bps = av_get_bytes_per_sample(cur->format);
        memcpy(b->overlap, input, bps*cur->nb_samples);
    }

    av_frame_free(&cur);

    for (sf = 0; sf < subframes; sf++) {
        if (sf != (subframes - 1))
            cur = ff_bufqueue_get(&s->bufqueue);
        else
            cur = ff_bufqueue_peek(&s->bufqueue, 0);

        for (ch = 0; ch < f->channels; ch++) {
            CeltBlock *b = &f->block[ch];
            const void *input = cur->extended_data[ch];
            const size_t bps  = av_get_bytes_per_sample(cur->format);
            const size_t left = (subframesize - cur->nb_samples)*bps;
            const size_t len  = FFMIN(subframesize, cur->nb_samples)*bps;
            memcpy(&b->samples[sf*subframesize], input, len);
            memset(&b->samples[cur->nb_samples], 0, left);
        }

        /* Last frame isn't popped off and freed yet - we need it for overlap */
        if (sf != (subframes - 1))
            av_frame_free(&cur);
    }
}

/* Apply the pre emphasis filter */
static void celt_apply_preemph_filter(OpusEncContext *s, CeltFrame *f)
{
    int i, sf, ch;
    const int subframesize = s->avctx->frame_size;
    const int subframes = OPUS_BLOCK_SIZE(s->pkt_framesize) / subframesize;

    /* Filter overlap */
    for (ch = 0; ch < f->channels; ch++) {
        CeltBlock *b = &f->block[ch];
        float m = b->emph_coeff;
        for (i = 0; i < CELT_OVERLAP; i++) {
            float sample = b->overlap[i];
            b->overlap[i] = sample - m;
            m = sample * CELT_EMPH_COEFF;
        }
        b->emph_coeff = m;
    }

    /* Filter the samples but do not update the last subframe's coeff - overlap ^^^ */
    for (sf = 0; sf < subframes; sf++) {
        for (ch = 0; ch < f->channels; ch++) {
            CeltBlock *b = &f->block[ch];
            float m = b->emph_coeff;
            for (i = 0; i < subframesize; i++) {
                float sample = b->samples[sf*subframesize + i];
                b->samples[sf*subframesize + i] = sample - m;
                m = sample * CELT_EMPH_COEFF;
            }
            if (sf != (subframes - 1))
                b->emph_coeff = m;
        }
    }
}

/* Create the window and do the mdct */
static void celt_frame_mdct(OpusEncContext *s, CeltFrame *f)
{
    int i, t, ch;
    float *win = s->scratch, *temp = s->scratch + 1920;

    if (f->transient) {
        for (ch = 0; ch < f->channels; ch++) {
            CeltBlock *b = &f->block[ch];
            float *src1 = b->overlap;
            for (t = 0; t < f->blocks; t++) {
                float *src2 = &b->samples[CELT_OVERLAP*t];
                s->dsp->vector_fmul(win, src1, ff_celt_window, 128);
                s->dsp->vector_fmul_reverse(&win[CELT_OVERLAP], src2,
                                            ff_celt_window - 8, 128);
                src1 = src2;
                s->mdct[0]->mdct(s->mdct[0], b->coeffs + t, win, f->blocks);
            }
        }
    } else {
        int blk_len = OPUS_BLOCK_SIZE(f->size), wlen = OPUS_BLOCK_SIZE(f->size + 1);
        int rwin = blk_len - CELT_OVERLAP, lap_dst = (wlen - blk_len - CELT_OVERLAP) >> 1;
        memset(win, 0, wlen*sizeof(float));
        for (ch = 0; ch < f->channels; ch++) {
            CeltBlock *b = &f->block[ch];

            /* Overlap */
            s->dsp->vector_fmul(temp, b->overlap, ff_celt_window, 128);
            memcpy(win + lap_dst, temp, CELT_OVERLAP*sizeof(float));

            /* Samples, flat top window */
            memcpy(&win[lap_dst + CELT_OVERLAP], b->samples, rwin*sizeof(float));

            /* Samples, windowed */
            s->dsp->vector_fmul_reverse(temp, b->samples + rwin,
                                        ff_celt_window - 8, 128);
            memcpy(win + lap_dst + blk_len, temp, CELT_OVERLAP*sizeof(float));

            s->mdct[f->size]->mdct(s->mdct[f->size], b->coeffs, win, 1);
        }
    }
}

/* Fills the bands and normalizes them */
static void celt_frame_map_norm_bands(OpusEncContext *s, CeltFrame *f)
{
    int i, j, ch;

    for (ch = 0; ch < f->channels; ch++) {
        CeltBlock *block = &f->block[ch];
        for (i = 0; i < CELT_MAX_BANDS; i++) {
            float ener = 0.0f;
            int band_offset = ff_celt_freq_bands[i] << f->size;
            int band_size   = ff_celt_freq_range[i] << f->size;
            float *coeffs   = &block->coeffs[band_offset];

            for (j = 0; j < band_size; j++)
                ener += coeffs[j]*coeffs[j];

            block->lin_energy[i] = sqrtf(ener) + FLT_EPSILON;
            ener = 1.0f/block->lin_energy[i];

            for (j = 0; j < band_size; j++)
                coeffs[j] *= ener;

            block->energy[i] = log2f(block->lin_energy[i]) - ff_celt_mean_energy[i];

            /* CELT_ENERGY_SILENCE is what the decoder uses and its not -infinity */
            block->energy[i] = FFMAX(block->energy[i], CELT_ENERGY_SILENCE);
        }
    }
}

static void celt_enc_tf(OpusRangeCoder *rc, CeltFrame *f)
{
    int i, tf_select = 0, diff = 0, tf_changed = 0, tf_select_needed;
    int bits = f->transient ? 2 : 4;

    tf_select_needed = ((f->size && (opus_rc_tell(rc) + bits + 1) <= f->framebits));

    for (i = f->start_band; i < f->end_band; i++) {
        if ((opus_rc_tell(rc) + bits + tf_select_needed) <= f->framebits) {
            const int tbit = (diff ^ 1) == f->tf_change[i];
            ff_opus_rc_enc_log(rc, tbit, bits);
            diff ^= tbit;
            tf_changed |= diff;
        }
        bits = f->transient ? 4 : 5;
    }

    if (tf_select_needed && ff_celt_tf_select[f->size][f->transient][0][tf_changed] !=
                            ff_celt_tf_select[f->size][f->transient][1][tf_changed]) {
        ff_opus_rc_enc_log(rc, f->tf_select, 1);
        tf_select = f->tf_select;
    }

    for (i = f->start_band; i < f->end_band; i++)
        f->tf_change[i] = ff_celt_tf_select[f->size][f->transient][tf_select][f->tf_change[i]];
}

static void ff_celt_enc_bitalloc(OpusRangeCoder *rc, CeltFrame *f)
{
    int i, j, low, high, total, done, bandbits, remaining, tbits_8ths;
    int skip_startband      = f->start_band;
    int skip_bit            = 0;
    int intensitystereo_bit = 0;
    int dualstereo_bit      = 0;
    int dynalloc            = 6;
    int extrabits           = 0;

    int *cap = f->caps;
    int boost[CELT_MAX_BANDS];
    int trim_offset[CELT_MAX_BANDS];
    int threshold[CELT_MAX_BANDS];
    int bits1[CELT_MAX_BANDS];
    int bits2[CELT_MAX_BANDS];

    /* Tell the spread to the decoder */
    if (opus_rc_tell(rc) + 4 <= f->framebits)
        ff_opus_rc_enc_cdf(rc, f->spread, ff_celt_model_spread);

    /* Generate static allocation caps */
    for (i = 0; i < CELT_MAX_BANDS; i++) {
        cap[i] = (ff_celt_static_caps[f->size][f->channels - 1][i] + 64)
                 * ff_celt_freq_range[i] << (f->channels - 1) << f->size >> 2;
    }

    /* Band boosts */
    tbits_8ths = f->framebits << 3;
    for (i = f->start_band; i < f->end_band; i++) {
        int quanta, b_dynalloc, boost_amount = f->alloc_boost[i];

        boost[i] = 0;

        quanta = ff_celt_freq_range[i] << (f->channels - 1) << f->size;
        quanta = FFMIN(quanta << 3, FFMAX(6 << 3, quanta));
        b_dynalloc = dynalloc;

        while (opus_rc_tell_frac(rc) + (b_dynalloc << 3) < tbits_8ths && boost[i] < cap[i]) {
            int is_boost = boost_amount--;

            ff_opus_rc_enc_log(rc, is_boost, b_dynalloc);
            if (!is_boost)
                break;

            boost[i]   += quanta;
            tbits_8ths -= quanta;

            b_dynalloc = 1;
        }

        if (boost[i])
            dynalloc = FFMAX(2, dynalloc - 1);
    }

    /* Put allocation trim */
    if (opus_rc_tell_frac(rc) + (6 << 3) <= tbits_8ths)
        ff_opus_rc_enc_cdf(rc, f->alloc_trim, ff_celt_model_alloc_trim);

    /* Anti-collapse bit reservation */
    tbits_8ths = (f->framebits << 3) - opus_rc_tell_frac(rc) - 1;
    f->anticollapse_needed = 0;
    if (f->transient && f->size >= 2 && tbits_8ths >= ((f->size + 2) << 3))
        f->anticollapse_needed = 1 << 3;
    tbits_8ths -= f->anticollapse_needed;

    /* Band skip bit reservation */
    if (tbits_8ths >= 1 << 3)
        skip_bit = 1 << 3;
    tbits_8ths -= skip_bit;

    /* Intensity/dual stereo bit reservation */
    if (f->channels == 2) {
        intensitystereo_bit = ff_celt_log2_frac[f->end_band - f->start_band];
        if (intensitystereo_bit <= tbits_8ths) {
            tbits_8ths -= intensitystereo_bit;
            if (tbits_8ths >= 1 << 3) {
                dualstereo_bit = 1 << 3;
                tbits_8ths -= 1 << 3;
            }
        } else {
            intensitystereo_bit = 0;
        }
    }

    /* Trim offsets */
    for (i = f->start_band; i < f->end_band; i++) {
        int trim     = f->alloc_trim - 5 - f->size;
        int band     = ff_celt_freq_range[i] * (f->end_band - i - 1);
        int duration = f->size + 3;
        int scale    = duration + f->channels - 1;

        /* PVQ minimum allocation threshold, below this value the band is
         * skipped */
        threshold[i] = FFMAX(3 * ff_celt_freq_range[i] << duration >> 4,
                             f->channels << 3);

        trim_offset[i] = trim * (band << scale) >> 6;

        if (ff_celt_freq_range[i] << f->size == 1)
            trim_offset[i] -= f->channels << 3;
    }

    /* Bisection */
    low  = 1;
    high = CELT_VECTORS - 1;
    while (low <= high) {
        int center = (low + high) >> 1;
        done = total = 0;

        for (i = f->end_band - 1; i >= f->start_band; i--) {
            bandbits = ff_celt_freq_range[i] * ff_celt_static_alloc[center][i]
                       << (f->channels - 1) << f->size >> 2;

            if (bandbits)
                bandbits = FFMAX(0, bandbits + trim_offset[i]);
            bandbits += boost[i];

            if (bandbits >= threshold[i] || done) {
                done = 1;
                total += FFMIN(bandbits, cap[i]);
            } else if (bandbits >= f->channels << 3)
                total += f->channels << 3;
        }

        if (total > tbits_8ths)
            high = center - 1;
        else
            low = center + 1;
    }
    high = low--;

    /* Bisection */
    for (i = f->start_band; i < f->end_band; i++) {
        bits1[i] = ff_celt_freq_range[i] * ff_celt_static_alloc[low][i]
                   << (f->channels - 1) << f->size >> 2;
        bits2[i] = high >= CELT_VECTORS ? cap[i] :
                   ff_celt_freq_range[i] * ff_celt_static_alloc[high][i]
                   << (f->channels - 1) << f->size >> 2;

        if (bits1[i])
            bits1[i] = FFMAX(0, bits1[i] + trim_offset[i]);
        if (bits2[i])
            bits2[i] = FFMAX(0, bits2[i] + trim_offset[i]);
        if (low)
            bits1[i] += boost[i];
        bits2[i] += boost[i];

        if (boost[i])
            skip_startband = i;
        bits2[i] = FFMAX(0, bits2[i] - bits1[i]);
    }

    /* Bisection */
    low  = 0;
    high = 1 << CELT_ALLOC_STEPS;
    for (i = 0; i < CELT_ALLOC_STEPS; i++) {
        int center = (low + high) >> 1;
        done = total = 0;

        for (j = f->end_band - 1; j >= f->start_band; j--) {
            bandbits = bits1[j] + (center * bits2[j] >> CELT_ALLOC_STEPS);

            if (bandbits >= threshold[j] || done) {
                done = 1;
                total += FFMIN(bandbits, cap[j]);
            } else if (bandbits >= f->channels << 3)
                total += f->channels << 3;
        }
        if (total > tbits_8ths)
            high = center;
        else
            low = center;
    }

    /* Bisection */
    done = total = 0;
    for (i = f->end_band - 1; i >= f->start_band; i--) {
        bandbits = bits1[i] + (low * bits2[i] >> CELT_ALLOC_STEPS);

        if (bandbits >= threshold[i] || done)
            done = 1;
        else
            bandbits = (bandbits >= f->channels << 3) ?
                       f->channels << 3 : 0;

        bandbits     = FFMIN(bandbits, cap[i]);
        f->pulses[i] = bandbits;
        total      += bandbits;
    }

    /* Band skipping */
    for (f->coded_bands = f->end_band; ; f->coded_bands--) {
        int allocation;
        j = f->coded_bands - 1;

        if (j == skip_startband) {
            /* all remaining bands are not skipped */
            tbits_8ths += skip_bit;
            break;
        }

        /* determine the number of bits available for coding "do not skip" markers */
        remaining   = tbits_8ths - total;
        bandbits    = remaining / (ff_celt_freq_bands[j+1] - ff_celt_freq_bands[f->start_band]);
        remaining  -= bandbits  * (ff_celt_freq_bands[j+1] - ff_celt_freq_bands[f->start_band]);
        allocation  = f->pulses[j] + bandbits * ff_celt_freq_range[j]
                      + FFMAX(0, remaining - (ff_celt_freq_bands[j] - ff_celt_freq_bands[f->start_band]));

        /* a "do not skip" marker is only coded if the allocation is
           above the chosen threshold */
        if (allocation >= FFMAX(threshold[j], (f->channels + 1) << 3)) {
            const int do_not_skip = f->coded_bands <= f->skip_band_floor;
            ff_opus_rc_enc_log(rc, do_not_skip, 1);
            if (do_not_skip)
                break;

            total      += 1 << 3;
            allocation -= 1 << 3;
        }

        /* the band is skipped, so reclaim its bits */
        total -= f->pulses[j];
        if (intensitystereo_bit) {
            total -= intensitystereo_bit;
            intensitystereo_bit = ff_celt_log2_frac[j - f->start_band];
            total += intensitystereo_bit;
        }

        total += f->pulses[j] = (allocation >= f->channels << 3) ? f->channels << 3 : 0;
    }

    /* Encode stereo flags */
    if (intensitystereo_bit) {
        f->intensity_stereo = FFMIN(f->intensity_stereo, f->coded_bands);
        ff_opus_rc_enc_uint(rc, f->intensity_stereo, f->coded_bands + 1 - f->start_band);
    }
    if (f->intensity_stereo <= f->start_band)
        tbits_8ths += dualstereo_bit; /* no intensity stereo means no dual stereo */
    else if (dualstereo_bit)
        ff_opus_rc_enc_log(rc, f->dual_stereo, 1);

    /* Supply the remaining bits in this frame to lower bands */
    remaining = tbits_8ths - total;
    bandbits  = remaining / (ff_celt_freq_bands[f->coded_bands] - ff_celt_freq_bands[f->start_band]);
    remaining -= bandbits * (ff_celt_freq_bands[f->coded_bands] - ff_celt_freq_bands[f->start_band]);
    for (i = f->start_band; i < f->coded_bands; i++) {
        int bits = FFMIN(remaining, ff_celt_freq_range[i]);

        f->pulses[i] += bits + bandbits * ff_celt_freq_range[i];
        remaining    -= bits;
    }

    /* Finally determine the allocation */
    for (i = f->start_band; i < f->coded_bands; i++) {
        int N = ff_celt_freq_range[i] << f->size;
        int prev_extra = extrabits;
        f->pulses[i] += extrabits;

        if (N > 1) {
            int dof;        // degrees of freedom
            int temp;       // dof * channels * log(dof)
            int offset;     // fine energy quantization offset, i.e.
                            // extra bits assigned over the standard
                            // totalbits/dof
            int fine_bits, max_bits;

            extrabits = FFMAX(0, f->pulses[i] - cap[i]);
            f->pulses[i] -= extrabits;

            /* intensity stereo makes use of an extra degree of freedom */
            dof = N * f->channels + (f->channels == 2 && N > 2 && !f->dual_stereo && i < f->intensity_stereo);
            temp = dof * (ff_celt_log_freq_range[i] + (f->size << 3));
            offset = (temp >> 1) - dof * CELT_FINE_OFFSET;
            if (N == 2) /* dof=2 is the only case that doesn't fit the model */
                offset += dof << 1;

            /* grant an additional bias for the first and second pulses */
            if (f->pulses[i] + offset < 2 * (dof << 3))
                offset += temp >> 2;
            else if (f->pulses[i] + offset < 3 * (dof << 3))
                offset += temp >> 3;

            fine_bits = (f->pulses[i] + offset + (dof << 2)) / (dof << 3);
            max_bits  = FFMIN((f->pulses[i] >> 3) >> (f->channels - 1), CELT_MAX_FINE_BITS);

            max_bits  = FFMAX(max_bits, 0);

            f->fine_bits[i] = av_clip(fine_bits, 0, max_bits);

            /* if fine_bits was rounded down or capped,
               give priority for the final fine energy pass */
            f->fine_priority[i] = (f->fine_bits[i] * (dof << 3) >= f->pulses[i] + offset);

            /* the remaining bits are assigned to PVQ */
            f->pulses[i] -= f->fine_bits[i] << (f->channels - 1) << 3;
        } else {
            /* all bits go to fine energy except for the sign bit */
            extrabits = FFMAX(0, f->pulses[i] - (f->channels << 3));
            f->pulses[i] -= extrabits;
            f->fine_bits[i] = 0;
            f->fine_priority[i] = 1;
        }

        /* hand back a limited number of extra fine energy bits to this band */
        if (extrabits > 0) {
            int fineextra = FFMIN(extrabits >> (f->channels + 2),
                                  CELT_MAX_FINE_BITS - f->fine_bits[i]);
            f->fine_bits[i] += fineextra;

            fineextra <<= f->channels + 2;
            f->fine_priority[i] = (fineextra >= extrabits - prev_extra);
            extrabits -= fineextra;
        }
    }
    f->remaining = extrabits;

    /* skipped bands dedicate all of their bits for fine energy */
    for (; i < f->end_band; i++) {
        f->fine_bits[i]     = f->pulses[i] >> (f->channels - 1) >> 3;
        f->pulses[i]        = 0;
        f->fine_priority[i] = f->fine_bits[i] < 1;
    }
}

static void exp_quant_coarse(OpusRangeCoder *rc, CeltFrame *f,
                             float last_energy[][CELT_MAX_BANDS], int intra)
{
    int i, ch;
    float alpha, beta, prev[2] = { 0, 0 };
    const uint8_t *pmod = ff_celt_coarse_energy_dist[f->size][intra];

    /* Inter is really just differential coding */
    if (opus_rc_tell(rc) + 3 <= f->framebits)
        ff_opus_rc_enc_log(rc, intra, 3);
    else
        intra = 0;

    if (intra) {
        alpha = 0.0f;
        beta  = 1.0f - 4915.0f/32768.0f;
    } else {
        alpha = ff_celt_alpha_coef[f->size];
        beta  = 1.0f - ff_celt_beta_coef[f->size];
    }

    for (i = f->start_band; i < f->end_band; i++) {
        for (ch = 0; ch < f->channels; ch++) {
            CeltBlock *block = &f->block[ch];
            const int left = f->framebits - opus_rc_tell(rc);
            const float last = FFMAX(-9.0f, last_energy[ch][i]);
            float diff = block->energy[i] - prev[ch] - last*alpha;
            int q_en = lrintf(diff);
            if (left >= 15) {
                ff_opus_rc_enc_laplace(rc, &q_en, pmod[i << 1] << 7, pmod[(i << 1) + 1] << 6);
            } else if (left >= 2) {
                q_en = av_clip(q_en, -1, 1);
                ff_opus_rc_enc_cdf(rc, 2*q_en + 3*(q_en < 0), ff_celt_model_energy_small);
            } else if (left >= 1) {
                q_en = av_clip(q_en, -1, 0);
                ff_opus_rc_enc_log(rc, (q_en & 1), 1);
            } else q_en = -1;

            block->error_energy[i] = q_en - diff;
            prev[ch] += beta * q_en;
        }
    }
}

static void celt_quant_coarse(OpusRangeCoder *rc, CeltFrame *f,
                              float last_energy[][CELT_MAX_BANDS])
{
    uint32_t inter, intra;
    OPUS_RC_CHECKPOINT_SPAWN(rc);

    exp_quant_coarse(rc, f, last_energy, 1);
    intra = OPUS_RC_CHECKPOINT_BITS(rc);

    OPUS_RC_CHECKPOINT_ROLLBACK(rc);

    exp_quant_coarse(rc, f, last_energy, 0);
    inter = OPUS_RC_CHECKPOINT_BITS(rc);

    if (inter > intra) { /* Unlikely */
        OPUS_RC_CHECKPOINT_ROLLBACK(rc);
        exp_quant_coarse(rc, f, last_energy, 1);
    }
}

static void celt_quant_fine(OpusRangeCoder *rc, CeltFrame *f)
{
    int i, ch;
    for (i = f->start_band; i < f->end_band; i++) {
        if (!f->fine_bits[i])
            continue;
        for (ch = 0; ch < f->channels; ch++) {
            CeltBlock *block = &f->block[ch];
            int quant, lim = (1 << f->fine_bits[i]);
            float offset, diff = 0.5f - block->error_energy[i];
            quant = av_clip(floor(diff*lim), 0, lim - 1);
            ff_opus_rc_put_raw(rc, quant, f->fine_bits[i]);
            offset = 0.5f - ((quant + 0.5f) * (1 << (14 - f->fine_bits[i])) / 16384.0f);
            block->error_energy[i] -= offset;
        }
    }
}

static void celt_quant_final(OpusEncContext *s, OpusRangeCoder *rc, CeltFrame *f)
{
    int i, ch, priority;
    for (priority = 0; priority < 2; priority++) {
        for (i = f->start_band; i < f->end_band && (f->framebits - opus_rc_tell(rc)) >= f->channels; i++) {
            if (f->fine_priority[i] != priority || f->fine_bits[i] >= CELT_MAX_FINE_BITS)
                continue;
            for (ch = 0; ch < f->channels; ch++) {
                CeltBlock *block = &f->block[ch];
                const float err = block->error_energy[i];
                const float offset = 0.5f * (1 << (14 - f->fine_bits[i] - 1)) / 16384.0f;
                const int sign = FFABS(err + offset) < FFABS(err - offset);
                ff_opus_rc_put_raw(rc, sign, 1);
                block->error_energy[i] -= offset*(1 - 2*sign);
            }
        }
    }
}

static void celt_quant_bands(OpusRangeCoder *rc, CeltFrame *f)
{
    float lowband_scratch[8 * 22];
    float norm[2 * 8 * 100];

    int totalbits = (f->framebits << 3) - f->anticollapse_needed;

    int update_lowband = 1;
    int lowband_offset = 0;

    int i, j;

    for (i = f->start_band; i < f->end_band; i++) {
        uint32_t cm[2] = { (1 << f->blocks) - 1, (1 << f->blocks) - 1 };
        int band_offset = ff_celt_freq_bands[i] << f->size;
        int band_size   = ff_celt_freq_range[i] << f->size;
        float *X = f->block[0].coeffs + band_offset;
        float *Y = (f->channels == 2) ? f->block[1].coeffs + band_offset : NULL;

        int consumed = opus_rc_tell_frac(rc);
        float *norm2 = norm + 8 * 100;
        int effective_lowband = -1;
        int b = 0;

        /* Compute how many bits we want to allocate to this band */
        if (i != f->start_band)
            f->remaining -= consumed;
        f->remaining2 = totalbits - consumed - 1;
        if (i <= f->coded_bands - 1) {
            int curr_balance = f->remaining / FFMIN(3, f->coded_bands-i);
            b = av_clip_uintp2(FFMIN(f->remaining2 + 1, f->pulses[i] + curr_balance), 14);
        }

        if (ff_celt_freq_bands[i] - ff_celt_freq_range[i] >= ff_celt_freq_bands[f->start_band] &&
            (update_lowband || lowband_offset == 0))
            lowband_offset = i;

        /* Get a conservative estimate of the collapse_mask's for the bands we're
        going to be folding from. */
        if (lowband_offset != 0 && (f->spread != CELT_SPREAD_AGGRESSIVE ||
                                    f->blocks > 1 || f->tf_change[i] < 0)) {
            int foldstart, foldend;

            /* This ensures we never repeat spectral content within one band */
            effective_lowband = FFMAX(ff_celt_freq_bands[f->start_band],
                                      ff_celt_freq_bands[lowband_offset] - ff_celt_freq_range[i]);
            foldstart = lowband_offset;
            while (ff_celt_freq_bands[--foldstart] > effective_lowband);
            foldend = lowband_offset - 1;
            while (ff_celt_freq_bands[++foldend] < effective_lowband + ff_celt_freq_range[i]);

            cm[0] = cm[1] = 0;
            for (j = foldstart; j < foldend; j++) {
                cm[0] |= f->block[0].collapse_masks[j];
                cm[1] |= f->block[f->channels - 1].collapse_masks[j];
            }
        }

        if (f->dual_stereo && i == f->intensity_stereo) {
            /* Switch off dual stereo to do intensity */
            f->dual_stereo = 0;
            for (j = ff_celt_freq_bands[f->start_band] << f->size; j < band_offset; j++)
                norm[j] = (norm[j] + norm2[j]) / 2;
        }

        if (f->dual_stereo) {
            cm[0] = f->pvq->encode_band(f->pvq, f, rc, i, X, NULL, band_size, b / 2, f->blocks,
                                        effective_lowband != -1 ? norm + (effective_lowband << f->size) : NULL, f->size,
                                        norm + band_offset, 0, 1.0f, lowband_scratch, cm[0]);

            cm[1] = f->pvq->encode_band(f->pvq, f, rc, i, Y, NULL, band_size, b / 2, f->blocks,
                                        effective_lowband != -1 ? norm2 + (effective_lowband << f->size) : NULL, f->size,
                                        norm2 + band_offset, 0, 1.0f, lowband_scratch, cm[1]);
        } else {
            cm[0] = f->pvq->encode_band(f->pvq, f, rc, i, X, Y, band_size, b, f->blocks,
                                        effective_lowband != -1 ? norm + (effective_lowband << f->size) : NULL, f->size,
                                        norm + band_offset, 0, 1.0f, lowband_scratch, cm[0] | cm[1]);
            cm[1] = cm[0];
        }

        f->block[0].collapse_masks[i]               = (uint8_t)cm[0];
        f->block[f->channels - 1].collapse_masks[i] = (uint8_t)cm[1];
        f->remaining += f->pulses[i] + consumed;

        /* Update the folding position only as long as we have 1 bit/sample depth */
        update_lowband = (b > band_size << 3);
    }
}

static void celt_encode_frame(OpusEncContext *s, OpusRangeCoder *rc, CeltFrame *f)
{
    int i, ch;

    celt_frame_setup_input(s, f);
    celt_apply_preemph_filter(s, f);
    if (f->pfilter) {
        /* Not implemented */
    }
    celt_frame_mdct(s, f);
    celt_frame_map_norm_bands(s, f);

    ff_opus_rc_enc_log(rc, f->silence, 15);

    if (!f->start_band && opus_rc_tell(rc) + 16 <= f->framebits)
        ff_opus_rc_enc_log(rc, f->pfilter, 1);

    if (f->pfilter) {
        /* Not implemented */
    }

    if (f->size && opus_rc_tell(rc) + 3 <= f->framebits)
        ff_opus_rc_enc_log(rc, f->transient, 3);

    celt_quant_coarse(rc, f, s->last_quantized_energy);
    celt_enc_tf      (rc, f);
    ff_celt_enc_bitalloc(rc, f);
    celt_quant_fine  (rc, f);
    celt_quant_bands (rc, f);

    if (f->anticollapse_needed)
        ff_opus_rc_put_raw(rc, f->anticollapse, 1);

    celt_quant_final(s, rc, f);

    for (ch = 0; ch < f->channels; ch++) {
        CeltBlock *block = &f->block[ch];
        for (i = 0; i < CELT_MAX_BANDS; i++)
            s->last_quantized_energy[ch][i] = block->energy[i] + block->error_energy[i];
    }
}

static void ff_opus_psy_process(OpusEncContext *s, int end, int *need_more)
{
    int max_delay_samples = (s->options.max_delay_ms*s->avctx->sample_rate)/1000;
    int max_bsize = FFMIN(OPUS_SAMPLES_TO_BLOCK_SIZE(max_delay_samples), CELT_BLOCK_960);

    s->pkt_frames = 1;
    s->pkt_framesize = max_bsize;
    s->mode = OPUS_MODE_CELT;
    s->bandwidth = OPUS_BANDWIDTH_FULLBAND;

    *need_more = s->bufqueue.available*s->avctx->frame_size < (max_delay_samples + CELT_OVERLAP);
    /* Don't request more if we start being flushed with NULL frames */
    *need_more = !end && *need_more;
}

static void ff_opus_psy_celt_frame_setup(OpusEncContext *s, CeltFrame *f, int index)
{
    int frame_size = OPUS_BLOCK_SIZE(s->pkt_framesize);

    f->avctx = s->avctx;
    f->dsp = s->dsp;
    f->pvq = s->pvq;
    f->start_band = (s->mode == OPUS_MODE_HYBRID) ? 17 : 0;
    f->end_band = ff_celt_band_end[s->bandwidth];
    f->channels = s->channels;
    f->size = s->pkt_framesize;

    /* Decisions */
    f->silence = 0;
    f->pfilter = 0;
    f->transient = 0;
    f->tf_select = 0;
    f->anticollapse = 0;
    f->alloc_trim = 5;
    f->skip_band_floor = f->end_band;
    f->intensity_stereo = f->end_band;
    f->dual_stereo = 0;
    f->spread = CELT_SPREAD_NORMAL;
    memset(f->tf_change, 0, sizeof(int)*CELT_MAX_BANDS);
    memset(f->alloc_boost, 0, sizeof(int)*CELT_MAX_BANDS);

    f->blocks = f->transient ? frame_size/CELT_OVERLAP : 1;
    f->framebits = FFALIGN(lrintf((double)s->avctx->bit_rate/(s->avctx->sample_rate/frame_size)), 8);
}

static void opus_packet_assembler(OpusEncContext *s, AVPacket *avpkt)
{
    int i, offset, fsize_needed;

    /* Write toc */
    opus_gen_toc(s, avpkt->data, &offset, &fsize_needed);

    for (i = 0; i < s->pkt_frames; i++) {
        ff_opus_rc_enc_end(&s->rc[i], avpkt->data + offset, s->frame[i].framebits >> 3);
        offset += s->frame[i].framebits >> 3;
    }

    avpkt->size = offset;
}

/* Used as overlap for the first frame and padding for the last encoded packet */
static AVFrame *spawn_empty_frame(OpusEncContext *s)
{
    int i;
    AVFrame *f = av_frame_alloc();
    if (!f)
        return NULL;
    f->format         = s->avctx->sample_fmt;
    f->nb_samples     = s->avctx->frame_size;
    f->channel_layout = s->avctx->channel_layout;
    if (av_frame_get_buffer(f, 4)) {
        av_frame_free(&f);
        return NULL;
    }
    for (i = 0; i < s->channels; i++) {
        size_t bps = av_get_bytes_per_sample(f->format);
        memset(f->extended_data[i], 0, bps*f->nb_samples);
    }
    return f;
}

static int opus_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                             const AVFrame *frame, int *got_packet_ptr)
{
    OpusEncContext *s = avctx->priv_data;
    int i, ret, frame_size, need_more, alloc_size = 0;

    if (frame) { /* Add new frame to queue */
        if ((ret = ff_af_queue_add(&s->afq, frame)) < 0)
            return ret;
        ff_bufqueue_add(avctx, &s->bufqueue, av_frame_clone(frame));
    } else {
        if (!s->afq.remaining_samples)
            return 0; /* We've been flushed and there's nothing left to encode */
    }

    /* Run the psychoacoustic system */
    ff_opus_psy_process(s, !frame, &need_more);

    /* Get more samples for lookahead/encoding */
    if (need_more)
        return 0;

    frame_size = OPUS_BLOCK_SIZE(s->pkt_framesize);

    if (!frame) {
        /* This can go negative, that's not a problem, we only pad if positive */
        int pad_empty = s->pkt_frames*(frame_size/s->avctx->frame_size) - s->bufqueue.available + 1;
        /* Pad with empty 2.5 ms frames to whatever framesize was decided,
         * this should only happen at the very last flush frame. The frames
         * allocated here will be freed (because they have no other references)
         * after they get used by celt_frame_setup_input() */
        for (i = 0; i < pad_empty; i++) {
            AVFrame *empty = spawn_empty_frame(s);
            if (!empty)
                return AVERROR(ENOMEM);
            ff_bufqueue_add(avctx, &s->bufqueue, empty);
        }
    }

    for (i = 0; i < s->pkt_frames; i++) {
        ff_opus_rc_enc_init(&s->rc[i]);
        ff_opus_psy_celt_frame_setup(s, &s->frame[i], i);
        celt_encode_frame(s, &s->rc[i], &s->frame[i]);
        alloc_size += s->frame[i].framebits >> 3;
    }

    /* Worst case toc + the frame lengths if needed */
    alloc_size += 2 + s->pkt_frames*2;

    if ((ret = ff_alloc_packet2(avctx, avpkt, alloc_size, 0)) < 0)
        return ret;

    /* Assemble packet */
    opus_packet_assembler(s, avpkt);

    /* Remove samples from queue and skip if needed */
    ff_af_queue_remove(&s->afq, s->pkt_frames*frame_size, &avpkt->pts, &avpkt->duration);
    if (s->pkt_frames*frame_size > avpkt->duration) {
        uint8_t *side = av_packet_new_side_data(avpkt, AV_PKT_DATA_SKIP_SAMPLES, 10);
        if (!side)
            return AVERROR(ENOMEM);
        AV_WL32(&side[4], s->pkt_frames*frame_size - avpkt->duration + 120);
    }

    *got_packet_ptr = 1;

    return 0;
}

static av_cold int opus_encode_end(AVCodecContext *avctx)
{
    int i;
    OpusEncContext *s = avctx->priv_data;

    for (i = 0; i < CELT_BLOCK_NB; i++)
        ff_mdct15_uninit(&s->mdct[i]);

    ff_celt_pvq_uninit(&s->pvq);
    av_freep(&s->dsp);
    av_freep(&s->frame);
    av_freep(&s->rc);
    ff_af_queue_close(&s->afq);
    ff_bufqueue_discard_all(&s->bufqueue);
    av_freep(&avctx->extradata);

    return 0;
}

static av_cold int opus_encode_init(AVCodecContext *avctx)
{
    int i, ch, ret;
    OpusEncContext *s = avctx->priv_data;

    s->avctx = avctx;
    s->channels = avctx->channels;

    /* Opus allows us to change the framesize on each packet (and each packet may
     * have multiple frames in it) but we can't change the codec's frame size on
     * runtime, so fix it to the lowest possible number of samples and use a queue
     * to accumulate AVFrames until we have enough to encode whatever the encoder
     * decides is the best */
    avctx->frame_size = 120;
    /* Initial padding will change if SILK is ever supported */
    avctx->initial_padding = 120;

    if (!avctx->bit_rate) {
        int coupled = ff_opus_default_coupled_streams[s->channels - 1];
        avctx->bit_rate = coupled*(96000) + (s->channels - coupled*2)*(48000);
    } else if (avctx->bit_rate < 6000 || avctx->bit_rate > 255000 * s->channels) {
        int64_t clipped_rate = av_clip(avctx->bit_rate, 6000, 255000 * s->channels);
        av_log(avctx, AV_LOG_ERROR, "Unsupported bitrate %"PRId64" kbps, clipping to %"PRId64" kbps\n",
               avctx->bit_rate/1000, clipped_rate/1000);
        avctx->bit_rate = clipped_rate;
    }

    /* Frame structs and range coder buffers */
    s->frame = av_malloc(OPUS_MAX_FRAMES_PER_PACKET*sizeof(CeltFrame));
    if (!s->frame)
        return AVERROR(ENOMEM);
    s->rc = av_malloc(OPUS_MAX_FRAMES_PER_PACKET*sizeof(OpusRangeCoder));
    if (!s->rc)
        return AVERROR(ENOMEM);

    /* Extradata */
    avctx->extradata_size = 19;
    avctx->extradata = av_malloc(avctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);
    opus_write_extradata(avctx);

    ff_af_queue_init(avctx, &s->afq);

    if ((ret = ff_celt_pvq_init(&s->pvq)) < 0)
        return ret;

    if (!(s->dsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT)))
        return AVERROR(ENOMEM);

    /* I have no idea why a base scaling factor of 68 works, could be the twiddles */
    for (i = 0; i < CELT_BLOCK_NB; i++)
        if ((ret = ff_mdct15_init(&s->mdct[i], 0, i + 3, 68 << (CELT_BLOCK_NB - 1 - i))))
            return AVERROR(ENOMEM);

    for (i = 0; i < OPUS_MAX_FRAMES_PER_PACKET; i++) {
        s->frame[i].block[0].emph_coeff = s->frame[i].block[1].emph_coeff = 0.0f;
        s->frame[i].seed = 0;
    }

    /* Zero out previous energy (matters for inter first frame) */
    for (ch = 0; ch < s->channels; ch++)
        for (i = 0; i < CELT_MAX_BANDS; i++)
            s->last_quantized_energy[ch][i] = 0.0f;

    /* Allocate an empty frame to use as overlap for the first frame of audio */
    ff_bufqueue_add(avctx, &s->bufqueue, spawn_empty_frame(s));
    if (!ff_bufqueue_peek(&s->bufqueue, 0))
        return AVERROR(ENOMEM);

    return 0;
}

#define OPUSENC_FLAGS AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM
static const AVOption opusenc_options[] = {
    { "opus_delay", "Maximum delay (and lookahead) in milliseconds", offsetof(OpusEncContext, options.max_delay_ms), AV_OPT_TYPE_FLOAT, { .dbl = OPUS_MAX_LOOKAHEAD }, 2.5f, OPUS_MAX_LOOKAHEAD, OPUSENC_FLAGS },
    { NULL },
};

static const AVClass opusenc_class = {
    .class_name = "Opus encoder",
    .item_name  = av_default_item_name,
    .option     = opusenc_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault opusenc_defaults[] = {
    { "b", "0" },
    { "compression_level", "10" },
    { NULL },
};

AVCodec ff_opus_encoder = {
    .name           = "opus",
    .long_name      = NULL_IF_CONFIG_SMALL("Opus"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_OPUS,
    .defaults       = opusenc_defaults,
    .priv_class     = &opusenc_class,
    .priv_data_size = sizeof(OpusEncContext),
    .init           = opus_encode_init,
    .encode2        = opus_encode_frame,
    .close          = opus_encode_end,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
    .capabilities   = AV_CODEC_CAP_EXPERIMENTAL | AV_CODEC_CAP_SMALL_LAST_FRAME | AV_CODEC_CAP_DELAY,
    .supported_samplerates = (const int []){ 48000, 0 },
    .channel_layouts = (const uint64_t []){ AV_CH_LAYOUT_MONO,
                                            AV_CH_LAYOUT_STEREO, 0 },
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_FLTP,
                                                     AV_SAMPLE_FMT_NONE },
};
