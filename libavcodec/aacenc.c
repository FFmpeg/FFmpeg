/*
 * AAC encoder
 * Copyright (C) 2008 Konstantin Shishkov
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
 * AAC encoder
 */

/***********************************
 *              TODOs:
 * add sane pulse detection
 ***********************************/
#include <float.h>

#include "libavutil/channel_layout.h"
#include "libavutil/libm.h"
#include "libavutil/float_dsp.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "put_bits.h"
#include "mpeg4audio.h"
#include "sinewin.h"
#include "profiles.h"
#include "version.h"

#include "aac.h"
#include "aactab.h"
#include "aacenc.h"
#include "aacenctab.h"
#include "aacenc_utils.h"

#include "psymodel.h"

static void put_pce(PutBitContext *pb, AVCodecContext *avctx)
{
    int i, j;
    AACEncContext *s = avctx->priv_data;
    AACPCEInfo *pce = &s->pce;
    const int bitexact = avctx->flags & AV_CODEC_FLAG_BITEXACT;
    const char *aux_data = bitexact ? "Lavc" : LIBAVCODEC_IDENT;

    put_bits(pb, 4, 0);

    put_bits(pb, 2, avctx->profile);
    put_bits(pb, 4, s->samplerate_index);

    put_bits(pb, 4, pce->num_ele[0]); /* Front */
    put_bits(pb, 4, pce->num_ele[1]); /* Side */
    put_bits(pb, 4, pce->num_ele[2]); /* Back */
    put_bits(pb, 2, pce->num_ele[3]); /* LFE */
    put_bits(pb, 3, 0); /* Assoc data */
    put_bits(pb, 4, 0); /* CCs */

    put_bits(pb, 1, 0); /* Stereo mixdown */
    put_bits(pb, 1, 0); /* Mono mixdown */
    put_bits(pb, 1, 0); /* Something else */

    for (i = 0; i < 4; i++) {
        for (j = 0; j < pce->num_ele[i]; j++) {
            if (i < 3)
                put_bits(pb, 1, pce->pairing[i][j]);
            put_bits(pb, 4, pce->index[i][j]);
        }
    }

    align_put_bits(pb);
    put_bits(pb, 8, strlen(aux_data));
    ff_put_string(pb, aux_data, 0);
}

/**
 * Make AAC audio config object.
 * @see 1.6.2.1 "Syntax - AudioSpecificConfig"
 */
static int put_audio_specific_config(AVCodecContext *avctx)
{
    PutBitContext pb;
    AACEncContext *s = avctx->priv_data;
    int channels = (!s->needs_pce)*(s->channels - (s->channels == 8 ? 1 : 0));
    const int max_size = 32;

    avctx->extradata = av_mallocz(max_size);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);

    init_put_bits(&pb, avctx->extradata, max_size);
    put_bits(&pb, 5, s->profile+1); //profile
    put_bits(&pb, 4, s->samplerate_index); //sample rate index
    put_bits(&pb, 4, channels);
    //GASpecificConfig
    put_bits(&pb, 1, 0); //frame length - 1024 samples
    put_bits(&pb, 1, 0); //does not depend on core coder
    put_bits(&pb, 1, 0); //is not extension
    if (s->needs_pce)
        put_pce(&pb, avctx);

    //Explicitly Mark SBR absent
    put_bits(&pb, 11, 0x2b7); //sync extension
    put_bits(&pb, 5,  AOT_SBR);
    put_bits(&pb, 1,  0);
    flush_put_bits(&pb);
    avctx->extradata_size = put_bytes_output(&pb);

    return 0;
}

void ff_quantize_band_cost_cache_init(struct AACEncContext *s)
{
    ++s->quantize_band_cost_cache_generation;
    if (s->quantize_band_cost_cache_generation == 0) {
        memset(s->quantize_band_cost_cache, 0, sizeof(s->quantize_band_cost_cache));
        s->quantize_band_cost_cache_generation = 1;
    }
}

#define WINDOW_FUNC(type) \
static void apply_ ##type ##_window(AVFloatDSPContext *fdsp, \
                                    SingleChannelElement *sce, \
                                    const float *audio)

WINDOW_FUNC(only_long)
{
    const float *lwindow = sce->ics.use_kb_window[0] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    const float *pwindow = sce->ics.use_kb_window[1] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    float *out = sce->ret_buf;

    fdsp->vector_fmul        (out,        audio,        lwindow, 1024);
    fdsp->vector_fmul_reverse(out + 1024, audio + 1024, pwindow, 1024);
}

WINDOW_FUNC(long_start)
{
    const float *lwindow = sce->ics.use_kb_window[1] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    const float *swindow = sce->ics.use_kb_window[0] ? ff_aac_kbd_short_128 : ff_sine_128;
    float *out = sce->ret_buf;

    fdsp->vector_fmul(out, audio, lwindow, 1024);
    memcpy(out + 1024, audio + 1024, sizeof(out[0]) * 448);
    fdsp->vector_fmul_reverse(out + 1024 + 448, audio + 1024 + 448, swindow, 128);
    memset(out + 1024 + 576, 0, sizeof(out[0]) * 448);
}

WINDOW_FUNC(long_stop)
{
    const float *lwindow = sce->ics.use_kb_window[0] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    const float *swindow = sce->ics.use_kb_window[1] ? ff_aac_kbd_short_128 : ff_sine_128;
    float *out = sce->ret_buf;

    memset(out, 0, sizeof(out[0]) * 448);
    fdsp->vector_fmul(out + 448, audio + 448, swindow, 128);
    memcpy(out + 576, audio + 576, sizeof(out[0]) * 448);
    fdsp->vector_fmul_reverse(out + 1024, audio + 1024, lwindow, 1024);
}

WINDOW_FUNC(eight_short)
{
    const float *swindow = sce->ics.use_kb_window[0] ? ff_aac_kbd_short_128 : ff_sine_128;
    const float *pwindow = sce->ics.use_kb_window[1] ? ff_aac_kbd_short_128 : ff_sine_128;
    const float *in = audio + 448;
    float *out = sce->ret_buf;
    int w;

    for (w = 0; w < 8; w++) {
        fdsp->vector_fmul        (out, in, w ? pwindow : swindow, 128);
        out += 128;
        in  += 128;
        fdsp->vector_fmul_reverse(out, in, swindow, 128);
        out += 128;
    }
}

