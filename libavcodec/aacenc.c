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
 * add temporal noise shaping
 ***********************************/

#include "libavutil/opt.h"
#include "avcodec.h"
#include "put_bits.h"
#include "dsputil.h"
#include "mpeg4audio.h"
#include "kbdwin.h"
#include "sinewin.h"

#include "aac.h"
#include "aactab.h"
#include "aacenc.h"

#include "psymodel.h"

#define AAC_MAX_CHANNELS 6

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

static const uint8_t channel_maps[][AAC_MAX_CHANNELS] = {
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
    put_bits(&pb, 4, avctx->channels);
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

static av_cold int aac_encode_init(AVCodecContext *avctx)
{
    AACEncContext *s = avctx->priv_data;
    int i;
    const uint8_t *sizes[2];
    uint8_t grouping[AAC_MAX_CHANNELS];
    int lengths[2];

    avctx->frame_size = 1024;

    for (i = 0; i < 16; i++)
        if (avctx->sample_rate == avpriv_mpeg4audio_sample_rates[i])
            break;
    if (i == 16) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported sample rate %d\n", avctx->sample_rate);
        return -1;
    }
    if (avctx->channels > AAC_MAX_CHANNELS) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported number of channels: %d\n", avctx->channels);
        return -1;
    }
    if (avctx->profile != FF_PROFILE_UNKNOWN && avctx->profile != FF_PROFILE_AAC_LOW) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported profile %d\n", avctx->profile);
        return -1;
    }
    if (1024.0 * avctx->bit_rate / avctx->sample_rate > 6144 * avctx->channels) {
        av_log(avctx, AV_LOG_ERROR, "Too many bits per frame requested\n");
        return -1;
    }
    s->samplerate_index = i;

    dsputil_init(&s->dsp, avctx);
    ff_mdct_init(&s->mdct1024, 11, 0, 1.0);
    ff_mdct_init(&s->mdct128,   8, 0, 1.0);
    // window init
    ff_kbd_window_init(ff_aac_kbd_long_1024, 4.0, 1024);
    ff_kbd_window_init(ff_aac_kbd_short_128, 6.0, 128);
    ff_init_ff_sine_windows(10);
    ff_init_ff_sine_windows(7);

    s->chan_map           = aac_chan_configs[avctx->channels-1];
    s->samples            = av_malloc(2 * 1024 * avctx->channels * sizeof(s->samples[0]));
    s->cpe                = av_mallocz(sizeof(ChannelElement) * s->chan_map[0]);
    avctx->extradata      = av_mallocz(5 + FF_INPUT_BUFFER_PADDING_SIZE);
    avctx->extradata_size = 5;
    put_audio_specific_config(avctx);

    sizes[0]   = swb_size_1024[i];
    sizes[1]   = swb_size_128[i];
    lengths[0] = ff_aac_num_swb_1024[i];
    lengths[1] = ff_aac_num_swb_128[i];
    for (i = 0; i < s->chan_map[0]; i++)
        grouping[i] = s->chan_map[i + 1] == TYPE_CPE;
    ff_psy_init(&s->psy, avctx, 2, sizes, lengths, s->chan_map[0], grouping);
    s->psypp = ff_psy_preprocess_init(avctx);
    s->coder = &ff_aac_coders[2];

    s->lambda = avctx->global_quality ? avctx->global_quality : 120;

    ff_aac_tableinit();

    return 0;
}

static void apply_window_and_mdct(AVCodecContext *avctx, AACEncContext *s,
                                  SingleChannelElement *sce, short *audio)
{
    int i, k;
    const int chans = avctx->channels;
    const float * lwindow = sce->ics.use_kb_window[0] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    const float * swindow = sce->ics.use_kb_window[0] ? ff_aac_kbd_short_128 : ff_sine_128;
    const float * pwindow = sce->ics.use_kb_window[1] ? ff_aac_kbd_short_128 : ff_sine_128;
    float *output = sce->ret;

    if (sce->ics.window_sequence[0] != EIGHT_SHORT_SEQUENCE) {
        memcpy(output, sce->saved, sizeof(float)*1024);
        if (sce->ics.window_sequence[0] == LONG_STOP_SEQUENCE) {
            memset(output, 0, sizeof(output[0]) * 448);
            for (i = 448; i < 576; i++)
                output[i] = sce->saved[i] * pwindow[i - 448];
            for (i = 576; i < 704; i++)
                output[i] = sce->saved[i];
        }
        if (sce->ics.window_sequence[0] != LONG_START_SEQUENCE) {
            for (i = 0; i < 1024; i++) {
                output[i+1024]         = audio[i * chans] * lwindow[1024 - i - 1];
                sce->saved[i] = audio[i * chans] * lwindow[i];
            }
        } else {
            for (i = 0; i < 448; i++)
                output[i+1024]         = audio[i * chans];
            for (; i < 576; i++)
                output[i+1024]         = audio[i * chans] * swindow[576 - i - 1];
            memset(output+1024+576, 0, sizeof(output[0]) * 448);
            for (i = 0; i < 1024; i++)
                sce->saved[i] = audio[i * chans];
        }
        s->mdct1024.mdct_calc(&s->mdct1024, sce->coeffs, output);
    } else {
        for (k = 0; k < 1024; k += 128) {
            for (i = 448 + k; i < 448 + k + 256; i++)
                output[i - 448 - k] = (i < 1024)
                                         ? sce->saved[i]
                                         : audio[(i-1024)*chans];
            s->dsp.vector_fmul        (output,     output, k ?  swindow : pwindow, 128);
            s->dsp.vector_fmul_reverse(output+128, output+128, swindow, 128);
            s->mdct128.mdct_calc(&s->mdct128, sce->coeffs + k, output);
        }
        for (i = 0; i < 1024; i++)
            sce->saved[i] = audio[i * chans];
    }
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
        put_bits(&s->pb, 8, namelen - 16);
    put_bits(&s->pb, 4, 0); //extension type - filler
    padbits = 8 - (put_bits_count(&s->pb) & 7);
    avpriv_align_put_bits(&s->pb);
    for (i = 0; i < namelen - 2; i++)
        put_bits(&s->pb, 8, name[i]);
    put_bits(&s->pb, 12 - padbits, 0);
}

