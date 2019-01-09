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

#include "opusenc.h"
#include "opus_pvq.h"
#include "opusenc_psy.h"
#include "opustab.h"

#include "libavutil/float_dsp.h"
#include "libavutil/opt.h"
#include "internal.h"
#include "bytestream.h"
#include "audio_frame_queue.h"

typedef struct OpusEncContext {
    AVClass *av_class;
    OpusEncOptions options;
    OpusPsyContext psyctx;
    AVCodecContext *avctx;
    AudioFrameQueue afq;
    AVFloatDSPContext *dsp;
    MDCT15Context *mdct[CELT_BLOCK_NB];
    CeltPVQ *pvq;
    struct FFBufQueue bufqueue;

    uint8_t enc_id[64];
    int enc_id_bits;

    OpusPacketInfo packet;

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
    int tmp = 0x0, extended_toc = 0;
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
    int cfg = toc_cfg[s->packet.framesize][s->packet.mode][s->packet.bandwidth];
    *fsize_needed = 0;
    if (!cfg)
        return 1;
    if (s->packet.frames == 2) {                                       /* 2 packets */
        if (s->frame[0].framebits == s->frame[1].framebits) {          /* same size */
            tmp = 0x1;
        } else {                                                  /* different size */
            tmp = 0x2;
            *fsize_needed = 1;                     /* put frame sizes in the packet */
        }
    } else if (s->packet.frames > 2) {
        tmp = 0x3;
        extended_toc = 1;
    }
    tmp |= (s->channels > 1) << 2;                                /* Stereo or mono */
    tmp |= (cfg - 1)         << 3;                           /* codec configuration */
    *toc++ = tmp;
    if (extended_toc) {
        for (int i = 0; i < (s->packet.frames - 1); i++)
            *fsize_needed |= (s->frame[i].framebits != s->frame[i + 1].framebits);
        tmp = (*fsize_needed) << 7;                                /* vbr flag */
        tmp |= (0) << 6;                                       /* padding flag */
        tmp |= s->packet.frames;
        *toc++ = tmp;
    }
    *size = 1 + extended_toc;
    return 0;
}