static void (*const apply_window[4])(AVFloatDSPContext *fdsp,
                                     SingleChannelElement *sce,
                                     const float *audio) = {
    [ONLY_LONG_SEQUENCE]   = apply_only_long_window,
    [LONG_START_SEQUENCE]  = apply_long_start_window,
    [EIGHT_SHORT_SEQUENCE] = apply_eight_short_window,
    [LONG_STOP_SEQUENCE]   = apply_long_stop_window
};

static void apply_window_and_mdct(AACEncContext *s, SingleChannelElement *sce,
                                  float *audio)
{
    int i;
    const float *output = sce->ret_buf;

    apply_window[sce->ics.window_sequence[0]](s->fdsp, sce, audio);

    if (sce->ics.window_sequence[0] != EIGHT_SHORT_SEQUENCE)
        s->mdct1024.mdct_calc(&s->mdct1024, sce->coeffs, output);
    else
        for (i = 0; i < 1024; i += 128)
            s->mdct128.mdct_calc(&s->mdct128, &sce->coeffs[i], output + i*2);
    memcpy(audio, audio + 1024, sizeof(audio[0]) * 1024);
    memcpy(sce->pcoeffs, sce->coeffs, sizeof(sce->pcoeffs));
}

/**
 * Encode ics_info element.
 * @see Table 4.6 (syntax of ics_info)
 */
static void put_ics_info(AACEncContext *s, IndividualChannelStream *info)
{
    int w;

    put_bits(&s->pb, 1, 0);                // ics_reserved bit
    put_bits(&s->pb, 2, info->window_sequence[0]);
    put_bits(&s->pb, 1, info->use_kb_window[0]);
    if (info->window_sequence[0] != EIGHT_SHORT_SEQUENCE) {
        put_bits(&s->pb, 6, info->max_sfb);
        put_bits(&s->pb, 1, !!info->predictor_present);
    } else {
        put_bits(&s->pb, 4, info->max_sfb);
        for (w = 1; w < 8; w++)
            put_bits(&s->pb, 1, !info->group_len[w]);
    }
}

/**
 * Encode MS data.
 * @see 4.6.8.1 "Joint Coding - M/S Stereo"
 */
static void encode_ms_info(PutBitContext *pb, ChannelElement *cpe)
{
    int i, w;

    put_bits(pb, 2, cpe->ms_mode);
    if (cpe->ms_mode == 1)
        for (w = 0; w < cpe->ch[0].ics.num_windows; w += cpe->ch[0].ics.group_len[w])
            for (i = 0; i < cpe->ch[0].ics.max_sfb; i++)
                put_bits(pb, 1, cpe->ms_mask[w*16 + i]);
}

/**
 * Produce integer coefficients from scalefactors provided by the model.
 */
static void adjust_frame_information(ChannelElement *cpe, int chans)
{
    int i, w, w2, g, ch;
    int maxsfb, cmaxsfb;

    for (ch = 0; ch < chans; ch++) {
        IndividualChannelStream *ics = &cpe->ch[ch].ics;
        maxsfb = 0;
        cpe->ch[ch].pulse.num_pulse = 0;
        for (w = 0; w < ics->num_windows; w += ics->group_len[w]) {
            for (w2 =  0; w2 < ics->group_len[w]; w2++) {
                for (cmaxsfb = ics->num_swb; cmaxsfb > 0 && cpe->ch[ch].zeroes[w*16+cmaxsfb-1]; cmaxsfb--)
                    ;
                maxsfb = FFMAX(maxsfb, cmaxsfb);
            }
        }
        ics->max_sfb = maxsfb;

        //adjust zero bands for window groups
        for (w = 0; w < ics->num_windows; w += ics->group_len[w]) {
            for (g = 0; g < ics->max_sfb; g++) {
                i = 1;
                for (w2 = w; w2 < w + ics->group_len[w]; w2++) {
                    if (!cpe->ch[ch].zeroes[w2*16 + g]) {
                        i = 0;
                        break;
                    }
                }
                cpe->ch[ch].zeroes[w*16 + g] = i;
            }
        }
    }

    if (chans > 1 && cpe->common_window) {
        IndividualChannelStream *ics0 = &cpe->ch[0].ics;
        IndividualChannelStream *ics1 = &cpe->ch[1].ics;
        int msc = 0;
        ics0->max_sfb = FFMAX(ics0->max_sfb, ics1->max_sfb);
        ics1->max_sfb = ics0->max_sfb;
        for (w = 0; w < ics0->num_windows*16; w += 16)
            for (i = 0; i < ics0->max_sfb; i++)
                if (cpe->ms_mask[w+i])
                    msc++;
        if (msc == 0 || ics0->max_sfb == 0)
            cpe->ms_mode = 0;
        else
            cpe->ms_mode = msc < ics0->max_sfb * ics0->num_windows ? 1 : 2;
    }
}

static void apply_intensity_stereo(ChannelElement *cpe)
{
    int w, w2, g, i;
    IndividualChannelStream *ics = &cpe->ch[0].ics;
    if (!cpe->common_window)
        return;
    for (w = 0; w < ics->num_windows; w += ics->group_len[w]) {
        for (w2 =  0; w2 < ics->group_len[w]; w2++) {
            int start = (w+w2) * 128;
            for (g = 0; g < ics->num_swb; g++) {
                int p  = -1 + 2 * (cpe->ch[1].band_type[w*16+g] - 14);
                float scale = cpe->ch[0].is_ener[w*16+g];
                if (!cpe->is_mask[w*16 + g]) {
                    start += ics->swb_sizes[g];
                    continue;
                }
                if (cpe->ms_mask[w*16 + g])
                    p *= -1;
                for (i = 0; i < ics->swb_sizes[g]; i++) {
                    float sum = (cpe->ch[0].coeffs[start+i] + p*cpe->ch[1].coeffs[start+i])*scale;
                    cpe->ch[0].coeffs[start+i] = sum;
                    cpe->ch[1].coeffs[start+i] = 0.0f;
                }
                start += ics->swb_sizes[g];
            }
        }
    }
}

