/*
 * AAC encoder
 * Copyright (C) 2008 Konstantin Shishkov
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * AAC encoder
 */

/***********************************
 *              TODOs:
 * add sane pulse detection
 * add temporal noise shaping
 ***********************************/

#include "libavutil/opt.h"
#include "avcodec.h"
#include "put_bits.h"
#include "dsputil.h"
#include "internal.h"
#include "mpeg4audio.h"
#include "kbdwin.h"
#include "sinewin.h"

#include "aac.h"
#include "aactab.h"
#include "aacenc.h"

#include "psymodel.h"

#define AAC_MAX_CHANNELS 6

#define ERROR_IF(cond, ...) \
    if (cond) { \
        av_log(avctx, AV_LOG_ERROR, __VA_ARGS__); \
        return AVERROR(EINVAL); \
    }

float ff_aac_pow34sf_tab[428];

static const uint8_t swb_size_1024_96[] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8,
    12, 12, 12, 12, 12, 16, 16, 24, 28, 36, 44,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

static const uint8_t swb_size_1024_64[] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8,
    12, 12, 12, 16, 16, 16, 20, 24, 24, 28, 36,
    40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40, 40
};

static const uint8_t swb_size_1024_48[] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8,
    12, 12, 12, 12, 16, 16, 20, 20, 24, 24, 28, 28,
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
    96
};

static const uint8_t swb_size_1024_32[] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8,
    12, 12, 12, 12, 16, 16, 20, 20, 24, 24, 28, 28,
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32
};

static const uint8_t swb_size_1024_24[] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    12, 12, 12, 12, 16, 16, 16, 20, 20, 24, 24, 28, 28,
    32, 36, 36, 40, 44, 48, 52, 52, 64, 64, 64, 64, 64
};

static const uint8_t swb_size_1024_16[] = {
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 16, 16, 16, 16, 20, 20, 20, 24, 24, 28, 28,
    32, 36, 40, 40, 44, 48, 52, 56, 60, 64, 64, 64
};

static const uint8_t swb_size_1024_8[] = {
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    16, 16, 16, 16, 16, 16, 16, 20, 20, 20, 20, 24, 24, 24, 28, 28,
    32, 36, 36, 40, 44, 48, 52, 56, 60, 64, 80
};

static const uint8_t *swb_size_1024[] = {
    swb_size_1024_96, swb_size_1024_96, swb_size_1024_64,
    swb_size_1024_48, swb_size_1024_48, swb_size_1024_32,
    swb_size_1024_24, swb_size_1024_24, swb_size_1024_16,
    swb_size_1024_16, swb_size_1024_16, swb_size_1024_8
};

static const uint8_t swb_size_128_96[] = {
    4, 4, 4, 4, 4, 4, 8, 8, 8, 16, 28, 36
};

static const uint8_t swb_size_128_48[] = {
    4, 4, 4, 4, 4, 8, 8, 8, 12, 12, 12, 16, 16, 16
};

static const uint8_t swb_size_128_24[] = {
    4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 12, 12, 16, 16, 20
};

static const uint8_t swb_size_128_16[] = {
    4, 4, 4, 4, 4, 4, 4, 4, 8, 8, 12, 12, 16, 20, 20
};

static const uint8_t swb_size_128_8[] = {
    4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8, 12, 16, 20, 20
};

static const uint8_t *swb_size_128[] = {
    /* the last entry on the following row is swb_size_128_64 but is a
       duplicate of swb_size_128_96 */
    swb_size_128_96, swb_size_128_96, swb_size_128_96,
    swb_size_128_48, swb_size_128_48, swb_size_128_48,
    swb_size_128_24, swb_size_128_24, swb_size_128_16,
    swb_size_128_16, swb_size_128_16, swb_size_128_8
};

/** default channel configurations */
static const uint8_t aac_chan_configs[6][5] = {
 {1, TYPE_SCE},                               // 1 channel  - single channel element
 {1, TYPE_CPE},                               // 2 channels - channel pair
 {2, TYPE_SCE, TYPE_CPE},                     // 3 channels - center + stereo
 {3, TYPE_SCE, TYPE_CPE, TYPE_SCE},           // 4 channels - front center + stereo + back center
 {3, TYPE_SCE, TYPE_CPE, TYPE_CPE},           // 5 channels - front center + stereo + back stereo
 {4, TYPE_SCE, TYPE_CPE, TYPE_CPE, TYPE_LFE}, // 6 channels - front center + stereo + back stereo + LFE
};