static int aac_encode_frame(AVCodecContext *avctx,
                            uint8_t *frame, int buf_size, void *data)
{
    AACEncContext *s = avctx->priv_data;
    int16_t *samples = s->samples, *samples2, *la;
    ChannelElement *cpe;
    int i, ch, w, g, chans, tag, start_ch;
    int chan_el_counter[4];
    FFPsyWindowInfo windows[AAC_MAX_CHANNELS];

    if (s->last_frame)
        return 0;
    if (data) {
        if (!s->psypp) {
            if (avctx->channels <= 2) {
                memcpy(s->samples + 1024 * avctx->channels, data,
                       1024 * avctx->channels * sizeof(s->samples[0]));
            } else {
                for (i = 0; i < 1024; i++)
                    for (ch = 0; ch < avctx->channels; ch++)
                        s->samples[(i + 1024) * avctx->channels + ch] =
                            ((int16_t*)data)[i * avctx->channels +
                                             channel_maps[avctx->channels-1][ch]];
            }
        } else {
            start_ch = 0;
            samples2 = s->samples + 1024 * avctx->channels;
            for (i = 0; i < s->chan_map[0]; i++) {
                tag = s->chan_map[i+1];
                chans = tag == TYPE_CPE ? 2 : 1;
                ff_psy_preprocess(s->psypp,
                                  (uint16_t*)data + channel_maps[avctx->channels-1][start_ch],
                                  samples2 + start_ch, start_ch, chans);
                start_ch += chans;
            }
        }
    }
    if (!avctx->frame_number) {
        memcpy(s->samples, s->samples + 1024 * avctx->channels,
               1024 * avctx->channels * sizeof(s->samples[0]));
        return 0;
    }

    start_ch = 0;
    for (i = 0; i < s->chan_map[0]; i++) {
        FFPsyWindowInfo* wi = windows + start_ch;
        tag      = s->chan_map[i+1];
        chans    = tag == TYPE_CPE ? 2 : 1;
        cpe      = &s->cpe[i];
        for (ch = 0; ch < chans; ch++) {
            IndividualChannelStream *ics = &cpe->ch[ch].ics;
            int cur_channel = start_ch + ch;
            samples2 = samples + cur_channel;
            la       = samples2 + (448+64) * avctx->channels;
            if (!data)
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

            apply_window_and_mdct(avctx, s, &cpe->ch[ch], samples2);
        }
        start_ch += chans;
    }
    do {
        int frame_bits;
        init_put_bits(&s->pb, frame, buf_size*8);
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
        if (frame_bits <= 6144 * avctx->channels - 3) {
            s->psy.bitres.bits = frame_bits / avctx->channels;
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

    if (!data)
        s->last_frame = 1;
    memcpy(s->samples, s->samples + 1024 * avctx->channels,
           1024 * avctx->channels * sizeof(s->samples[0]));
    return put_bits_count(&s->pb)>>3;
}

static av_cold int aac_encode_end(AVCodecContext *avctx)
{
    AACEncContext *s = avctx->priv_data;

    ff_mdct_end(&s->mdct1024);
    ff_mdct_end(&s->mdct128);
    ff_psy_end(&s->psy);
    ff_psy_preprocess_end(s->psypp);
    av_freep(&s->samples);
    av_freep(&s->cpe);
    return 0;
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
    .encode         = aac_encode_frame,
    .close          = aac_encode_end,
    .capabilities = CODEC_CAP_SMALL_LAST_FRAME | CODEC_CAP_DELAY | CODEC_CAP_EXPERIMENTAL,
    .sample_fmts = (const enum AVSampleFormat[]){AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("Advanced Audio Coding"),
    .priv_class = &aacenc_class,
};