static void celt_frame_setup_input(OpusEncContext *s, CeltFrame *f)
{
    AVFrame *cur = NULL;
    const int subframesize = s->avctx->frame_size;
    int subframes = OPUS_BLOCK_SIZE(s->packet.framesize) / subframesize;

    cur = ff_bufqueue_get(&s->bufqueue);

    for (int ch = 0; ch < f->channels; ch++) {
        CeltBlock *b = &f->block[ch];
        const void *input = cur->extended_data[ch];
        size_t bps = av_get_bytes_per_sample(cur->format);
        memcpy(b->overlap, input, bps*cur->nb_samples);
    }

    av_frame_free(&cur);

    for (int sf = 0; sf < subframes; sf++) {
        if (sf != (subframes - 1))
            cur = ff_bufqueue_get(&s->bufqueue);
        else
            cur = ff_bufqueue_peek(&s->bufqueue, 0);

        for (int ch = 0; ch < f->channels; ch++) {
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
    const int subframesize = s->avctx->frame_size;
    const int subframes = OPUS_BLOCK_SIZE(s->packet.framesize) / subframesize;

    /* Filter overlap */
    for (int ch = 0; ch < f->channels; ch++) {
        CeltBlock *b = &f->block[ch];
        float m = b->emph_coeff;
        for (int i = 0; i < CELT_OVERLAP; i++) {
            float sample = b->overlap[i];
            b->overlap[i] = sample - m;
            m = sample * CELT_EMPH_COEFF;
        }
        b->emph_coeff = m;
    }

    /* Filter the samples but do not update the last subframe's coeff - overlap ^^^ */
    for (int sf = 0; sf < subframes; sf++) {
        for (int ch = 0; ch < f->channels; ch++) {
            CeltBlock *b = &f->block[ch];
            float m = b->emph_coeff;
            for (int i = 0; i < subframesize; i++) {
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
    float *win = s->scratch, *temp = s->scratch + 1920;

    if (f->transient) {
        for (int ch = 0; ch < f->channels; ch++) {
            CeltBlock *b = &f->block[ch];
            float *src1 = b->overlap;
            for (int t = 0; t < f->blocks; t++) {
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
        for (int ch = 0; ch < f->channels; ch++) {
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

    for (int ch = 0; ch < f->channels; ch++) {
        CeltBlock *block = &f->block[ch];
        for (int i = 0; i < CELT_MAX_BANDS; i++) {
            float ener = 0.0f;
            int band_offset = ff_celt_freq_bands[i] << f->size;
            int band_size   = ff_celt_freq_range[i] << f->size;
            float *coeffs   = &block->coeffs[band_offset];

            for (int j = 0; j < band_size; j++)
                ener += coeffs[j]*coeffs[j];

            block->lin_energy[i] = sqrtf(ener) + FLT_EPSILON;
            ener = 1.0f/block->lin_energy[i];

            for (int j = 0; j < band_size; j++)
                coeffs[j] *= ener;

            block->energy[i] = log2f(block->lin_energy[i]) - ff_celt_mean_energy[i];

            /* CELT_ENERGY_SILENCE is what the decoder uses and its not -infinity */
            block->energy[i] = FFMAX(block->energy[i], CELT_ENERGY_SILENCE);
        }
    }
}

static void celt_enc_tf(CeltFrame *f, OpusRangeCoder *rc)
{
    int tf_select = 0, diff = 0, tf_changed = 0, tf_select_needed;
    int bits = f->transient ? 2 : 4;

    tf_select_needed = ((f->size && (opus_rc_tell(rc) + bits + 1) <= f->framebits));

    for (int i = f->start_band; i < f->end_band; i++) {
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

    for (int i = f->start_band; i < f->end_band; i++)
        f->tf_change[i] = ff_celt_tf_select[f->size][f->transient][tf_select][f->tf_change[i]];
}

static void celt_enc_quant_pfilter(OpusRangeCoder *rc, CeltFrame *f)
{
    float gain = f->pf_gain;
    int txval, octave = f->pf_octave, period = f->pf_period, tapset = f->pf_tapset;

    ff_opus_rc_enc_log(rc, f->pfilter, 1);
    if (!f->pfilter)
        return;

    /* Octave */
    txval = FFMIN(octave, 6);
    ff_opus_rc_enc_uint(rc, txval, 6);
    octave = txval;
    /* Period */
    txval = av_clip(period - (16 << octave) + 1, 0, (1 << (4 + octave)) - 1);
    ff_opus_rc_put_raw(rc, period, 4 + octave);
    period = txval + (16 << octave) - 1;
    /* Gain */
    txval = FFMIN(((int)(gain / 0.09375f)) - 1, 7);
    ff_opus_rc_put_raw(rc, txval, 3);
    gain   = 0.09375f * (txval + 1);
    /* Tapset */
    if ((opus_rc_tell(rc) + 2) <= f->framebits)
        ff_opus_rc_enc_cdf(rc, tapset, ff_celt_model_tapset);
    else
        tapset = 0;
    /* Finally create the coeffs */
    for (int i = 0; i < 2; i++) {
        CeltBlock *block = &f->block[i];

        block->pf_period_new = FFMAX(period, CELT_POSTFILTER_MINPERIOD);
        block->pf_gains_new[0] = gain * ff_celt_postfilter_taps[tapset][0];
        block->pf_gains_new[1] = gain * ff_celt_postfilter_taps[tapset][1];
        block->pf_gains_new[2] = gain * ff_celt_postfilter_taps[tapset][2];
    }
}

static void exp_quant_coarse(OpusRangeCoder *rc, CeltFrame *f,
                             float last_energy[][CELT_MAX_BANDS], int intra)
{
    float alpha, beta, prev[2] = { 0, 0 };
    const uint8_t *pmod = ff_celt_coarse_energy_dist[f->size][intra];

    /* Inter is really just differential coding */
    if (opus_rc_tell(rc) + 3 <= f->framebits)
        ff_opus_rc_enc_log(rc, intra, 3);
    else
        intra = 0;

    if (intra) {
        alpha = 0.0f;
        beta  = 1.0f - (4915.0f/32768.0f);
    } else {
        alpha = ff_celt_alpha_coef[f->size];
        beta  = ff_celt_beta_coef[f->size];
    }

    for (int i = f->start_band; i < f->end_band; i++) {
        for (int ch = 0; ch < f->channels; ch++) {
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

static void celt_quant_coarse(CeltFrame *f, OpusRangeCoder *rc,
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

static void celt_quant_fine(CeltFrame *f, OpusRangeCoder *rc)
{
    for (int i = f->start_band; i < f->end_band; i++) {
        if (!f->fine_bits[i])
            continue;
        for (int ch = 0; ch < f->channels; ch++) {
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
    for (int priority = 0; priority < 2; priority++) {
        for (int i = f->start_band; i < f->end_band && (f->framebits - opus_rc_tell(rc)) >= f->channels; i++) {
            if (f->fine_priority[i] != priority || f->fine_bits[i] >= CELT_MAX_FINE_BITS)
                continue;
            for (int ch = 0; ch < f->channels; ch++) {
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

static void celt_encode_frame(OpusEncContext *s, OpusRangeCoder *rc,
                              CeltFrame *f, int index)
{
    ff_opus_rc_enc_init(rc);

    ff_opus_psy_celt_frame_init(&s->psyctx, f, index);

    celt_frame_setup_input(s, f);

    if (f->silence) {
        if (f->framebits >= 16)
            ff_opus_rc_enc_log(rc, 1, 15); /* Silence (if using explicit singalling) */
        for (int ch = 0; ch < s->channels; ch++)
            memset(s->last_quantized_energy[ch], 0.0f, sizeof(float)*CELT_MAX_BANDS);
        return;
    }

    /* Filters */
    celt_apply_preemph_filter(s, f);
    if (f->pfilter) {
        ff_opus_rc_enc_log(rc, 0, 15);
        celt_enc_quant_pfilter(rc, f);
    }

    /* Transform */
    celt_frame_mdct(s, f);

    /* Need to handle transient/non-transient switches at any point during analysis */
    while (ff_opus_psy_celt_frame_process(&s->psyctx, f, index))
        celt_frame_mdct(s, f);

    ff_opus_rc_enc_init(rc);

    /* Silence */
    ff_opus_rc_enc_log(rc, 0, 15);

    /* Pitch filter */
    if (!f->start_band && opus_rc_tell(rc) + 16 <= f->framebits)
        celt_enc_quant_pfilter(rc, f);

    /* Transient flag */
    if (f->size && opus_rc_tell(rc) + 3 <= f->framebits)
        ff_opus_rc_enc_log(rc, f->transient, 3);

    /* Main encoding */
    celt_quant_coarse  (f, rc, s->last_quantized_energy);
    celt_enc_tf        (f, rc);
    ff_celt_bitalloc   (f, rc, 1);
    celt_quant_fine    (f, rc);
    ff_celt_quant_bands(f, rc);

    /* Anticollapse bit */
    if (f->anticollapse_needed)
        ff_opus_rc_put_raw(rc, f->anticollapse, 1);

    /* Final per-band energy adjustments from leftover bits */
    celt_quant_final(s, rc, f);

    for (int ch = 0; ch < f->channels; ch++) {
        CeltBlock *block = &f->block[ch];
        for (int i = 0; i < CELT_MAX_BANDS; i++)
            s->last_quantized_energy[ch][i] = block->energy[i] + block->error_energy[i];
    }
}

static inline int write_opuslacing(uint8_t *dst, int v)
{
    dst[0] = FFMIN(v - FFALIGN(v - 255, 4), v);
    dst[1] = v - dst[0] >> 2;
    return 1 + (v >= 252);
}

static void opus_packet_assembler(OpusEncContext *s, AVPacket *avpkt)
{
    int offset, fsize_needed;

    /* Write toc */
    opus_gen_toc(s, avpkt->data, &offset, &fsize_needed);

    /* Frame sizes if needed */
    if (fsize_needed) {
        for (int i = 0; i < s->packet.frames - 1; i++) {
            offset += write_opuslacing(avpkt->data + offset,
                                       s->frame[i].framebits >> 3);
        }
    }

    /* Packets */
    for (int i = 0; i < s->packet.frames; i++) {
        ff_opus_rc_enc_end(&s->rc[i], avpkt->data + offset,
                           s->frame[i].framebits >> 3);
        offset += s->frame[i].framebits >> 3;
    }

    avpkt->size = offset;
}

/* Used as overlap for the first frame and padding for the last encoded packet */
static AVFrame *spawn_empty_frame(OpusEncContext *s)
{
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
    for (int i = 0; i < s->channels; i++) {
        size_t bps = av_get_bytes_per_sample(f->format);
        memset(f->extended_data[i], 0, bps*f->nb_samples);
    }
    return f;
}

static int opus_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                             const AVFrame *frame, int *got_packet_ptr)
{
    OpusEncContext *s = avctx->priv_data;
    int ret, frame_size, alloc_size = 0;

    if (frame) { /* Add new frame to queue */
        if ((ret = ff_af_queue_add(&s->afq, frame)) < 0)
            return ret;
        ff_bufqueue_add(avctx, &s->bufqueue, av_frame_clone(frame));
    } else {
        ff_opus_psy_signal_eof(&s->psyctx);
        if (!s->afq.remaining_samples || !avctx->frame_number)
            return 0; /* We've been flushed and there's nothing left to encode */
    }

    /* Run the psychoacoustic system */
    if (ff_opus_psy_process(&s->psyctx, &s->packet))
        return 0;

    frame_size = OPUS_BLOCK_SIZE(s->packet.framesize);

    if (!frame) {
        /* This can go negative, that's not a problem, we only pad if positive */
        int pad_empty = s->packet.frames*(frame_size/s->avctx->frame_size) - s->bufqueue.available + 1;
        /* Pad with empty 2.5 ms frames to whatever framesize was decided,
         * this should only happen at the very last flush frame. The frames
         * allocated here will be freed (because they have no other references)
         * after they get used by celt_frame_setup_input() */
        for (int i = 0; i < pad_empty; i++) {
            AVFrame *empty = spawn_empty_frame(s);
            if (!empty)
                return AVERROR(ENOMEM);
            ff_bufqueue_add(avctx, &s->bufqueue, empty);
        }
    }

    for (int i = 0; i < s->packet.frames; i++) {
        celt_encode_frame(s, &s->rc[i], &s->frame[i], i);
        alloc_size += s->frame[i].framebits >> 3;
    }

    /* Worst case toc + the frame lengths if needed */
    alloc_size += 2 + s->packet.frames*2;

    if ((ret = ff_alloc_packet2(avctx, avpkt, alloc_size, 0)) < 0)
        return ret;

    /* Assemble packet */
    opus_packet_assembler(s, avpkt);

    /* Update the psychoacoustic system */
    ff_opus_psy_postencode_update(&s->psyctx, s->frame, s->rc);

    /* Remove samples from queue and skip if needed */
    ff_af_queue_remove(&s->afq, s->packet.frames*frame_size, &avpkt->pts, &avpkt->duration);
    if (s->packet.frames*frame_size > avpkt->duration) {
        uint8_t *side = av_packet_new_side_data(avpkt, AV_PKT_DATA_SKIP_SAMPLES, 10);
        if (!side)
            return AVERROR(ENOMEM);
        AV_WL32(&side[4], s->packet.frames*frame_size - avpkt->duration + 120);
    }

    *got_packet_ptr = 1;

    return 0;
}

static av_cold int opus_encode_end(AVCodecContext *avctx)
{
    OpusEncContext *s = avctx->priv_data;

    for (int i = 0; i < CELT_BLOCK_NB; i++)
        ff_mdct15_uninit(&s->mdct[i]);

    ff_celt_pvq_uninit(&s->pvq);
    av_freep(&s->dsp);
    av_freep(&s->frame);
    av_freep(&s->rc);
    ff_af_queue_close(&s->afq);
    ff_opus_psy_end(&s->psyctx);
    ff_bufqueue_discard_all(&s->bufqueue);
    av_freep(&avctx->extradata);

    return 0;
}

static av_cold int opus_encode_init(AVCodecContext *avctx)
{
    int ret, max_frames;
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

    /* Extradata */
    avctx->extradata_size = 19;
    avctx->extradata = av_malloc(avctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);
    opus_write_extradata(avctx);

    ff_af_queue_init(avctx, &s->afq);

    if ((ret = ff_celt_pvq_init(&s->pvq, 1)) < 0)
        return ret;

    if (!(s->dsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT)))
        return AVERROR(ENOMEM);

    /* I have no idea why a base scaling factor of 68 works, could be the twiddles */
    for (int i = 0; i < CELT_BLOCK_NB; i++)
        if ((ret = ff_mdct15_init(&s->mdct[i], 0, i + 3, 68 << (CELT_BLOCK_NB - 1 - i))))
            return AVERROR(ENOMEM);

    /* Zero out previous energy (matters for inter first frame) */
    for (int ch = 0; ch < s->channels; ch++)
        memset(s->last_quantized_energy[ch], 0.0f, sizeof(float)*CELT_MAX_BANDS);

    /* Allocate an empty frame to use as overlap for the first frame of audio */
    ff_bufqueue_add(avctx, &s->bufqueue, spawn_empty_frame(s));
    if (!ff_bufqueue_peek(&s->bufqueue, 0))
        return AVERROR(ENOMEM);

    if ((ret = ff_opus_psy_init(&s->psyctx, s->avctx, &s->bufqueue, &s->options)))
        return ret;

    /* Frame structs and range coder buffers */
    max_frames = ceilf(FFMIN(s->options.max_delay_ms, 120.0f)/2.5f);
    s->frame = av_malloc(max_frames*sizeof(CeltFrame));
    if (!s->frame)
        return AVERROR(ENOMEM);
    s->rc = av_malloc(max_frames*sizeof(OpusRangeCoder));
    if (!s->rc)
        return AVERROR(ENOMEM);

    for (int i = 0; i < max_frames; i++) {
        s->frame[i].dsp = s->dsp;
        s->frame[i].avctx = s->avctx;
        s->frame[i].seed = 0;
        s->frame[i].pvq = s->pvq;
        s->frame[i].apply_phase_inv = 1;
        s->frame[i].block[0].emph_coeff = s->frame[i].block[1].emph_coeff = 0.0f;
    }

    return 0;
}

#define OPUSENC_FLAGS AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM
static const AVOption opusenc_options[] = {
    { "opus_delay", "Maximum delay in milliseconds", offsetof(OpusEncContext, options.max_delay_ms), AV_OPT_TYPE_FLOAT, { .dbl = OPUS_MAX_LOOKAHEAD }, 2.5f, OPUS_MAX_LOOKAHEAD, OPUSENC_FLAGS, "max_delay_ms" },
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