/**
 * Table to remap channels from Libav's default order to AAC order.
 */
static const uint8_t aac_chan_maps[AAC_MAX_CHANNELS][AAC_MAX_CHANNELS] = {
    { 0 },
    { 0, 1 },
    { 2, 0, 1 },
    { 2, 0, 1, 3 },
    { 2, 0, 1, 3, 4 },
    { 2, 0, 1, 4, 5, 3 },
};

/**
 * Make AAC audio config object.
 * @see 1.6.2.1 "Syntax - AudioSpecificConfig"
 */
static void put_audio_specific_config(AVCodecContext *avctx)
{
    PutBitContext pb;
    AACEncContext *s = avctx->priv_data;

    init_put_bits(&pb, avctx->extradata, avctx->extradata_size*8);
    put_bits(&pb, 5, 2); //object type - AAC-LC
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
static void apply_ ##type ##_window(DSPContext *dsp, SingleChannelElement *sce, const float *audio)

WINDOW_FUNC(only_long)
{
    const float *lwindow = sce->ics.use_kb_window[0] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    const float *pwindow = sce->ics.use_kb_window[1] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    float *out = sce->ret;

    dsp->vector_fmul        (out,        audio,        lwindow, 1024);
    dsp->vector_fmul_reverse(out + 1024, audio + 1024, pwindow, 1024);
}

WINDOW_FUNC(long_start)
{
    const float *lwindow = sce->ics.use_kb_window[1] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    const float *swindow = sce->ics.use_kb_window[0] ? ff_aac_kbd_short_128 : ff_sine_128;
    float *out = sce->ret;

    dsp->vector_fmul(out, audio, lwindow, 1024);
    memcpy(out + 1024, audio + 1024, sizeof(out[0]) * 448);
    dsp->vector_fmul_reverse(out + 1024 + 448, audio + 1024 + 448, swindow, 128);
    memset(out + 1024 + 576, 0, sizeof(out[0]) * 448);
}

WINDOW_FUNC(long_stop)
{
    const float *lwindow = sce->ics.use_kb_window[0] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    const float *swindow = sce->ics.use_kb_window[1] ? ff_aac_kbd_short_128 : ff_sine_128;
    float *out = sce->ret;

    memset(out, 0, sizeof(out[0]) * 448);
    dsp->vector_fmul(out + 448, audio + 448, swindow, 128);
    memcpy(out + 576, audio + 576, sizeof(out[0]) * 448);
    dsp->vector_fmul_reverse(out + 1024, audio + 1024, lwindow, 1024);
}

WINDOW_FUNC(eight_short)
{
    const float *swindow = sce->ics.use_kb_window[0] ? ff_aac_kbd_short_128 : ff_sine_128;
    const float *pwindow = sce->ics.use_kb_window[1] ? ff_aac_kbd_short_128 : ff_sine_128;
    const float *in = audio + 448;
    float *out = sce->ret;
    int w;

    for (w = 0; w < 8; w++) {
        dsp->vector_fmul        (out, in, w ? pwindow : swindow, 128);
        out += 128;
        in  += 128;
        dsp->vector_fmul_reverse(out, in, swindow, 128);
        out += 128;
    }
}

static void (*const apply_window[4])(DSPContext *dsp, SingleChannelElement *sce, const float *audio) = {
    [ONLY_LONG_SEQUENCE]   = apply_only_long_window,
    [LONG_START_SEQUENCE]  = apply_long_start_window,
    [EIGHT_SHORT_SEQUENCE] = apply_eight_short_window,
    [LONG_STOP_SEQUENCE]   = apply_long_stop_window
};

static void apply_window_and_mdct(AACEncContext *s, SingleChannelElement *sce,
                                  float *audio)
{
    int i;
    float *output = sce->ret;

    apply_window[sce->ics.window_sequence[0]](&s->dsp, sce, audio);

    if (sce->ics.window_sequence[0] != EIGHT_SHORT_SEQUENCE)
        s->mdct1024.mdct_calc(&s->mdct1024, sce->coeffs, output);
    else
        for (i = 0; i < 1024; i += 128)
            s->mdct128.mdct_calc(&s->mdct128, sce->coeffs + i, output + i*2);
    memcpy(audio, audio + 1024, sizeof(audio[0]) * 1024);
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
        put_bits(&s->pb, 1, 0);            // no prediction
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
static void adjust_frame_information(AACEncContext *apc, ChannelElement *cpe, int chans)
{
    int i, w, w2, g, ch;
    int start, maxsfb, cmaxsfb;

    for (ch = 0; ch < chans; ch++) {
        IndividualChannelStream *ics = &cpe->ch[ch].ics;
        start = 0;
        maxsfb = 0;
        cpe->ch[ch].pulse.num_pulse = 0;
        for (w = 0; w < ics->num_windows*16; w += 16) {
            for (g = 0; g < ics->num_swb; g++) {
                //apply M/S
                if (cpe->common_window && !ch && cpe->ms_mask[w + g]) {
                    for (i = 0; i < ics->swb_sizes[g]; i++) {
                        cpe->ch[0].coeffs[start+i] = (cpe->ch[0].coeffs[start+i] + cpe->ch[1].coeffs[start+i]) / 2.0;
                        cpe->ch[1].coeffs[start+i] =  cpe->ch[0].coeffs[start+i] - cpe->ch[1].coeffs[start+i];
                    }
                }
                start += ics->swb_sizes[g];
            }
            for (cmaxsfb = ics->num_swb; cmaxsfb > 0 && cpe->ch[ch].zeroes[w+cmaxsfb-1]; cmaxsfb--)
                ;
            maxsfb = FFMAX(maxsfb, cmaxsfb);
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

/**
 * Encode scalefactor band coding type.
 */
static void encode_band_info(AACEncContext *s, SingleChannelElement *sce)
{
    int w;

    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w])
        s->coder->encode_window_bands_info(s, sce, w, sce->ics.group_len[w], s->lambda);
}

/**
 * Encode scalefactors.
 */
static void encode_scale_factors(AVCodecContext *avctx, AACEncContext *s,
                                 SingleChannelElement *sce)
{
    int off = sce->sf_idx[0], diff;
    int i, w;

    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
        for (i = 0; i < sce->ics.max_sfb; i++) {
            if (!sce->zeroes[w*16 + i]) {
                diff = sce->sf_idx[w*16 + i] - off + SCALE_DIFF_ZERO;
                if (diff < 0 || diff > 120)
                    av_log(avctx, AV_LOG_ERROR, "Scalefactor difference is too big to be coded\n");
                off = sce->sf_idx[w*16 + i];
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
            for (w2 = w; w2 < w + sce->ics.group_len[w]; w2++)
                s->coder->quantize_and_encode_band(s, &s->pb, sce->coeffs + start + w2*128,
                                                   sce->ics.swb_sizes[i],
                                                   sce->sf_idx[w*16 + i],
                                                   sce->band_type[w*16 + i],
                                                   s->lambda);
            start += sce->ics.swb_sizes[i];
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
    if (!common_window)
        put_ics_info(s, &sce->ics);
    encode_band_info(s, sce);
    encode_scale_factors(avctx, s, sce);
    encode_pulses(s, &sce->pulse);
    put_bits(&s->pb, 1, 0); //tns
    put_bits(&s->pb, 1, 0); //ssr
    encode_spectral_coeffs(s, sce);
    return 0;
}

/**
 * Write some auxiliary information about the created AAC file.
 */
static void put_bitstream_info(AVCodecContext *avctx, AACEncContext *s,
                               const char *name)
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
 * Deinterleave input samples.
 * Channels are reordered from Libav's default order to AAC order.
 */
static void deinterleave_input_samples(AACEncContext *s, const AVFrame *frame)
{
    int ch, i;
    const int sinc = s->channels;
    const uint8_t *channel_map = aac_chan_maps[sinc - 1];

    /* deinterleave and remap input samples */
    for (ch = 0; ch < sinc; ch++) {
        /* copy last 1024 samples of previous frame to the start of the current frame */
        memcpy(&s->planar_samples[ch][1024], &s->planar_samples[ch][2048], 1024 * sizeof(s->planar_samples[0][0]));

        /* deinterleave */
        i = 2048;
        if (frame) {
            const float *sptr = ((const float *)frame->data[0]) + channel_map[ch];
            for (; i < 2048 + frame->nb_samples; i++) {
                s->planar_samples[ch][i] = *sptr;
                sptr += sinc;
            }
        }
        memset(&s->planar_samples[ch][i], 0,
               (3072 - i) * sizeof(s->planar_samples[0][0]));
    }
}

static int aac_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                            const AVFrame *frame, int *got_packet_ptr)
{
    AACEncContext *s = avctx->priv_data;
    float **samples = s->planar_samples, *samples2, *la, *overlap;
    ChannelElement *cpe;
    int i, ch, w, g, chans, tag, start_ch, ret;
    int chan_el_counter[4];
    FFPsyWindowInfo windows[AAC_MAX_CHANNELS];

    if (s->last_frame == 2)
        return 0;

    /* add current frame to queue */
    if (frame) {
        if ((ret = ff_af_queue_add(&s->afq, frame) < 0))
            return ret;
    }

    deinterleave_input_samples(s, frame);
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
            for (w = 0; w < ics->num_windows; w++)
                ics->group_len[w] = wi[ch].grouping[w];

            apply_window_and_mdct(s, &cpe->ch[ch], overlap);
        }
        start_ch += chans;
    }
    if ((ret = ff_alloc_packet(avpkt, 768 * s->channels))) {
        av_log(avctx, AV_LOG_ERROR, "Error getting output packet\n");
        return ret;
    }

    do {
        int frame_bits;

        init_put_bits(&s->pb, avpkt->data, avpkt->size);

        if ((avctx->frame_number & 0xFF)==1 && !(avctx->flags & CODEC_FLAG_BITEXACT))
            put_bitstream_info(avctx, s, LIBAVCODEC_IDENT);
        start_ch = 0;
        memset(chan_el_counter, 0, sizeof(chan_el_counter));
        for (i = 0; i < s->chan_map[0]; i++) {
            FFPsyWindowInfo* wi = windows + start_ch;
            const float *coeffs[2];
            tag      = s->chan_map[i+1];
            chans    = tag == TYPE_CPE ? 2 : 1;
            cpe      = &s->cpe[i];
            put_bits(&s->pb, 3, tag);
            put_bits(&s->pb, 4, chan_el_counter[tag]++);
            for (ch = 0; ch < chans; ch++)
                coeffs[ch] = cpe->ch[ch].coeffs;
            s->psy.model->analyze(&s->psy, start_ch, coeffs, wi);
            for (ch = 0; ch < chans; ch++) {
                s->cur_channel = start_ch * 2 + ch;
                s->coder->search_for_quantizers(avctx, s, &cpe->ch[ch], s->lambda);
            }
            cpe->common_window = 0;
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
            s->cur_channel = start_ch * 2;
            if (s->options.stereo_mode && cpe->common_window) {
                if (s->options.stereo_mode > 0) {
                    IndividualChannelStream *ics = &cpe->ch[0].ics;
                    for (w = 0; w < ics->num_windows; w += ics->group_len[w])
                        for (g = 0;  g < ics->num_swb; g++)
                            cpe->ms_mask[w*16+g] = 1;
                } else if (s->coder->search_for_ms) {
                    s->coder->search_for_ms(s, cpe, s->lambda);
                }
            }
            adjust_frame_information(s, cpe, chans);
            if (chans == 2) {
                put_bits(&s->pb, 1, cpe->common_window);
                if (cpe->common_window) {
                    put_ics_info(s, &cpe->ch[0].ics);
                    encode_ms_info(&s->pb, cpe);
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

        s->lambda *= avctx->bit_rate * 1024.0f / avctx->sample_rate / frame_bits;

    } while (1);

    put_bits(&s->pb, 3, TYPE_END);
    flush_put_bits(&s->pb);
    avctx->frame_bits = put_bits_count(&s->pb);

    // rate control stuff
    if (!(avctx->flags & CODEC_FLAG_QSCALE)) {
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
    if (s->psypp)
        ff_psy_preprocess_end(s->psypp);
    av_freep(&s->buffer.samples);
    av_freep(&s->cpe);
    ff_af_queue_close(&s->afq);
#if FF_API_OLD_ENCODE_AUDIO
    av_freep(&avctx->coded_frame);
#endif
    return 0;
}

static av_cold int dsp_init(AVCodecContext *avctx, AACEncContext *s)
{
    int ret = 0;

    ff_dsputil_init(&s->dsp, avctx);

    // window init
    ff_kbd_window_init(ff_aac_kbd_long_1024, 4.0, 1024);
    ff_kbd_window_init(ff_aac_kbd_short_128, 6.0, 128);
    ff_init_ff_sine_windows(10);
    ff_init_ff_sine_windows(7);

    if (ret = ff_mdct_init(&s->mdct1024, 11, 0, 32768.0))
        return ret;
    if (ret = ff_mdct_init(&s->mdct128,   8, 0, 32768.0))
        return ret;

    return 0;
}

static av_cold int alloc_buffers(AVCodecContext *avctx, AACEncContext *s)
{
    int ch;
    FF_ALLOCZ_OR_GOTO(avctx, s->buffer.samples, 3 * 1024 * s->channels * sizeof(s->buffer.samples[0]), alloc_fail);
    FF_ALLOCZ_OR_GOTO(avctx, s->cpe, sizeof(ChannelElement) * s->chan_map[0], alloc_fail);
    FF_ALLOCZ_OR_GOTO(avctx, avctx->extradata, 5 + FF_INPUT_BUFFER_PADDING_SIZE, alloc_fail);

    for(ch = 0; ch < s->channels; ch++)
        s->planar_samples[ch] = s->buffer.samples + 3 * 1024 * ch;

#if FF_API_OLD_ENCODE_AUDIO
    if (!(avctx->coded_frame = avcodec_alloc_frame()))
        goto alloc_fail;
#endif

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

    ERROR_IF(i == 16,
             "Unsupported sample rate %d\n", avctx->sample_rate);
    ERROR_IF(s->channels > AAC_MAX_CHANNELS,
             "Unsupported number of channels: %d\n", s->channels);
    ERROR_IF(avctx->profile != FF_PROFILE_UNKNOWN && avctx->profile != FF_PROFILE_AAC_LOW,
             "Unsupported profile %d\n", avctx->profile);
    ERROR_IF(1024.0 * avctx->bit_rate / avctx->sample_rate > 6144 * s->channels,
             "Too many bits per frame requested\n");

    s->samplerate_index = i;

    s->chan_map = aac_chan_configs[s->channels-1];

    if (ret = dsp_init(avctx, s))
        goto fail;

    if (ret = alloc_buffers(avctx, s))
        goto fail;

    avctx->extradata_size = 5;
    put_audio_specific_config(avctx);

    sizes[0]   = swb_size_1024[i];
    sizes[1]   = swb_size_128[i];
    lengths[0] = ff_aac_num_swb_1024[i];
    lengths[1] = ff_aac_num_swb_128[i];
    for (i = 0; i < s->chan_map[0]; i++)
        grouping[i] = s->chan_map[i + 1] == TYPE_CPE;
    if (ret = ff_psy_init(&s->psy, avctx, 2, sizes, lengths, s->chan_map[0], grouping))
        goto fail;
    s->psypp = ff_psy_preprocess_init(avctx);
    s->coder = &ff_aac_coders[2];

    s->lambda = avctx->global_quality ? avctx->global_quality : 120;

    ff_aac_tableinit();

    for (i = 0; i < 428; i++)
        ff_aac_pow34sf_tab[i] = sqrt(ff_aac_pow2sf_tab[i] * sqrt(ff_aac_pow2sf_tab[i]));

    avctx->delay = 1024;
    ff_af_queue_init(avctx, &s->afq);

    return 0;
fail:
    aac_encode_end(avctx);
    return ret;
}

#define AACENC_FLAGS AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM
static const AVOption aacenc_options[] = {
    {"stereo_mode", "Stereo coding method", offsetof(AACEncContext, options.stereo_mode), AV_OPT_TYPE_INT, {.dbl = 0}, -1, 1, AACENC_FLAGS, "stereo_mode"},
        {"auto",     "Selected by the Encoder", 0, AV_OPT_TYPE_CONST, {.dbl = -1 }, INT_MIN, INT_MAX, AACENC_FLAGS, "stereo_mode"},
        {"ms_off",   "Disable Mid/Side coding", 0, AV_OPT_TYPE_CONST, {.dbl =  0 }, INT_MIN, INT_MAX, AACENC_FLAGS, "stereo_mode"},
        {"ms_force", "Force Mid/Side for the whole frame if possible", 0, AV_OPT_TYPE_CONST, {.dbl =  1 }, INT_MIN, INT_MAX, AACENC_FLAGS, "stereo_mode"},
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
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_AAC,
    .priv_data_size = sizeof(AACEncContext),
    .init           = aac_encode_init,
    .encode2        = aac_encode_frame,
    .close          = aac_encode_end,
    .capabilities   = CODEC_CAP_SMALL_LAST_FRAME | CODEC_CAP_DELAY |
                      CODEC_CAP_EXPERIMENTAL,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_FLT,
                                                     AV_SAMPLE_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("Advanced Audio Coding"),
    .priv_class     = &aacenc_class,
};