static void apply_mid_side_stereo(ChannelElement *cpe)
{
    int w, w2, g, i;
    IndividualChannelStream *ics = &cpe->ch[0].ics;
    if (!cpe->common_window)
        return;
    for (w = 0; w < ics->num_windows; w += ics->group_len[w]) {
        for (w2 =  0; w2 < ics->group_len[w]; w2++) {
            int start = (w+w2) * 128;
            for (g = 0; g < ics->num_swb; g++) {
                /* ms_mask can be used for other purposes in PNS and I/S,
                 * so must not apply M/S if any band uses either, even if
                 * ms_mask is set.
                 */
                if (!cpe->ms_mask[w*16 + g] || cpe->is_mask[w*16 + g]
                    || cpe->ch[0].band_type[w*16 + g] >= NOISE_BT
                    || cpe->ch[1].band_type[w*16 + g] >= NOISE_BT) {
                    start += ics->swb_sizes[g];
                    continue;
                }
                for (i = 0; i < ics->swb_sizes[g]; i++) {
                    float L = (cpe->ch[0].coeffs[start+i] + cpe->ch[1].coeffs[start+i]) * 0.5f;
                    float R = L - cpe->ch[1].coeffs[start+i];
                    cpe->ch[0].coeffs[start+i] = L;
                    cpe->ch[1].coeffs[start+i] = R;
                }
                start += ics->swb_sizes[g];
            }
        }
    }
}

/**
 * Encode scalefactor band coding type.
 */
static void encode_band_info(AACEncContext *s, SingleChannelElement *sce)
{
    int w;

    if (s->coder->set_special_band_scalefactors)
        s->coder->set_special_band_scalefactors(s, sce);

    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w])
        s->coder->encode_window_bands_info(s, sce, w, sce->ics.group_len[w], s->lambda);
}

/**
 * Encode scalefactors.
 */
static void encode_scale_factors(AVCodecContext *avctx, AACEncContext *s,
                                 SingleChannelElement *sce)
{
    int diff, off_sf = sce->sf_idx[0], off_pns = sce->sf_idx[0] - NOISE_OFFSET;
    int off_is = 0, noise_flag = 1;
    int i, w;

    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
        for (i = 0; i < sce->ics.max_sfb; i++) {
            if (!sce->zeroes[w*16 + i]) {
                if (sce->band_type[w*16 + i] == NOISE_BT) {
                    diff = sce->sf_idx[w*16 + i] - off_pns;
                    off_pns = sce->sf_idx[w*16 + i];
                    if (noise_flag-- > 0) {
                        put_bits(&s->pb, NOISE_PRE_BITS, diff + NOISE_PRE);
                        continue;
                    }
                } else if (sce->band_type[w*16 + i] == INTENSITY_BT  ||
                           sce->band_type[w*16 + i] == INTENSITY_BT2) {
                    diff = sce->sf_idx[w*16 + i] - off_is;
                    off_is = sce->sf_idx[w*16 + i];
                } else {
                    diff = sce->sf_idx[w*16 + i] - off_sf;
                    off_sf = sce->sf_idx[w*16 + i];
                }
                diff += SCALE_DIFF_ZERO;
                av_assert0(diff >= 0 && diff <= 120);
                put_bits(&s->pb, ff_aac_scalefactor_bits[diff], ff_aac_scalefactor_code[diff]);
            }
        }
    }
}

/**
 * Encode pulse data.
 */
static void encode_pulses(AACEncContext *s, Pulse *pulse)
{
    int i;

    put_bits(&s->pb, 1, !!pulse->num_pulse);
    if (!pulse->num_pulse)
        return;

    put_bits(&s->pb, 2, pulse->num_pulse - 1);
    put_bits(&s->pb, 6, pulse->start);
    for (i = 0; i < pulse->num_pulse; i++) {
        put_bits(&s->pb, 5, pulse->pos[i]);
        put_bits(&s->pb, 4, pulse->amp[i]);
    }
}

/**
 * Encode spectral coefficients processed by psychoacoustic model.
 */
static void encode_spectral_coeffs(AACEncContext *s, SingleChannelElement *sce)
{
    int start, i, w, w2;

    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
        start = 0;
        for (i = 0; i < sce->ics.max_sfb; i++) {
            if (sce->zeroes[w*16 + i]) {
                start += sce->ics.swb_sizes[i];
                continue;
            }
            for (w2 = w; w2 < w + sce->ics.group_len[w]; w2++) {
                s->coder->quantize_and_encode_band(s, &s->pb,
                                                   &sce->coeffs[start + w2*128],
                                                   NULL, sce->ics.swb_sizes[i],
                                                   sce->sf_idx[w*16 + i],
                                                   sce->band_type[w*16 + i],
                                                   s->lambda,
                                                   sce->ics.window_clipping[w]);
            }
            start += sce->ics.swb_sizes[i];
        }
    }
}

/**
 * Downscale spectral coefficients for near-clipping windows to avoid artifacts
 */
static void avoid_clipping(AACEncContext *s, SingleChannelElement *sce)
{
    int start, i, j, w;

    if (sce->ics.clip_avoidance_factor < 1.0f) {
        for (w = 0; w < sce->ics.num_windows; w++) {
            start = 0;
            for (i = 0; i < sce->ics.max_sfb; i++) {
                float *swb_coeffs = &sce->coeffs[start + w*128];
                for (j = 0; j < sce->ics.swb_sizes[i]; j++)
                    swb_coeffs[j] *= sce->ics.clip_avoidance_factor;
                start += sce->ics.swb_sizes[i];
            }
        }
    }
}

/**
 * Encode one channel of audio data.
 */
