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

#include "libavutil/float_dsp.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "put_bits.h"
#include "internal.h"
#include "mpeg4audio.h"
#include "kbdwin.h"
#include "sinewin.h"

#include "aac.h"
#include "aactab.h"
#include "aacenc.h"
#include "aacenctab.h"
#include "aacenc_utils.h"

#include "psymodel.h"

/**
 * Make AAC audio config object.
 * @see 1.6.2.1 "Syntax - AudioSpecificConfig"
 */
static void put_audio_specific_config(AVCodecContext *avctx)
{
    PutBitContext pb;
    AACEncContext *s = avctx->priv_data;

    init_put_bits(&pb, avctx->extradata, avctx->extradata_size);
    put_bits(&pb, 5, s->profile+1); //profile
    put_bits(&pb, 4, s->samplerate_index); //sample rate index
    put_bits(&pb, 4, s->channels);
    //GASpecificConfig
    put_bits(&pb, 1, 0); //frame length - 1024 samples
    put_bits(&pb, 1, 0); //does not depend on core coder
    put_bits(&pb, 1, 0); //is not extension

    //Explicitly Mark SBR absent
    put_bits(&pb, 11, 0x2b7); //sync extension
    put_bits(&pb, 5,  AOT_SBR);
    put_bits(&pb, 1,  0);
    flush_put_bits(&pb);
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
    float *output = sce->ret_buf;

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
                if (!cpe->ms_mask[w*16 + g]) {
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
    avpriv_align_put_bits(&s->pb);
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
    const uint8_t *channel_map = aac_chan_maps[s->channels - 1];

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
    int i, ch, w, chans, tag, start_ch, ret;
    int ms_mode = 0, is_mode = 0, tns_mode = 0, pred_mode = 0;
    int chan_el_counter[4];
    FFPsyWindowInfo windows[AAC_MAX_CHANNELS];

    if (s->last_frame == 2)
        return 0;

    /* add current frame to queue */
    if (frame) {
        if ((ret = ff_af_queue_add(&s->afq, frame)) < 0)
            return ret;
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
            IndividualChannelStream *ics = &cpe->ch[ch].ics;
            int cur_channel = start_ch + ch;
            float clip_avoidance_factor;
            overlap  = &samples[cur_channel][0];
            samples2 = overlap + 1024;
            la       = samples2 + (448+64);
            if (!frame)
                la = NULL;
            if (tag == TYPE_LFE) {
                wi[ch].window_type[0] = ONLY_LONG_SEQUENCE;
                wi[ch].window_shape   = 0;
                wi[ch].num_windows    = 1;
                wi[ch].grouping[0]    = 1;

                /* Only the lowest 12 coefficients are used in a LFE channel.
                 * The expression below results in only the bottom 8 coefficients
                 * being used for 11.025kHz to 16kHz sample rates.
                 */
                ics->num_swb = s->samplerate_index >= 8 ? 1 : 3;
            } else {
                wi[ch] = s->psy.model->window(&s->psy, samples2, la, cur_channel,
                                              ics->window_sequence[0]);
            }
            ics->window_sequence[1] = ics->window_sequence[0];
            ics->window_sequence[0] = wi[ch].window_type[0];
            ics->use_kb_window[1]   = ics->use_kb_window[0];
            ics->use_kb_window[0]   = wi[ch].window_shape;
            ics->num_windows        = wi[ch].num_windows;
            ics->swb_sizes          = s->psy.bands    [ics->num_windows == 8];
            ics->num_swb            = tag == TYPE_LFE ? ics->num_swb : s->psy.num_bands[ics->num_windows == 8];
            ics->swb_offset         = wi[ch].window_type[0] == EIGHT_SHORT_SEQUENCE ?
                                        ff_swb_offset_128 [s->samplerate_index]:
                                        ff_swb_offset_1024[s->samplerate_index];
            ics->tns_max_bands      = wi[ch].window_type[0] == EIGHT_SHORT_SEQUENCE ?
                                        ff_tns_max_bands_128 [s->samplerate_index]:
                                        ff_tns_max_bands_1024[s->samplerate_index];
            clip_avoidance_factor = 0.0f;
            for (w = 0; w < ics->num_windows; w++)
                ics->group_len[w] = wi[ch].grouping[w];
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

            apply_window_and_mdct(s, &cpe->ch[ch], overlap);
            if (isnan(cpe->ch->coeffs[0])) {
                av_log(avctx, AV_LOG_ERROR, "Input contains NaN\n");
                return AVERROR(EINVAL);
            }
            avoid_clipping(s, &cpe->ch[ch]);
        }
        start_ch += chans;
    }
    if ((ret = ff_alloc_packet2(avctx, avpkt, 8192 * s->channels, 0)) < 0)
        return ret;
    do {
        int frame_bits;

        init_put_bits(&s->pb, avpkt->data, avpkt->size);

        if ((avctx->frame_number & 0xFF)==1 && !(avctx->flags & AV_CODEC_FLAG_BITEXACT))
            put_bitstream_info(s, LIBAVCODEC_IDENT);
        start_ch = 0;
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
                memset(&sce->ics.prediction_used, 0, sizeof(sce->ics.prediction_used));
                memset(&sce->tns, 0, sizeof(TemporalNoiseShaping));
                for (w = 0; w < 128; w++)
                    if (sce->band_type[w] > RESERVED_BT)
                        sce->band_type[w] = 0;
            }
            s->psy.model->analyze(&s->psy, start_ch, coeffs, wi);
            for (ch = 0; ch < chans; ch++) {
                s->cur_channel = start_ch + ch;
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
                if (s->options.pns && s->coder->search_for_pns)
                    s->coder->search_for_pns(s, avctx, sce);
                if (s->options.tns && s->coder->search_for_tns)
                    s->coder->search_for_tns(s, sce);
                if (s->options.tns && s->coder->apply_tns_filt)
                    s->coder->apply_tns_filt(s, sce);
                if (sce->tns.present)
                    tns_mode = 1;
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
                if (s->coder->adjust_common_prediction)
                    s->coder->adjust_common_prediction(s, cpe);
                for (ch = 0; ch < chans; ch++) {
                    sce = &cpe->ch[ch];
                    s->cur_channel = start_ch + ch;
                    if (s->options.pred && s->coder->apply_main_pred)
                        s->coder->apply_main_pred(s, sce);
                }
                s->cur_channel = start_ch;
            }
            if (s->options.stereo_mode) { /* Mid/Side stereo */
                if (s->options.stereo_mode == -1 && s->coder->search_for_ms)
                    s->coder->search_for_ms(s, cpe);
                else if (cpe->common_window)
                    memset(cpe->ms_mask, 1, sizeof(cpe->ms_mask));
                for (w = 0; w < 128; w++)
                    cpe->ms_mask[w] = cpe->is_mask[w] ? 0 : cpe->ms_mask[w];
                apply_mid_side_stereo(cpe);
            }
            adjust_frame_information(cpe, chans);
            if (chans == 2) {
                put_bits(&s->pb, 1, cpe->common_window);
                if (cpe->common_window) {
                    put_ics_info(s, &cpe->ch[0].ics);
                    if (s->coder->encode_main_pred)
                        s->coder->encode_main_pred(s, &cpe->ch[0]);
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

        frame_bits = put_bits_count(&s->pb);
        if (frame_bits <= 6144 * s->channels - 3) {
            s->psy.bitres.bits = frame_bits / s->channels;
            break;
        }
        if (is_mode || ms_mode || tns_mode || pred_mode) {
            for (i = 0; i < s->chan_map[0]; i++) {
                // Must restore coeffs
                chans = tag == TYPE_CPE ? 2 : 1;
                cpe = &s->cpe[i];
                for (ch = 0; ch < chans; ch++)
                    memcpy(cpe->ch[ch].coeffs, cpe->ch[ch].pcoeffs, sizeof(cpe->ch[ch].coeffs));
            }
        }

        s->lambda *= avctx->bit_rate * 1024.0f / avctx->sample_rate / frame_bits;

    } while (1);

    put_bits(&s->pb, 3, TYPE_END);
    flush_put_bits(&s->pb);
    avctx->frame_bits = put_bits_count(&s->pb);

    // rate control stuff
    if (!(avctx->flags & AV_CODEC_FLAG_QSCALE)) {
        float ratio = avctx->bit_rate * 1024.0f / avctx->sample_rate / avctx->frame_bits;
        s->lambda *= ratio;
        s->lambda = FFMIN(s->lambda, 65536.f);
    }

    if (!frame)
        s->last_frame++;

    ff_af_queue_remove(&s->afq, avctx->frame_size, &avpkt->pts,
                       &avpkt->duration);

    avpkt->size = put_bits_count(&s->pb) >> 3;
    *got_packet_ptr = 1;
    return 0;
}

static av_cold int aac_encode_end(AVCodecContext *avctx)
{
    AACEncContext *s = avctx->priv_data;

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
    ff_kbd_window_init(ff_aac_kbd_long_1024, 4.0, 1024);
    ff_kbd_window_init(ff_aac_kbd_short_128, 6.0, 128);
    ff_init_ff_sine_windows(10);
    ff_init_ff_sine_windows(7);

    if ((ret = ff_mdct_init(&s->mdct1024, 11, 0, 32768.0)) < 0)
        return ret;
    if ((ret = ff_mdct_init(&s->mdct128,   8, 0, 32768.0)) < 0)
        return ret;

    return 0;
}

static av_cold int alloc_buffers(AVCodecContext *avctx, AACEncContext *s)
{
    int ch;
    FF_ALLOCZ_ARRAY_OR_GOTO(avctx, s->buffer.samples, s->channels, 3 * 1024 * sizeof(s->buffer.samples[0]), alloc_fail);
    FF_ALLOCZ_ARRAY_OR_GOTO(avctx, s->cpe, s->chan_map[0], sizeof(ChannelElement), alloc_fail);
    FF_ALLOCZ_OR_GOTO(avctx, avctx->extradata, 5 + AV_INPUT_BUFFER_PADDING_SIZE, alloc_fail);

    for(ch = 0; ch < s->channels; ch++)
        s->planar_samples[ch] = s->buffer.samples + 3 * 1024 * ch;

    return 0;
alloc_fail:
    return AVERROR(ENOMEM);
}

static av_cold int aac_encode_init(AVCodecContext *avctx)
{
    AACEncContext *s = avctx->priv_data;
    int i, ret = 0;
    const uint8_t *sizes[2];
    uint8_t grouping[AAC_MAX_CHANNELS];
    int lengths[2];

    avctx->frame_size = 1024;

    for (i = 0; i < 16; i++)
        if (avctx->sample_rate == avpriv_mpeg4audio_sample_rates[i])
            break;

    s->channels = avctx->channels;

    ERROR_IF(i == 16 || i >= ff_aac_swb_size_1024_len || i >= ff_aac_swb_size_128_len,
             "Unsupported sample rate %d\n", avctx->sample_rate);
    ERROR_IF(s->channels > AAC_MAX_CHANNELS,
             "Unsupported number of channels: %d\n", s->channels);
    WARN_IF(1024.0 * avctx->bit_rate / avctx->sample_rate > 6144 * s->channels,
             "Too many bits per frame requested, clamping to max\n");
    if (avctx->profile == FF_PROFILE_AAC_MAIN) {
        s->options.pred = 1;
    } else if ((avctx->profile == FF_PROFILE_AAC_LOW ||
                avctx->profile == FF_PROFILE_UNKNOWN) && s->options.pred) {
        s->profile = 0; /* Main */
        WARN_IF(1, "Prediction requested, changing profile to AAC-Main\n");
    } else if (avctx->profile == FF_PROFILE_AAC_LOW ||
               avctx->profile == FF_PROFILE_UNKNOWN) {
        s->profile = 1; /* Low */
    } else {
        ERROR_IF(1, "Unsupported profile %d\n", avctx->profile);
    }

    if (s->options.aac_coder != AAC_CODER_TWOLOOP) {
        s->options.intensity_stereo = 0;
        s->options.pns = 0;
    }

    avctx->bit_rate = (int)FFMIN(
        6144 * s->channels / 1024.0 * avctx->sample_rate,
        avctx->bit_rate);

    s->samplerate_index = i;

    s->chan_map = aac_chan_configs[s->channels-1];

    if ((ret = dsp_init(avctx, s)) < 0)
        goto fail;

    if ((ret = alloc_buffers(avctx, s)) < 0)
        goto fail;

    avctx->extradata_size = 5;
    put_audio_specific_config(avctx);

    sizes[0]   = ff_aac_swb_size_1024[i];
    sizes[1]   = ff_aac_swb_size_128[i];
    lengths[0] = ff_aac_num_swb_1024[i];
    lengths[1] = ff_aac_num_swb_128[i];
    for (i = 0; i < s->chan_map[0]; i++)
        grouping[i] = s->chan_map[i + 1] == TYPE_CPE;
    if ((ret = ff_psy_init(&s->psy, avctx, 2, sizes, lengths,
                           s->chan_map[0], grouping)) < 0)
        goto fail;
    s->psypp = ff_psy_preprocess_init(avctx);
    s->coder = &ff_aac_coders[s->options.aac_coder];
    ff_lpc_init(&s->lpc, 2*avctx->frame_size, TNS_MAX_ORDER, FF_LPC_TYPE_LEVINSON);

    if (HAVE_MIPSDSPR1)
        ff_aac_coder_init_mips(s);

    s->lambda = avctx->global_quality > 0 ? avctx->global_quality : 120;

    ff_aac_tableinit();

    avctx->initial_padding = 1024;
    ff_af_queue_init(avctx, &s->afq);

    return 0;
fail:
    aac_encode_end(avctx);
    return ret;
}

#define AACENC_FLAGS AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM
static const AVOption aacenc_options[] = {
    {"stereo_mode", "Stereo coding method", offsetof(AACEncContext, options.stereo_mode), AV_OPT_TYPE_INT, {.i64 = 0}, -1, 1, AACENC_FLAGS, "stereo_mode"},
        {"auto",     "Selected by the Encoder", 0, AV_OPT_TYPE_CONST, {.i64 = -1 }, INT_MIN, INT_MAX, AACENC_FLAGS, "stereo_mode"},
        {"ms_off",   "Disable Mid/Side coding", 0, AV_OPT_TYPE_CONST, {.i64 =  0 }, INT_MIN, INT_MAX, AACENC_FLAGS, "stereo_mode"},
        {"ms_force", "Force Mid/Side for the whole frame if possible", 0, AV_OPT_TYPE_CONST, {.i64 =  1 }, INT_MIN, INT_MAX, AACENC_FLAGS, "stereo_mode"},
    {"aac_coder", "Coding algorithm", offsetof(AACEncContext, options.aac_coder), AV_OPT_TYPE_INT, {.i64 = AAC_CODER_TWOLOOP}, 0, AAC_CODER_NB-1, AACENC_FLAGS, "aac_coder"},
        {"faac",     "FAAC-inspired method",      0, AV_OPT_TYPE_CONST, {.i64 = AAC_CODER_FAAC},    INT_MIN, INT_MAX, AACENC_FLAGS, "aac_coder"},
        {"anmr",     "ANMR method",               0, AV_OPT_TYPE_CONST, {.i64 = AAC_CODER_ANMR},    INT_MIN, INT_MAX, AACENC_FLAGS, "aac_coder"},
        {"twoloop",  "Two loop searching method", 0, AV_OPT_TYPE_CONST, {.i64 = AAC_CODER_TWOLOOP}, INT_MIN, INT_MAX, AACENC_FLAGS, "aac_coder"},
        {"fast",     "Constant quantizer",        0, AV_OPT_TYPE_CONST, {.i64 = AAC_CODER_FAST},    INT_MIN, INT_MAX, AACENC_FLAGS, "aac_coder"},
    {"aac_pns", "Perceptual Noise Substitution", offsetof(AACEncContext, options.pns), AV_OPT_TYPE_INT, {.i64 = 1}, 0, 1, AACENC_FLAGS, "aac_pns"},
        {"disable",  "Disable perceptual noise substitution", 0, AV_OPT_TYPE_CONST, {.i64 =  0 }, INT_MIN, INT_MAX, AACENC_FLAGS, "aac_pns"},
        {"enable",   "Enable perceptual noise substitution",  0, AV_OPT_TYPE_CONST, {.i64 =  1 }, INT_MIN, INT_MAX, AACENC_FLAGS, "aac_pns"},
    {"aac_is", "Intensity stereo coding", offsetof(AACEncContext, options.intensity_stereo), AV_OPT_TYPE_INT, {.i64 = 1}, 0, 1, AACENC_FLAGS, "intensity_stereo"},
        {"disable",  "Disable intensity stereo coding", 0, AV_OPT_TYPE_CONST, {.i64 = 0}, INT_MIN, INT_MAX, AACENC_FLAGS, "intensity_stereo"},
        {"enable",   "Enable intensity stereo coding", 0, AV_OPT_TYPE_CONST, {.i64 = 1}, INT_MIN, INT_MAX, AACENC_FLAGS, "intensity_stereo"},
    {"aac_tns", "Temporal noise shaping", offsetof(AACEncContext, options.tns), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, AACENC_FLAGS, "aac_tns"},
        {"disable",  "Disable temporal noise shaping", 0, AV_OPT_TYPE_CONST, {.i64 = 0}, INT_MIN, INT_MAX, AACENC_FLAGS, "aac_tns"},
        {"enable",   "Enable temporal noise shaping", 0, AV_OPT_TYPE_CONST, {.i64 = 1}, INT_MIN, INT_MAX, AACENC_FLAGS, "aac_tns"},
    {"aac_pred", "AAC-Main prediction", offsetof(AACEncContext, options.pred), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, AACENC_FLAGS, "aac_pred"},
        {"disable",  "Disable AAC-Main prediction", 0, AV_OPT_TYPE_CONST, {.i64 = 0}, INT_MIN, INT_MAX, AACENC_FLAGS, "aac_pred"},
        {"enable",   "Enable AAC-Main prediction", 0, AV_OPT_TYPE_CONST, {.i64 = 1}, INT_MIN, INT_MAX, AACENC_FLAGS, "aac_pred"},
    {NULL}
};

static const AVClass aacenc_class = {
    "AAC encoder",
    av_default_item_name,
    aacenc_options,
    LIBAVUTIL_VERSION_INT,
};

AVCodec ff_aac_encoder = {
    .name           = "aac",
    .long_name      = NULL_IF_CONFIG_SMALL("AAC (Advanced Audio Coding)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_AAC,
    .priv_data_size = sizeof(AACEncContext),
    .init           = aac_encode_init,
    .encode2        = aac_encode_frame,
    .close          = aac_encode_end,
    .supported_samplerates = mpeg4audio_sample_rates,
    .capabilities   = AV_CODEC_CAP_SMALL_LAST_FRAME | AV_CODEC_CAP_DELAY |
                      AV_CODEC_CAP_EXPERIMENTAL,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_FLTP,
                                                     AV_SAMPLE_FMT_NONE },
    .priv_class     = &aacenc_class,
};