static int encode_individual_channel(AVCodecContext *avctx, AACEncContext *s,
                                     SingleChannelElement *sce,
                                     int common_window)
{
    put_bits(&s->pb, 8, sce->sf_idx[0]);
    if (!common_window) {
        put_ics_info(s, &sce->ics);
        if (s->coder->encode_main_pred)
            s->coder->encode_main_pred(s, sce);
        if (s->coder->encode_ltp_info)
            s->coder->encode_ltp_info(s, sce, 0);
    }
    encode_band_info(s, sce);
    encode_scale_factors(avctx, s, sce);
    encode_pulses(s, &sce->pulse);
    put_bits(&s->pb, 1, !!sce->tns.present);
    if (s->coder->encode_tns_info)
        s->coder->encode_tns_info(s, sce);
    put_bits(&s->pb, 1, 0); //ssr
    encode_spectral_coeffs(s, sce);
    return 0;
}

/**
 * Write some auxiliary information about the created AAC file.
 */
static void put_bitstream_info(AACEncContext *s, const char *name)
{
    int i, namelen, padbits;

    namelen = strlen(name) + 2;
    put_bits(&s->pb, 3, TYPE_FIL);
    put_bits(&s->pb, 4, FFMIN(namelen, 15));
    if (namelen >= 15)
        put_bits(&s->pb, 8, namelen - 14);
    put_bits(&s->pb, 4, 0); //extension type - filler
    padbits = -put_bits_count(&s->pb) & 7;
    align_put_bits(&s->pb);
    for (i = 0; i < namelen - 2; i++)
        put_bits(&s->pb, 8, name[i]);
    put_bits(&s->pb, 12 - padbits, 0);
}

/*
 * Copy input samples.
 * Channels are reordered from libavcodec's default order to AAC order.
 */
static void copy_input_samples(AACEncContext *s, const AVFrame *frame)
{
    int ch;
    int end = 2048 + (frame ? frame->nb_samples : 0);
    const uint8_t *channel_map = s->reorder_map;

    /* copy and remap input samples */
    for (ch = 0; ch < s->channels; ch++) {
        /* copy last 1024 samples of previous frame to the start of the current frame */
        memcpy(&s->planar_samples[ch][1024], &s->planar_samples[ch][2048], 1024 * sizeof(s->planar_samples[0][0]));

        /* copy new samples and zero any remaining samples */
        if (frame) {
            memcpy(&s->planar_samples[ch][2048],
                   frame->extended_data[channel_map[ch]],
                   frame->nb_samples * sizeof(s->planar_samples[0][0]));
        }
        memset(&s->planar_samples[ch][end], 0,
               (3072 - end) * sizeof(s->planar_samples[0][0]));
    }
}

static int aac_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                            const AVFrame *frame, int *got_packet_ptr)
{
    AACEncContext *s = avctx->priv_data;
    float **samples = s->planar_samples, *samples2, *la, *overlap;
    ChannelElement *cpe;
    SingleChannelElement *sce;
    IndividualChannelStream *ics;
    int i, its, ch, w, chans, tag, start_ch, ret, frame_bits;
    int target_bits, rate_bits, too_many_bits, too_few_bits;
    int ms_mode = 0, is_mode = 0, tns_mode = 0, pred_mode = 0;
    int chan_el_counter[4];
    FFPsyWindowInfo windows[AAC_MAX_CHANNELS];

    /* add current frame to queue */
    if (frame) {
        if ((ret = ff_af_queue_add(&s->afq, frame)) < 0)
            return ret;
    } else {
        if (!s->afq.remaining_samples || (!s->afq.frame_alloc && !s->afq.frame_count))
            return 0;
    }

    copy_input_samples(s, frame);
    if (s->psypp)
        ff_psy_preprocess(s->psypp, s->planar_samples, s->channels);

    if (!avctx->frame_number)
        return 0;

    start_ch = 0;
    for (i = 0; i < s->chan_map[0]; i++) {
        FFPsyWindowInfo* wi = windows + start_ch;
        tag      = s->chan_map[i+1];
        chans    = tag == TYPE_CPE ? 2 : 1;
        cpe      = &s->cpe[i];
        for (ch = 0; ch < chans; ch++) {
            int k;
            float clip_avoidance_factor;
            sce = &cpe->ch[ch];
            ics = &sce->ics;
            s->cur_channel = start_ch + ch;
            overlap  = &samples[s->cur_channel][0];
            samples2 = overlap + 1024;
            la       = samples2 + (448+64);
            if (!frame)
                la = NULL;
            if (tag == TYPE_LFE) {
                wi[ch].window_type[0] = wi[ch].window_type[1] = ONLY_LONG_SEQUENCE;
                wi[ch].window_shape   = 0;
                wi[ch].num_windows    = 1;
                wi[ch].grouping[0]    = 1;
                wi[ch].clipping[0]    = 0;

                /* Only the lowest 12 coefficients are used in a LFE channel.
                 * The expression below results in only the bottom 8 coefficients
                 * being used for 11.025kHz to 16kHz sample rates.
                 */
                ics->num_swb = s->samplerate_index >= 8 ? 1 : 3;
            } else {
                wi[ch] = s->psy.model->window(&s->psy, samples2, la, s->cur_channel,
                                              ics->window_sequence[0]);
            }
            ics->window_sequence[1] = ics->window_sequence[0];
            ics->window_sequence[0] = wi[ch].window_type[0];
            ics->use_kb_window[1]   = ics->use_kb_window[0];
            ics->use_kb_window[0]   = wi[ch].window_shape;
            ics->num_windows        = wi[ch].num_windows;
            ics->swb_sizes          = s->psy.bands    [ics->num_windows == 8];
            ics->num_swb            = tag == TYPE_LFE ? ics->num_swb : s->psy.num_bands[ics->num_windows == 8];
            ics->max_sfb            = FFMIN(ics->max_sfb, ics->num_swb);
            ics->swb_offset         = wi[ch].window_type[0] == EIGHT_SHORT_SEQUENCE ?
                                        ff_swb_offset_128 [s->samplerate_index]:
                                        ff_swb_offset_1024[s->samplerate_index];
            ics->tns_max_bands      = wi[ch].window_type[0] == EIGHT_SHORT_SEQUENCE ?
                                        ff_tns_max_bands_128 [s->samplerate_index]:
                                        ff_tns_max_bands_1024[s->samplerate_index];

            for (w = 0; w < ics->num_windows; w++)
                ics->group_len[w] = wi[ch].grouping[w];

            /* Calculate input sample maximums and evaluate clipping risk */
            clip_avoidance_factor = 0.0f;
            for (w = 0; w < ics->num_windows; w++) {
                const float *wbuf = overlap + w * 128;
                const int wlen = 2048 / ics->num_windows;
                float max = 0;
                int j;
                /* mdct input is 2 * output */
                for (j = 0; j < wlen; j++)
                    max = FFMAX(max, fabsf(wbuf[j]));
                wi[ch].clipping[w] = max;
            }
            for (w = 0; w < ics->num_windows; w++) {
                if (wi[ch].clipping[w] > CLIP_AVOIDANCE_FACTOR) {
                    ics->window_clipping[w] = 1;
                    clip_avoidance_factor = FFMAX(clip_avoidance_factor, wi[ch].clipping[w]);
                } else {
                    ics->window_clipping[w] = 0;
                }
            }
            if (clip_avoidance_factor > CLIP_AVOIDANCE_FACTOR) {
                ics->clip_avoidance_factor = CLIP_AVOIDANCE_FACTOR / clip_avoidance_factor;
            } else {
                ics->clip_avoidance_factor = 1.0f;
            }

            apply_window_and_mdct(s, sce, overlap);

            if (s->options.ltp && s->coder->update_ltp) {
                s->coder->update_ltp(s, sce);
                apply_window[sce->ics.window_sequence[0]](s->fdsp, sce, &sce->ltp_state[0]);
                s->mdct1024.mdct_calc(&s->mdct1024, sce->lcoeffs, sce->ret_buf);
            }

            for (k = 0; k < 1024; k++) {
                if (!(fabs(cpe->ch[ch].coeffs[k]) < 1E16)) { // Ensure headroom for energy calculation
                    av_log(avctx, AV_LOG_ERROR, "Input contains (near) NaN/+-Inf\n");
                    return AVERROR(EINVAL);
                }
            }
            avoid_clipping(s, sce);
        }
        start_ch += chans;
    }
    if ((ret = ff_alloc_packet(avctx, avpkt, 8192 * s->channels)) < 0)
        return ret;
    frame_bits = its = 0;
    do {
        init_put_bits(&s->pb, avpkt->data, avpkt->size);

        if ((avctx->frame_number & 0xFF)==1 && !(avctx->flags & AV_CODEC_FLAG_BITEXACT))
            put_bitstream_info(s, LIBAVCODEC_IDENT);
        start_ch = 0;
        target_bits = 0;
        memset(chan_el_counter, 0, sizeof(chan_el_counter));
        for (i = 0; i < s->chan_map[0]; i++) {
            FFPsyWindowInfo* wi = windows + start_ch;
            const float *coeffs[2];
            tag      = s->chan_map[i+1];
            chans    = tag == TYPE_CPE ? 2 : 1;
            cpe      = &s->cpe[i];
            cpe->common_window = 0;
            memset(cpe->is_mask, 0, sizeof(cpe->is_mask));
            memset(cpe->ms_mask, 0, sizeof(cpe->ms_mask));
            put_bits(&s->pb, 3, tag);
            put_bits(&s->pb, 4, chan_el_counter[tag]++);
            for (ch = 0; ch < chans; ch++) {
                sce = &cpe->ch[ch];
                coeffs[ch] = sce->coeffs;
                sce->ics.predictor_present = 0;
                sce->ics.ltp.present = 0;
                memset(sce->ics.ltp.used, 0, sizeof(sce->ics.ltp.used));
                memset(sce->ics.prediction_used, 0, sizeof(sce->ics.prediction_used));
                memset(&sce->tns, 0, sizeof(TemporalNoiseShaping));
                for (w = 0; w < 128; w++)
                    if (sce->band_type[w] > RESERVED_BT)
                        sce->band_type[w] = 0;
            }
            s->psy.bitres.alloc = -1;
            s->psy.bitres.bits = s->last_frame_pb_count / s->channels;
            s->psy.model->analyze(&s->psy, start_ch, coeffs, wi);
            if (s->psy.bitres.alloc > 0) {
                /* Lambda unused here on purpose, we need to take psy's unscaled allocation */
                target_bits += s->psy.bitres.alloc
                    * (s->lambda / (avctx->global_quality ? avctx->global_quality : 120));
                s->psy.bitres.alloc /= chans;
            }
            s->cur_type = tag;
            for (ch = 0; ch < chans; ch++) {
                s->cur_channel = start_ch + ch;
                if (s->options.pns && s->coder->mark_pns)
                    s->coder->mark_pns(s, avctx, &cpe->ch[ch]);
                s->coder->search_for_quantizers(avctx, s, &cpe->ch[ch], s->lambda);
            }
            if (chans > 1
                && wi[0].window_type[0] == wi[1].window_type[0]
                && wi[0].window_shape   == wi[1].window_shape) {

                cpe->common_window = 1;
                for (w = 0; w < wi[0].num_windows; w++) {
                    if (wi[0].grouping[w] != wi[1].grouping[w]) {
                        cpe->common_window = 0;
                        break;
                    }
                }
            }
            for (ch = 0; ch < chans; ch++) { /* TNS and PNS */
                sce = &cpe->ch[ch];
                s->cur_channel = start_ch + ch;
                if (s->options.tns && s->coder->search_for_tns)
                    s->coder->search_for_tns(s, sce);
                if (s->options.tns && s->coder->apply_tns_filt)
                    s->coder->apply_tns_filt(s, sce);
                if (sce->tns.present)
                    tns_mode = 1;
                if (s->options.pns && s->coder->search_for_pns)
                    s->coder->search_for_pns(s, avctx, sce);
            }
            s->cur_channel = start_ch;
            if (s->options.intensity_stereo) { /* Intensity Stereo */
                if (s->coder->search_for_is)
                    s->coder->search_for_is(s, avctx, cpe);
                if (cpe->is_mode) is_mode = 1;
                apply_intensity_stereo(cpe);
            }
            if (s->options.pred) { /* Prediction */
                for (ch = 0; ch < chans; ch++) {
                    sce = &cpe->ch[ch];
                    s->cur_channel = start_ch + ch;
                    if (s->options.pred && s->coder->search_for_pred)
                        s->coder->search_for_pred(s, sce);
                    if (cpe->ch[ch].ics.predictor_present) pred_mode = 1;
                }
                if (s->coder->adjust_common_pred)
                    s->coder->adjust_common_pred(s, cpe);
                for (ch = 0; ch < chans; ch++) {
                    sce = &cpe->ch[ch];
                    s->cur_channel = start_ch + ch;
                    if (s->options.pred && s->coder->apply_main_pred)
                        s->coder->apply_main_pred(s, sce);
                }
                s->cur_channel = start_ch;
            }
            if (s->options.mid_side) { /* Mid/Side stereo */
                if (s->options.mid_side == -1 && s->coder->search_for_ms)
                    s->coder->search_for_ms(s, cpe);
                else if (cpe->common_window)
                    memset(cpe->ms_mask, 1, sizeof(cpe->ms_mask));
                apply_mid_side_stereo(cpe);
            }
            adjust_frame_information(cpe, chans);
            if (s->options.ltp) { /* LTP */
                for (ch = 0; ch < chans; ch++) {
                    sce = &cpe->ch[ch];
                    s->cur_channel = start_ch + ch;
                    if (s->coder->search_for_ltp)
                        s->coder->search_for_ltp(s, sce, cpe->common_window);
                    if (sce->ics.ltp.present) pred_mode = 1;
                }
                s->cur_channel = start_ch;
                if (s->coder->adjust_common_ltp)
                    s->coder->adjust_common_ltp(s, cpe);
            }
            if (chans == 2) {
                put_bits(&s->pb, 1, cpe->common_window);
                if (cpe->common_window) {
                    put_ics_info(s, &cpe->ch[0].ics);
                    if (s->coder->encode_main_pred)
                        s->coder->encode_main_pred(s, &cpe->ch[0]);
                    if (s->coder->encode_ltp_info)
                        s->coder->encode_ltp_info(s, &cpe->ch[0], 1);
                    encode_ms_info(&s->pb, cpe);
                    if (cpe->ms_mode) ms_mode = 1;
                }
            }
            for (ch = 0; ch < chans; ch++) {
                s->cur_channel = start_ch + ch;
                encode_individual_channel(avctx, s, &cpe->ch[ch], cpe->common_window);
            }
            start_ch += chans;
        }

        if (avctx->flags & AV_CODEC_FLAG_QSCALE) {
            /* When using a constant Q-scale, don't mess with lambda */
            break;
        }

        /* rate control stuff
         * allow between the nominal bitrate, and what psy's bit reservoir says to target
         * but drift towards the nominal bitrate always
         */
        frame_bits = put_bits_count(&s->pb);
        rate_bits = avctx->bit_rate * 1024 / avctx->sample_rate;
        rate_bits = FFMIN(rate_bits, 6144 * s->channels - 3);
        too_many_bits = FFMAX(target_bits, rate_bits);
        too_many_bits = FFMIN(too_many_bits, 6144 * s->channels - 3);
        too_few_bits = FFMIN(FFMAX(rate_bits - rate_bits/4, target_bits), too_many_bits);

        /* When using ABR, be strict (but only for increasing) */
        too_few_bits = too_few_bits - too_few_bits/8;
        too_many_bits = too_many_bits + too_many_bits/2;

        if (   its == 0 /* for steady-state Q-scale tracking */
            || (its < 5 && (frame_bits < too_few_bits || frame_bits > too_many_bits))
            || frame_bits >= 6144 * s->channels - 3  )
        {
            float ratio = ((float)rate_bits) / frame_bits;

            if (frame_bits >= too_few_bits && frame_bits <= too_many_bits) {
                /*
                 * This path is for steady-state Q-scale tracking
                 * When frame bits fall within the stable range, we still need to adjust
                 * lambda to maintain it like so in a stable fashion (large jumps in lambda
                 * create artifacts and should be avoided), but slowly
                 */
                ratio = sqrtf(sqrtf(ratio));
                ratio = av_clipf(ratio, 0.9f, 1.1f);
            } else {
                /* Not so fast though */
                ratio = sqrtf(ratio);
            }
            s->lambda = av_clipf(s->lambda * ratio, FLT_EPSILON, 65536.f);

            /* Keep iterating if we must reduce and lambda is in the sky */
            if (ratio > 0.9f && ratio < 1.1f) {
                break;
            } else {
                if (is_mode || ms_mode || tns_mode || pred_mode) {
                    for (i = 0; i < s->chan_map[0]; i++) {
                        // Must restore coeffs
                        chans = tag == TYPE_CPE ? 2 : 1;
                        cpe = &s->cpe[i];
                        for (ch = 0; ch < chans; ch++)
                            memcpy(cpe->ch[ch].coeffs, cpe->ch[ch].pcoeffs, sizeof(cpe->ch[ch].coeffs));
                    }
                }
                its++;
            }
        } else {
            break;
        }
    } while (1);

    if (s->options.ltp && s->coder->ltp_insert_new_frame)
        s->coder->ltp_insert_new_frame(s);

    put_bits(&s->pb, 3, TYPE_END);
    flush_put_bits(&s->pb);

    s->last_frame_pb_count = put_bits_count(&s->pb);
    avpkt->size            = put_bytes_output(&s->pb);

    s->lambda_sum += s->lambda;
    s->lambda_count++;

    ff_af_queue_remove(&s->afq, avctx->frame_size, &avpkt->pts,
                       &avpkt->duration);

    *got_packet_ptr = 1;
    return 0;
}

static av_cold int aac_encode_end(AVCodecContext *avctx)
{
    AACEncContext *s = avctx->priv_data;

    av_log(avctx, AV_LOG_INFO, "Qavg: %.3f\n", s->lambda_count ? s->lambda_sum / s->lambda_count : NAN);

    ff_mdct_end(&s->mdct1024);
    ff_mdct_end(&s->mdct128);
    ff_psy_end(&s->psy);
    ff_lpc_end(&s->lpc);
    if (s->psypp)
        ff_psy_preprocess_end(s->psypp);
    av_freep(&s->buffer.samples);
    av_freep(&s->cpe);
    av_freep(&s->fdsp);
    ff_af_queue_close(&s->afq);
    return 0;
}

static av_cold int dsp_init(AVCodecContext *avctx, AACEncContext *s)
{
    int ret = 0;

    s->fdsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    // window init
    ff_aac_float_common_init();

    if ((ret = ff_mdct_init(&s->mdct1024, 11, 0, 32768.0)) < 0)
        return ret;
    if ((ret = ff_mdct_init(&s->mdct128,   8, 0, 32768.0)) < 0)
        return ret;

    return 0;
}

static av_cold int alloc_buffers(AVCodecContext *avctx, AACEncContext *s)
{
    int ch;
    if (!FF_ALLOCZ_TYPED_ARRAY(s->buffer.samples, s->channels * 3 * 1024) ||
        !FF_ALLOCZ_TYPED_ARRAY(s->cpe,            s->chan_map[0]))
        return AVERROR(ENOMEM);

    for(ch = 0; ch < s->channels; ch++)
        s->planar_samples[ch] = s->buffer.samples + 3 * 1024 * ch;

    return 0;
}

static av_cold int aac_encode_init(AVCodecContext *avctx)
{
    AACEncContext *s = avctx->priv_data;
    int i, ret = 0;
    const uint8_t *sizes[2];
    uint8_t grouping[AAC_MAX_CHANNELS];
    int lengths[2];

    /* Constants */
    s->last_frame_pb_count = 0;
    avctx->frame_size = 1024;
    avctx->initial_padding = 1024;
    s->lambda = avctx->global_quality > 0 ? avctx->global_quality : 120;

    /* Channel map and unspecified bitrate guessing */
    s->channels = avctx->ch_layout.nb_channels;

    s->needs_pce = 1;
    for (i = 0; i < FF_ARRAY_ELEMS(aac_normal_chan_layouts); i++) {
        if (!av_channel_layout_compare(&avctx->ch_layout, &aac_normal_chan_layouts[i])) {
            s->needs_pce = s->options.pce;
            break;
        }
    }

    if (s->needs_pce) {
        char buf[64];
        for (i = 0; i < FF_ARRAY_ELEMS(aac_pce_configs); i++)
            if (!av_channel_layout_compare(&avctx->ch_layout, &aac_pce_configs[i].layout))
                break;
        av_channel_layout_describe(&avctx->ch_layout, buf, sizeof(buf));
        if (i == FF_ARRAY_ELEMS(aac_pce_configs)) {
            av_log(avctx, AV_LOG_ERROR, "Unsupported channel layout \"%s\"\n", buf);
            return AVERROR(EINVAL);
        }
        av_log(avctx, AV_LOG_INFO, "Using a PCE to encode channel layout \"%s\"\n", buf);
        s->pce = aac_pce_configs[i];
        s->reorder_map = s->pce.reorder_map;
        s->chan_map = s->pce.config_map;
    } else {
        s->reorder_map = aac_chan_maps[s->channels - 1];
        s->chan_map = aac_chan_configs[s->channels - 1];
    }

    if (!avctx->bit_rate) {
        for (i = 1; i <= s->chan_map[0]; i++) {
            avctx->bit_rate += s->chan_map[i] == TYPE_CPE ? 128000 : /* Pair */
                               s->chan_map[i] == TYPE_LFE ? 16000  : /* LFE  */
                                                            69000  ; /* SCE  */
        }
    }

    /* Samplerate */
    for (i = 0; i < 16; i++)
        if (avctx->sample_rate == ff_mpeg4audio_sample_rates[i])
            break;
    s->samplerate_index = i;
    ERROR_IF(s->samplerate_index == 16 ||
             s->samplerate_index >= ff_aac_swb_size_1024_len ||
             s->samplerate_index >= ff_aac_swb_size_128_len,
             "Unsupported sample rate %d\n", avctx->sample_rate);

    /* Bitrate limiting */
    WARN_IF(1024.0 * avctx->bit_rate / avctx->sample_rate > 6144 * s->channels,
             "Too many bits %f > %d per frame requested, clamping to max\n",
             1024.0 * avctx->bit_rate / avctx->sample_rate,
             6144 * s->channels);
    avctx->bit_rate = (int64_t)FFMIN(6144 * s->channels / 1024.0 * avctx->sample_rate,
                                     avctx->bit_rate);

    /* Profile and option setting */
    avctx->profile = avctx->profile == FF_PROFILE_UNKNOWN ? FF_PROFILE_AAC_LOW :
                     avctx->profile;
    for (i = 0; i < FF_ARRAY_ELEMS(aacenc_profiles); i++)
        if (avctx->profile == aacenc_profiles[i])
            break;
    if (avctx->profile == FF_PROFILE_MPEG2_AAC_LOW) {
        avctx->profile = FF_PROFILE_AAC_LOW;
        ERROR_IF(s->options.pred,
                 "Main prediction unavailable in the \"mpeg2_aac_low\" profile\n");
        ERROR_IF(s->options.ltp,
                 "LTP prediction unavailable in the \"mpeg2_aac_low\" profile\n");
        WARN_IF(s->options.pns,
                "PNS unavailable in the \"mpeg2_aac_low\" profile, turning off\n");
        s->options.pns = 0;
    } else if (avctx->profile == FF_PROFILE_AAC_LTP) {
        s->options.ltp = 1;
        ERROR_IF(s->options.pred,
                 "Main prediction unavailable in the \"aac_ltp\" profile\n");
    } else if (avctx->profile == FF_PROFILE_AAC_MAIN) {
        s->options.pred = 1;
        ERROR_IF(s->options.ltp,
                 "LTP prediction unavailable in the \"aac_main\" profile\n");
    } else if (s->options.ltp) {
        avctx->profile = FF_PROFILE_AAC_LTP;
        WARN_IF(1,
                "Chainging profile to \"aac_ltp\"\n");
        ERROR_IF(s->options.pred,
                 "Main prediction unavailable in the \"aac_ltp\" profile\n");
    } else if (s->options.pred) {
        avctx->profile = FF_PROFILE_AAC_MAIN;
        WARN_IF(1,
                "Chainging profile to \"aac_main\"\n");
        ERROR_IF(s->options.ltp,
                 "LTP prediction unavailable in the \"aac_main\" profile\n");
    }
    s->profile = avctx->profile;

    /* Coder limitations */
    s->coder = &ff_aac_coders[s->options.coder];
    if (s->options.coder == AAC_CODER_ANMR) {
        ERROR_IF(avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL,
                 "The ANMR coder is considered experimental, add -strict -2 to enable!\n");
        s->options.intensity_stereo = 0;
        s->options.pns = 0;
    }
    ERROR_IF(s->options.ltp && avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL,
             "The LPT profile requires experimental compliance, add -strict -2 to enable!\n");

    /* M/S introduces horrible artifacts with multichannel files, this is temporary */
    if (s->channels > 3)
        s->options.mid_side = 0;

    if ((ret = dsp_init(avctx, s)) < 0)
        return ret;

    if ((ret = alloc_buffers(avctx, s)) < 0)
        return ret;

    if ((ret = put_audio_specific_config(avctx)))
        return ret;

    sizes[0]   = ff_aac_swb_size_1024[s->samplerate_index];
    sizes[1]   = ff_aac_swb_size_128[s->samplerate_index];
    lengths[0] = ff_aac_num_swb_1024[s->samplerate_index];
    lengths[1] = ff_aac_num_swb_128[s->samplerate_index];
    for (i = 0; i < s->chan_map[0]; i++)
        grouping[i] = s->chan_map[i + 1] == TYPE_CPE;
    if ((ret = ff_psy_init(&s->psy, avctx, 2, sizes, lengths,
                           s->chan_map[0], grouping)) < 0)
        return ret;
    s->psypp = ff_psy_preprocess_init(avctx);
    ff_lpc_init(&s->lpc, 2*avctx->frame_size, TNS_MAX_ORDER, FF_LPC_TYPE_LEVINSON);
    s->random_state = 0x1f2e3d4c;

    s->abs_pow34   = abs_pow34_v;
    s->quant_bands = quantize_bands;

    if (ARCH_X86)
        ff_aac_dsp_init_x86(s);

    if (HAVE_MIPSDSP)
        ff_aac_coder_init_mips(s);

    ff_af_queue_init(avctx, &s->afq);
    ff_aac_tableinit();

    return 0;
}

#define AACENC_FLAGS AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM
static const AVOption aacenc_options[] = {
    {"aac_coder", "Coding algorithm", offsetof(AACEncContext, options.coder), AV_OPT_TYPE_INT, {.i64 = AAC_CODER_TWOLOOP}, 0, AAC_CODER_NB-1, AACENC_FLAGS, "coder"},
        {"anmr",     "ANMR method",               0, AV_OPT_TYPE_CONST, {.i64 = AAC_CODER_ANMR},    INT_MIN, INT_MAX, AACENC_FLAGS, "coder"},
        {"twoloop",  "Two loop searching method", 0, AV_OPT_TYPE_CONST, {.i64 = AAC_CODER_TWOLOOP}, INT_MIN, INT_MAX, AACENC_FLAGS, "coder"},
        {"fast",     "Default fast search",       0, AV_OPT_TYPE_CONST, {.i64 = AAC_CODER_FAST},    INT_MIN, INT_MAX, AACENC_FLAGS, "coder"},
    {"aac_ms", "Force M/S stereo coding", offsetof(AACEncContext, options.mid_side), AV_OPT_TYPE_BOOL, {.i64 = -1}, -1, 1, AACENC_FLAGS},
    {"aac_is", "Intensity stereo coding", offsetof(AACEncContext, options.intensity_stereo), AV_OPT_TYPE_BOOL, {.i64 = 1}, -1, 1, AACENC_FLAGS},
    {"aac_pns", "Perceptual noise substitution", offsetof(AACEncContext, options.pns), AV_OPT_TYPE_BOOL, {.i64 = 1}, -1, 1, AACENC_FLAGS},
    {"aac_tns", "Temporal noise shaping", offsetof(AACEncContext, options.tns), AV_OPT_TYPE_BOOL, {.i64 = 1}, -1, 1, AACENC_FLAGS},
    {"aac_ltp", "Long term prediction", offsetof(AACEncContext, options.ltp), AV_OPT_TYPE_BOOL, {.i64 = 0}, -1, 1, AACENC_FLAGS},
    {"aac_pred", "AAC-Main prediction", offsetof(AACEncContext, options.pred), AV_OPT_TYPE_BOOL, {.i64 = 0}, -1, 1, AACENC_FLAGS},
    {"aac_pce", "Forces the use of PCEs", offsetof(AACEncContext, options.pce), AV_OPT_TYPE_BOOL, {.i64 = 0}, -1, 1, AACENC_FLAGS},
    FF_AAC_PROFILE_OPTS
    {NULL}
};

static const AVClass aacenc_class = {
    .class_name = "AAC encoder",
    .item_name  = av_default_item_name,
    .option     = aacenc_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const FFCodecDefault aac_encode_defaults[] = {
    { "b", "0" },
    { NULL }
};

const FFCodec ff_aac_encoder = {
    .p.name         = "aac",
    .p.long_name    = NULL_IF_CONFIG_SMALL("AAC (Advanced Audio Coding)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_AAC,
    .priv_data_size = sizeof(AACEncContext),
    .init           = aac_encode_init,
    FF_CODEC_ENCODE_CB(aac_encode_frame),
    .close          = aac_encode_end,
    .defaults       = aac_encode_defaults,
    .p.supported_samplerates = ff_mpeg4audio_sample_rates,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
    .p.capabilities = AV_CODEC_CAP_SMALL_LAST_FRAME | AV_CODEC_CAP_DELAY,
    .p.sample_fmts  = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_FLTP,
                                                     AV_SAMPLE_FMT_NONE },
    .p.priv_class   = &aacenc_class,
};
