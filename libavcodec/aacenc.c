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
#include "libavutil/mem.h"
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

/**
 * List of PCE (Program Configuration Element) for the channel layouts listed
 * in channel_layout.h
 *
 * For those wishing in the future to add other layouts:
 *
 * - num_ele: number of elements in each group of front, side, back, lfe channels
 *            (an element is of type SCE (single channel), CPE (channel pair) for
 *            the first 3 groups; and is LFE for LFE group).
 *
 * - pairing: 0 for an SCE element or 1 for a CPE; does not apply to LFE group
 *
 * - index: there are three independent indices for SCE, CPE and LFE;
 *     they are incremented irrespective of the group to which the element belongs;
 *     they are not reset when going from one group to another
 *
 *     Example: for 7.0 channel layout,
 *        .pairing = { { 1, 0 }, { 1 }, { 1 }, }, (3 CPE and 1 SCE in front group)
 *        .index = { { 0, 0 }, { 1 }, { 2 }, },
 *               (index is 0 for the single SCE but goes from 0 to 2 for the CPEs)
 *
 *     The index order impacts the channel ordering. But is otherwise arbitrary
 *     (the sequence could have been 2, 0, 1 instead of 0, 1, 2).
 *
 *     Spec allows for discontinuous indices, e.g. if one has a total of two SCE,
 *     SCE.0 SCE.15 is OK per spec; BUT it won't be decoded by our AAC decoder
 *     which at this time requires that indices fully cover some range starting
 *     from 0 (SCE.1 SCE.0 is OK but not SCE.0 SCE.15).
 *
 * - config_map: total number of elements and their types. Beware, the way the
 *               types are ordered impacts the final channel ordering.
 *
 * - reorder_map: reorders the channels.
 *
 */
static const AACPCEInfo aac_pce_configs[] = {
    {
        .layout = AV_CHANNEL_LAYOUT_MONO,
        .num_ele = { 1, 0, 0, 0 },
        .pairing = { { 0 }, },
        .index = { { 0 }, },
        .config_map = { 1, TYPE_SCE, },
        .reorder_map = { 0 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_STEREO,
        .num_ele = { 1, 0, 0, 0 },
        .pairing = { { 1 }, },
        .index = { { 0 }, },
        .config_map = { 1, TYPE_CPE, },
        .reorder_map = { 0, 1 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_2POINT1,
        .num_ele = { 1, 0, 0, 1 },
        .pairing = { { 1 }, },
        .index = { { 0 },{ 0 },{ 0 },{ 0 } },
        .config_map = { 2, TYPE_CPE, TYPE_LFE },
        .reorder_map = { 0, 1, 2 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_2_1,
        .num_ele = { 1, 0, 1, 0 },
        .pairing = { { 1 },{ 0 },{ 0 } },
        .index = { { 0 },{ 0 },{ 0 }, },
        .config_map = { 2, TYPE_CPE, TYPE_SCE },
        .reorder_map = { 0, 1, 2 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_SURROUND,
        .num_ele = { 2, 0, 0, 0 },
        .pairing = { { 0, 1 }, },
        .index = { { 0, 0 }, },
        .config_map = { 2, TYPE_SCE, TYPE_CPE },
        .reorder_map = { 2, 0, 1 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_3POINT1,
        .num_ele = { 2, 0, 0, 1 },
        .pairing = { { 0, 1 }, },
        .index = { { 0, 0 }, { 0 }, { 0 }, { 0 }, },
        .config_map = { 3, TYPE_SCE, TYPE_CPE, TYPE_LFE },
        .reorder_map = { 2, 0, 1, 3 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_4POINT0,
        .num_ele = { 2, 0, 1, 0 },
        .pairing = { { 0, 1 }, { 0 }, { 0 }, },
        .index = { { 0, 0 }, { 0 }, { 1 } },
        .config_map = { 3, TYPE_SCE, TYPE_CPE, TYPE_SCE },
        .reorder_map = { 2, 0, 1, 3 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_4POINT1,
        .num_ele = { 2, 0, 1, 1 },
        .pairing = { { 0, 1 }, { 0 }, { 0 }, },
        .index = { { 0, 0 }, { 0 }, { 1 }, { 0 } },
        .config_map = { 4, TYPE_SCE, TYPE_CPE, TYPE_SCE, TYPE_LFE },
        .reorder_map = { 2, 0, 1, 4, 3 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_2_2,
        .num_ele = { 1, 0, 1, 0 },
        .pairing = { { 1 }, { 0 }, { 1 }, },
        .index = { { 0 }, { 0 }, { 1 } },
        .config_map = { 2, TYPE_CPE, TYPE_CPE },
        .reorder_map = { 0, 1, 2, 3 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_QUAD,
        .num_ele = { 1, 0, 1, 0 },
        .pairing = { { 1 }, { 0 }, { 1 }, },
        .index = { { 0 }, { 0 }, { 1 } },
        .config_map = { 2, TYPE_CPE, TYPE_CPE },
        .reorder_map = { 0, 1, 2, 3 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_5POINT0,
        .num_ele = { 2, 0, 1, 0 },
        .pairing = { { 0, 1 }, { 0 }, { 1 } },
        .index = { { 0, 0 }, { 0 }, { 1 } },
        .config_map = { 3, TYPE_SCE, TYPE_CPE, TYPE_CPE },
        .reorder_map = { 2, 0, 1, 3, 4 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_5POINT1,
        .num_ele = { 2, 0, 1, 1 },
        .pairing = { { 0, 1 }, { 0 }, { 1 }, },
        .index = { { 0, 0 }, { 0 }, { 1 }, { 0 } },
        .config_map = { 4, TYPE_SCE, TYPE_CPE, TYPE_CPE, TYPE_LFE },
        .reorder_map = { 2, 0, 1, 4, 5, 3 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_5POINT0_BACK,
        .num_ele = { 2, 0, 1, 0 },
        .pairing = { { 0, 1 }, { 0 }, { 1 } },
        .index = { { 0, 0 }, { 0 }, { 1 } },
        .config_map = { 3, TYPE_SCE, TYPE_CPE, TYPE_CPE },
        .reorder_map = { 2, 0, 1, 3, 4 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_5POINT1_BACK,
        .num_ele = { 2, 0, 1, 1 },
        .pairing = { { 0, 1 }, { 0 }, { 1 }, },
        .index = { { 0, 0 }, { 0 }, { 1 }, { 0 } },
        .config_map = { 4, TYPE_SCE, TYPE_CPE, TYPE_CPE, TYPE_LFE },
        .reorder_map = { 2, 0, 1, 4, 5, 3 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_6POINT0,
        .num_ele = { 2, 0, 2, 0 },
        .pairing = { { 0, 1 }, { 0 }, { 1, 0 } },
        .index = { { 0, 0 }, { 0 }, { 1, 1 } },
        .config_map = { 4, TYPE_SCE, TYPE_CPE, TYPE_CPE, TYPE_SCE },
        .reorder_map = { 2, 0, 1, 4, 5, 3 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_6POINT0_FRONT,
        .num_ele = { 2, 0, 1, 0 },
        .pairing = { { 1, 1 }, { 0 }, { 1 } },
        .index = { { 0, 1 }, { 0 }, { 2 }, },
        .config_map = { 3, TYPE_CPE, TYPE_CPE, TYPE_CPE, },
        .reorder_map = { 2, 3, 0, 1, 4, 5 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_HEXAGONAL,
        .num_ele = { 2, 0, 2, 0 },
        .pairing = { { 0, 1 }, { 0 }, { 1, 0 } },
        .index = { { 0, 0 }, { 0 }, { 1, 1 } },
        .config_map = { 4, TYPE_SCE, TYPE_CPE, TYPE_CPE, TYPE_SCE },
        .reorder_map = { 2, 0, 1, 3, 4, 5 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_6POINT1,
        .num_ele = { 2, 0, 2, 1 },
        .pairing = { { 0, 1 }, { 0 }, { 1, 0 }, },
        .index = { { 0, 0 }, { 0 }, { 1, 1 }, { 0 } },
        .config_map = { 5, TYPE_SCE, TYPE_CPE, TYPE_CPE, TYPE_SCE, TYPE_LFE },
        .reorder_map = { 2, 0, 1, 5, 6, 4, 3 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_6POINT1_BACK,
        .num_ele = { 2, 0, 2, 1 },
        .pairing = { { 0, 1 },{ 0 },{ 1, 0 }, },
        .index = { { 0, 0 },{ 0 },{ 1, 1 },{ 0 } },
        .config_map = { 5, TYPE_SCE, TYPE_CPE, TYPE_CPE, TYPE_SCE, TYPE_LFE },
        .reorder_map = { 2, 0, 1, 4, 5, 6, 3 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_6POINT1_FRONT,
        .num_ele = { 2, 0, 1, 1 },
        .pairing = { { 1, 1 }, { 0 }, { 1 }, },
        .index = { { 0, 1 }, { 0 }, { 2 }, { 0 }, },
        .config_map = { 4, TYPE_CPE, TYPE_CPE, TYPE_CPE, TYPE_LFE, },
        .reorder_map = { 3, 4, 0, 1, 5, 6, 2 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_7POINT0,
        .num_ele = { 2, 0, 2, 0 },
        .pairing = { { 0, 1 }, { 0 }, { 1, 1 }, },
        .index = { { 0, 0 }, { 0 }, { 2, 1 }, },
        .config_map = { 4, TYPE_SCE, TYPE_CPE, TYPE_CPE, TYPE_CPE },
        .reorder_map = { 2, 0, 1, 3, 4, 5, 6 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_7POINT0_FRONT,
        .num_ele = { 3, 0, 1, 0 },
        .pairing = { { 0, 1, 1 }, { 0 }, { 1 }, },
        .index = { { 0, 0, 1 }, { 0 }, { 2 }, },
        .config_map = { 4, TYPE_SCE, TYPE_CPE, TYPE_CPE, TYPE_CPE },
        .reorder_map = { 2, 3, 4, 0, 1, 5, 6 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_7POINT1,
        .num_ele = { 2, 0, 2, 1 },
        .pairing = { { 0, 1 }, { 0 }, { 1, 1 }, },
        .index = { { 0, 0 }, { 0 }, { 2, 1 }, { 0 } },
        .config_map = { 5, TYPE_SCE, TYPE_CPE, TYPE_CPE, TYPE_CPE, TYPE_LFE },
        .reorder_map = { 2, 0, 1, 4, 5, 6, 7, 3 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_7POINT1_WIDE,
        .num_ele = { 3, 0, 1, 1 },
        .pairing = { { 0, 1, 1 }, { 0 }, { 1 }, },
        .index = { { 0, 0, 1 }, { 0 }, { 2 }, { 0 }, },
        .config_map = { 5, TYPE_SCE, TYPE_CPE, TYPE_CPE, TYPE_CPE, TYPE_LFE },
        .reorder_map = { 2, 4, 5, 0, 1, 6, 7, 3 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_7POINT1_WIDE_BACK,
        .num_ele = { 3, 0, 1, 1 },
        .pairing = { { 0, 1, 1 }, { 0 }, { 1 } },
        .index = { { 0, 0, 1 }, { 0 }, { 2 }, { 0 } },
        .config_map = { 5, TYPE_SCE, TYPE_CPE, TYPE_CPE, TYPE_CPE, TYPE_LFE },
        .reorder_map = { 2, 6, 7, 0, 1, 4, 5, 3 },
    },
    {
        .layout = AV_CHANNEL_LAYOUT_OCTAGONAL,
        .num_ele = { 2, 0, 3, 0 },
        .pairing = { { 0, 1 }, { 0 }, { 1, 1, 0 }, },
        .index = { { 0, 0 }, { 0 }, { 1, 2, 1 }, },
        .config_map = { 5, TYPE_SCE, TYPE_CPE, TYPE_CPE, TYPE_CPE, TYPE_SCE },
        .reorder_map = { 2, 0, 1, 6, 7, 3, 4, 5 },
    },
};

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
static int put_audio_specific_config(AVCodecContext *avctx, int chcfg)
{
    PutBitContext pb;
    AACEncContext *s = avctx->priv_data;
    const int max_size = 32;

    avctx->extradata = av_mallocz(max_size);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);

    init_put_bits(&pb, avctx->extradata, max_size);
    put_bits(&pb, 5, s->profile+1); //profile
    put_bits(&pb, 4, s->samplerate_index); //sample rate index
    put_bits(&pb, 4, chcfg);
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
    float *output = sce->ret_buf;

    apply_window[sce->ics.window_sequence[0]](s->fdsp, sce, audio);

    if (sce->ics.window_sequence[0] != EIGHT_SHORT_SEQUENCE)
        s->mdct1024_fn(s->mdct1024, sce->coeffs, output, sizeof(float));
    else
        for (i = 0; i < 1024; i += 128)
            s->mdct128_fn(s->mdct128, &sce->coeffs[i], output + i*2, sizeof(float));
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
        put_bits(&s->pb, 1, 0); /* No predictor present */
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
            for (cmaxsfb = ics->num_swb; cmaxsfb > 0 && cpe->ch[ch].zeroes[w*16+cmaxsfb-1]; cmaxsfb--)
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

/* Intensity stereo is only allowed when its irreducible image error */
#define NMR_IS_IMG_GATE 0.5f

/* Frequency in Hz for the lower limit of intensity stereo */
#define NMR_IS_LOW_LIMIT 6100

/* Rate ceiling (bits/sample/channel) above which intensity is skipped, ~145kbps */
#define NMR_IS_MAXBPS 1.52f

/* The rate ceiling is lifted on hard-to-code frames. The signal is the bit
 * reservoir going into deficit: a negative fill means the trellis is spending
 * more than the nominal rate to hold quality (operating lambda has climbed). */
#define NMR_IS_FILLGAIN 0.27f
#define NMR_IS_FILLMAX  0.40f

/* M/S thresholds: a band is recoded as mid+side when the side is negligible */
#define NMR_MS_EQUIV 0.01f
#define NMR_MS_MASK  0.0f

/* PNS-stereo decorrelation gate: a band may be noise-substituted in a CPE only if its
 * side energy is at least this fraction of its mid energy, i.e. the image is genuinely
 * wide (channels decorrelated). PNS renders uncorrelated noise per channel, so it only
 * preserves the image on already-wide bands; a much stricter bar than I/S (which can
 * collapse correlated bands). Lower = more PNS / more imaging risk. */
#define NMR_PNS_STEREO_DECORR 0.6f

/* Recode one band's window group as mid+side in place, updating the psy band
 * energies/thresholds to the M/S spectra. The threshold is halved as a coarse guard
 * against L/R unmasking of the independently-quantized M/S noise (M/S is a lossless
 * rotation but lossy coding). Used for the M/S decision and the intensity fallback. */
static void nmr_apply_ms_band(AACEncContext *s, ChannelElement *cpe,
                              int w, int g, int start, int len, int gl)
{
    SingleChannelElement *sce0 = &cpe->ch[0];
    SingleChannelElement *sce1 = &cpe->ch[1];
    cpe->ms_mask[w*16+g] = 1;
    for (int w2 = 0; w2 < gl; w2++) {
        FFPsyBand *b0 = &s->psy.ch[s->cur_channel+0].psy_bands[(w+w2)*16+g];
        FFPsyBand *b1 = &s->psy.ch[s->cur_channel+1].psy_bands[(w+w2)*16+g];
        float *L = sce0->coeffs + start + (w+w2)*128;
        float *R = sce1->coeffs + start + (w+w2)*128;
        float em = 0.0f, es = 0.0f;
        for (int i = 0; i < len; i++) {
            float m = (L[i] + R[i]) * 0.5f;
            R[i] = m - R[i]; L[i] = m;
            em += L[i]*L[i]; es += R[i]*R[i];
        }
        b0->threshold = b1->threshold = FFMIN(b0->threshold, b1->threshold) * 0.5f;
        b0->energy = em; b1->energy = es;
    }
}

/* Intensity-stereo perceptual test for one band's window group: collapse the pair
 * to a single carrier (L + p*R)*scale that the decoder rescales per channel, and
 * check that the irreducible image error, which no bit budget can reduce, is
 * masked in both channels. On success returns 1 and fills the carrier scale, the
 * decoder's R/carrier ratio sr_, and the phase p. The caller restricts this to HF
 * bands with energy in both channels. */
static int nmr_is_image_masked(AACEncContext *s, ChannelElement *cpe,
                               int w, int g, int start, int len, int gl,
                               float ener0, float ener1, float dot,
                               float minthr0, float minthr1,
                               float *scale_out, float *sr_out, int *p_out)
{
    int p = dot >= 0.0f ? 1 : -1;
    float ener01 = ener0 + ener1 + 2*p*dot;     /* energy of L + p*R */
    if (ener01 <= FLT_MIN)
        return 0;
    float scale = sqrtf(ener0 / ener01);        /* carrier = (L + p*R)*scale */
    float sr_   = sqrtf(ener1 / ener0);         /* decoder: R = p*sr_*carrier */
    float img0 = 0.0f, img1 = 0.0f;
    for (int w2 = 0; w2 < gl; w2++) {
        const float *L = cpe->ch[0].coeffs + start + (w+w2)*128;
        const float *R = cpe->ch[1].coeffs + start + (w+w2)*128;
        for (int i = 0; i < len; i++) {
            float c  = (L[i] + p*R[i]) * scale;
            float dl = L[i] - c, dr = R[i] - p*sr_*c;
            img0 += dl*dl; img1 += dr*dr;
        }
    }
    if (img0 >= NMR_IS_IMG_GATE * minthr0 * gl ||
        img1 >= NMR_IS_IMG_GATE * minthr1 * gl)
        return 0;
    *scale_out = scale; *sr_out = sr_; *p_out = p;
    return 1;
}

/* Recode one band's window group as intensity stereo in place: replace L with the
 * carrier, zero R, signal the phase via the side channel's band type, and fold the
 * pair's masking into the surviving (carrier) channel. */
static void nmr_apply_is_band(AACEncContext *s, ChannelElement *cpe,
                              int w, int g, int start, int len, int gl,
                              float scale, float sr_, int p,
                              float ener0, float ener1)
{
    cpe->is_mask[w*16+g] = 1;
    cpe->ch[0].is_ener[w*16+g] = scale;
    cpe->ch[1].is_ener[w*16+g] = ener0 / ener1;
    cpe->ch[1].band_type[w*16+g] = p > 0 ? INTENSITY_BT : INTENSITY_BT2;
    for (int w2 = 0; w2 < gl; w2++) {
        FFPsyBand *b0 = &s->psy.ch[s->cur_channel+0].psy_bands[(w+w2)*16+g];
        FFPsyBand *b1 = &s->psy.ch[s->cur_channel+1].psy_bands[(w+w2)*16+g];
        float *L = cpe->ch[0].coeffs + start + (w+w2)*128;
        float *R = cpe->ch[1].coeffs + start + (w+w2)*128;
        float ec = 0.0f;
        for (int i = 0; i < len; i++) {
            L[i] = (L[i] + p*R[i]) * scale;
            R[i] = 0.0f;
            ec += L[i]*L[i];
        }
        b0->threshold = FFMIN(b0->threshold, b1->threshold / FFMAX(sr_*sr_, 1e-9f));
        b0->energy = ec; b1->energy = 0.0f;
    }
}

/*
 * Per-band stereo-mode decision (L/R vs M/S vs intensity) for the NMR coder,
 * made before quantization from the psychoacoustic model alone, so the
 * quantizer search allocates natively on the spectra that are actually coded.
 */
static void nmr_decide_stereo(AACEncContext *s, ChannelElement *cpe)
{
    SingleChannelElement *sce0 = &cpe->ch[0];
    SingleChannelElement *sce1 = &cpe->ch[1];
    IndividualChannelStream *ics = &sce0->ics;
    const AVCodecContext *avctx = s->psy.avctx;
    const float freq_mult = avctx->sample_rate / (1024.0f / ics->num_windows) / 2.0f;
    const float bps = avctx->bit_rate > 0 ?
                      (float)avctx->bit_rate / avctx->sample_rate / avctx->ch_layout.nb_channels : 0.0f;
    int is_count = 0;

    /* Stereo decision, with no bitrate dependence. Start from full L/R and depart from
     * it only where the change is inaudible. M/S and I/S differ in what they trade:
     *   M/S  recodes the pair as mid+side -- an invertible rotation, but the M and S
     *        are quantized independently, so it is lossy coding whose noise un-mixes
     *        back to L/R. Used where it barely changes the result (the side is
     *        negligible vs the mid, so it is ~equivalent to L/R at the same rate) --
     *        OR where the doubled side energy is masked.
     *   I/S  drops the side phase and keeps its energy, where the residual image error
     *        is masked. Used for the decorrelated HF that M/S cannot help.
     * Both tests are content/perceptual and frame-stable, so the image holds. */

    /* I/S rate gate: eligible at/below ~128 kbps, with the ceiling lifted on hard
     * frames (bit reservoir in deficit) so a starved high-rate passage can still
     * call on intensity. Where an I/S candidate is found but IS is not eligible, fall
     * back to M/S: not free, but ~equivalent to L/R there and it lets the energy
     * compact into the mid. */
    const float rate_frame = avctx->bit_rate * 1024.0f / FFMAX(avctx->sample_rate, 1);
    const float deficit = (s->nmr && rate_frame > 0.0f)
                          ? FFMAX(0.0f, -(float)s->nmr->rc_fill / rate_frame) : 0.0f;
    const float is_bonus = FFMIN(NMR_IS_FILLMAX, NMR_IS_FILLGAIN * deficit);
    const int allow_is = s->options.intensity_stereo && bps < NMR_IS_MAXBPS + is_bonus;

    for (int w = 0; w < ics->num_windows; w += ics->group_len[w]) {
        int start = 0;
        for (int g = 0; g < ics->num_swb; start += ics->swb_sizes[g++]) {
            int len = ics->swb_sizes[g], gl = ics->group_len[w];
            float ener0 = 0.0f, ener1 = 0.0f, dot = 0.0f, es_tot = 0.0f, em_tot = 0.0f;
            float minthr0 = FLT_MAX, minthr1 = FLT_MAX;

            cpe->is_mask[w*16+g] = 0;
            cpe->ms_mask[w*16+g] = 0;

            for (int w2 = 0; w2 < gl; w2++) {
                FFPsyBand *b0 = &s->psy.ch[s->cur_channel+0].psy_bands[(w+w2)*16+g];
                FFPsyBand *b1 = &s->psy.ch[s->cur_channel+1].psy_bands[(w+w2)*16+g];
                const float *L = sce0->coeffs + start + (w+w2)*128;
                const float *R = sce1->coeffs + start + (w+w2)*128;
                float el = 0.0f, er = 0.0f, em = 0.0f, es = 0.0f, d = 0.0f;
                for (int i = 0; i < len; i++) {
                    float m  = (L[i] + R[i]) * 0.5f;
                    float sv = m - R[i];
                    el += L[i]*L[i]; er += R[i]*R[i];
                    em += m*m; es += sv*sv; d += L[i]*R[i];
                }
                ener0 += el; ener1 += er; dot += d; es_tot += es; em_tot += em;
                minthr0 = FFMIN(minthr0, b0->threshold);
                minthr1 = FFMIN(minthr1, b1->threshold);
            }
            float thr_g = FFMIN(minthr0, minthr1) * gl;   /* group masking budget */

            /* PNS-stereo reservation. Reserve a band for noise substitution only if it
             * is noise-like in both channels (intersected can_pns) and clearly
             * decorrelated (wide image). */
            if (cpe->ch[0].can_pns[w*16+g] && cpe->ch[1].can_pns[w*16+g] &&
                es_tot > NMR_PNS_STEREO_DECORR * em_tot)
                continue;
            cpe->ch[0].can_pns[w*16+g] = cpe->ch[1].can_pns[w*16+g] = 0;

            int ms_ok = s->options.mid_side &&
                        (s->options.mid_side == 1 ||
                         es_tot < NMR_MS_EQUIV * em_tot ||
                         es_tot < NMR_MS_MASK  * thr_g);
            float scale, sr_; int p;
            int is_ok = !ms_ok &&
                        start * freq_mult > NMR_IS_LOW_LIMIT &&
                        ener0 > FLT_MIN && ener1 > FLT_MIN &&
                        nmr_is_image_masked(s, cpe, w, g, start, len, gl,
                                            ener0, ener1, dot, minthr0, minthr1,
                                            &scale, &sr_, &p);

            if (ms_ok) {
                nmr_apply_ms_band(s, cpe, w, g, start, len, gl);
            } else if (is_ok && allow_is) {
                nmr_apply_is_band(s, cpe, w, g, start, len, gl,
                                  scale, sr_, p, ener0, ener1);
                is_count++;
            } else if (is_ok && s->options.mid_side) {
                nmr_apply_ms_band(s, cpe, w, g, start, len, gl);
            }
            /* else: keep full L/R stereo */
        }
    }
    cpe->is_mode = !!is_count;
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
    if (!common_window)
        put_ics_info(s, &sce->ics);
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

    if (!avctx->frame_num)
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

        if ((avctx->frame_num & 0xFF)==1 && !(avctx->flags & AV_CODEC_FLAG_BITEXACT))
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

            const int use_tns = s->options.tns && s->coder->search_for_tns &&
                                s->coder->apply_tns_filt;

            /* The NMR coder rate-controls itself and never re-quantizes, so TNS must run
             * before the quantizer */
            const int tns_first = s->options.coder == AAC_CODER_NMR;
            if (tns_first && use_tns) {
                for (ch = 0; ch < chans; ch++) {
                    sce = &cpe->ch[ch];
                    s->cur_channel = start_ch + ch;
                    /* mono: mark_pns before TNS so the region cap sees PNS bands. Stereo
                     * PNS is marked in its own block (below) after the stereo decision. */
                    if (chans == 1 && s->options.pns && s->coder->mark_pns)
                        s->coder->mark_pns(s, avctx, sce);
                    s->coder->search_for_tns(s, sce);
                    s->coder->apply_tns_filt(s, sce);
                    if (sce->tns.present)
                        tns_mode = 1;
                }
            }

            /* NMR stereo PNS (imaging-safe). Mark each channel's noise-like bands on the
             * original L/R psy, then keep PNS only where BOTH channels are noise-like. */
            if (chans == 2 && cpe->common_window && tns_first &&
                s->options.pns && s->coder->mark_pns) {
                s->cur_channel = start_ch;     s->coder->mark_pns(s, avctx, &cpe->ch[0]);
                s->cur_channel = start_ch + 1; s->coder->mark_pns(s, avctx, &cpe->ch[1]);
                for (int b = 0; b < 128; b++)
                    if (!cpe->ch[0].can_pns[b] || !cpe->ch[1].can_pns[b])
                        cpe->ch[0].can_pns[b] = cpe->ch[1].can_pns[b] = 0;
            }

            /* The NMR coder decides I/S and M/S BEFORE quantization, from the psy model,
             * and the trellis then allocates natively on the coeffs actually coded. */
            if (chans == 2 && cpe->common_window && s->options.coder == AAC_CODER_NMR &&
                (s->options.mid_side || s->options.intensity_stereo)) {
                s->cur_channel = start_ch;
                nmr_decide_stereo(s, cpe);
            }
            for (ch = 0; ch < chans; ch++) {
                s->cur_channel = start_ch + ch;
                /* NMR PNS is mono-only */
                if (s->options.pns && s->coder->mark_pns && !tns_first)
                    s->coder->mark_pns(s, avctx, &cpe->ch[ch]);
                s->coder->search_for_quantizers(avctx, s, &cpe->ch[ch], s->lambda);
            }
            for (ch = 0; ch < chans; ch++) { /* TNS (non-NMR) and PNS */
                sce = &cpe->ch[ch];
                s->cur_channel = start_ch + ch;
                if (!tns_first && use_tns) {
                    s->coder->search_for_tns(s, sce);
                    s->coder->apply_tns_filt(s, sce);
                    if (sce->tns.present)
                        tns_mode = 1;
                }
                if (s->options.pns && s->coder->search_for_pns)
                    s->coder->search_for_pns(s, avctx, sce);
            }
            s->cur_channel = start_ch;
            if (s->options.intensity_stereo) { /* Intensity Stereo */
                if (s->options.coder != AAC_CODER_NMR) { /* NMR: decided pre-search */
                    if (s->coder->search_for_is)
                        s->coder->search_for_is(s, avctx, cpe);
                    apply_intensity_stereo(cpe);
                }
                if (cpe->is_mode) is_mode = 1;
            }
            if (s->options.mid_side && s->options.coder != AAC_CODER_NMR) { /* Mid/Side stereo */
                if (s->options.mid_side == -1 && s->coder->search_for_ms)
                    s->coder->search_for_ms(s, cpe);
                else if (cpe->common_window)
                    memset(cpe->ms_mask, 1, sizeof(cpe->ms_mask));
                apply_mid_side_stereo(cpe);
            }
            adjust_frame_information(cpe, chans);
            if (chans == 2) {
                put_bits(&s->pb, 1, cpe->common_window);
                if (cpe->common_window) {
                    put_ics_info(s, &cpe->ch[0].ics);
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

        frame_bits = put_bits_count(&s->pb);

        /* The NMR coder rate-controls itself (global-lambda reservoir servo):
         * per-frame bits intentionally float around the nominal rate, so skip
         * the lambda rate loop and only intervene on a hard overflow. */
        if (s->options.coder == AAC_CODER_NMR && avctx->bit_rate_tolerance != 0 &&
            frame_bits < 6144 * s->channels - 3)
            break;

        /* rate control stuff
         * allow between the nominal bitrate, and what psy's bit reservoir says to target
         * but drift towards the nominal bitrate always
         */
        rate_bits = avctx->bit_rate * 1024 / avctx->sample_rate;
        rate_bits = FFMIN(rate_bits, 6144 * s->channels - 3);
        too_many_bits = FFMAX(target_bits, rate_bits);
        too_many_bits = FFMIN(too_many_bits, 6144 * s->channels - 3);
        too_few_bits = FFMIN(FFMAX(rate_bits - rate_bits/4, target_bits), too_many_bits);

        /* When strict bit-rate control is demanded */
        if (avctx->bit_rate_tolerance == 0) {
            if (rate_bits < frame_bits) {
                float ratio = ((float)rate_bits) / frame_bits;
                s->lambda *= FFMIN(0.9f, ratio);
                continue;
            }
            /* reset lambda when solution is found */
            s->lambda = avctx->global_quality > 0 ? avctx->global_quality : 120;
            break;
        }

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

    /* tool-usage stats over the final per-band decisions of this frame */
    for (i = 0; i < s->chan_map[0]; i++) {
        int etag = s->chan_map[i + 1], echans = etag == TYPE_CPE ? 2 : 1;
        ChannelElement *ce = &s->cpe[i];
        IndividualChannelStream *ics = &ce->ch[0].ics;
        for (ch = 0; ch < echans; ch++) {   /* per-channel frame stats */
            int is_short = ce->ch[ch].ics.window_sequence[0] == EIGHT_SHORT_SEQUENCE;
            s->stat_chans++;
            if (is_short)
                s->stat_short++;
            if (ce->ch[ch].tns.present) {
                if (is_short) s->stat_tns_short++;
                else          s->stat_tns_long++;
            }
        }
        for (w = 0; w < ics->num_windows; w += ics->group_len[w]) {
            for (int g = 0; g < ics->num_swb; g++) {
                int idx = w*16 + g, coded = 0;
                for (ch = 0; ch < echans; ch++) {
                    SingleChannelElement *sce = &ce->ch[ch];
                    if (sce->zeroes[idx] && sce->band_type[idx] == 0)
                        continue;
                    s->stat_ch_bands++;
                    if (sce->band_type[idx] == NOISE_BT)
                        s->stat_pns++;
                    coded = 1;
                }
                if (etag == TYPE_CPE && coded) {
                    s->stat_cpe_bands++;
                    if (ce->ms_mask[idx]) s->stat_ms++;
                    if (ce->is_mask[idx]) s->stat_is++;
                }
            }
        }
    }

    put_bits(&s->pb, 3, TYPE_END);
    flush_put_bits(&s->pb);

    s->last_frame_pb_count = put_bits_count(&s->pb);

    /* NMR rate accounting: how many bits the frame really took beyond what the
     * trellis counted; feeds the next frame's budget correction */
    if (s->nmr) {
        int counted = 0;
        for (i = 0; i < s->channels; i++)
            counted += s->nmr->counted[i];
        if (counted > 0) {
            float side = (float)s->last_frame_pb_count - counted;
            if (s->nmr->side_inited) {
                s->nmr->side_ema += 0.125f * (side - s->nmr->side_ema);
            } else {
                s->nmr->side_ema    = side;
                s->nmr->side_inited = 1;
            }
        }
    }
    avpkt->size            = put_bytes_output(&s->pb);

    s->lambda_sum += (s->nmr && s->nmr->lam_rc > 0.0f) ? s->nmr->lam_rc : s->lambda;
    s->lambda_count++;

    ff_af_queue_remove(&s->afq, avctx->frame_size, &avpkt->pts,
                       &avpkt->duration);

    avpkt->flags |= AV_PKT_FLAG_KEY;

    *got_packet_ptr = 1;
    return 0;
}

static av_cold int aac_encode_end(AVCodecContext *avctx)
{
    AACEncContext *s = avctx->priv_data;

    av_log(avctx, AV_LOG_INFO,
           "Qavg: %.3f  Tr: %.1f%%  TNS(L): %.1f%%  TNS(S): %.1f%%  M/S: %.1f%%  I/S: %.1f%%  PNS: %.1f%%\n",
           s->lambda_count ? s->lambda_sum / s->lambda_count : NAN,
           s->stat_chans     ? 100.0 * s->stat_short      / s->stat_chans               : 0.0,
           s->stat_chans - s->stat_short ? 100.0 * s->stat_tns_long  / (s->stat_chans - s->stat_short) : 0.0,
           s->stat_short     ? 100.0 * s->stat_tns_short  / s->stat_short               : 0.0,
           s->stat_cpe_bands ? 100.0 * s->stat_ms         / s->stat_cpe_bands           : 0.0,
           s->stat_cpe_bands ? 100.0 * s->stat_is         / s->stat_cpe_bands           : 0.0,
           s->stat_ch_bands  ? 100.0 * s->stat_pns        / s->stat_ch_bands            : 0.0);

    av_tx_uninit(&s->mdct1024);
    av_tx_uninit(&s->mdct128);
    ff_psy_end(&s->psy);
    ff_lpc_end(&s->lpc);
    av_freep(&s->buffer.samples);
    av_freep(&s->cpe);
    av_freep(&s->fdsp);
    av_freep(&s->nmr);
    ff_af_queue_close(&s->afq);
    return 0;
}

static av_cold int dsp_init(AVCodecContext *avctx, AACEncContext *s)
{
    int ret = 0;
    float scale = 32768.0f;

    s->fdsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    if ((ret = av_tx_init(&s->mdct1024, &s->mdct1024_fn, AV_TX_FLOAT_MDCT, 0,
                          1024, &scale, 0)) < 0)
        return ret;
    if ((ret = av_tx_init(&s->mdct128, &s->mdct128_fn,   AV_TX_FLOAT_MDCT, 0,
                          128, &scale, 0)) < 0)
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

    if (s->options.coder == AAC_CODER_NMR) {
        s->nmr = av_mallocz(sizeof(*s->nmr));
        if (!s->nmr)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static av_cold int aac_encode_init(AVCodecContext *avctx)
{
    AACEncContext *s = avctx->priv_data;
    int i, ret = 0;
    int chcfg;
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
    for (chcfg = 1; chcfg < FF_ARRAY_ELEMS(aac_normal_chan_layouts); chcfg++) {
        if (!av_channel_layout_compare(&avctx->ch_layout, &aac_normal_chan_layouts[chcfg])) {
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
        chcfg = 0;
    } else {
        s->reorder_map = aac_chan_maps[chcfg - 1];
        s->chan_map = aac_chan_configs[chcfg - 1];
    }

    if (!avctx->bit_rate) {
        for (i = 1; i <= s->chan_map[0]; i++) {
            avctx->bit_rate += s->chan_map[i] == TYPE_CPE ? 128000 : /* Pair */
                               s->chan_map[i] == TYPE_LFE ? 16000  : /* LFE  */
                                                            69000  ; /* SCE  */
        }
    }

    /* Samplerate */
    for (int i = 0;; i++) {
        av_assert1(i < 13);
        if (avctx->sample_rate == ff_mpeg4audio_sample_rates[i]) {
            s->samplerate_index = i;
            break;
        }
    }

    /* Bitrate limiting */
    WARN_IF(1024.0 * avctx->bit_rate / avctx->sample_rate > 6144 * s->channels,
             "Too many bits %f > %d per frame requested, clamping to max\n",
             1024.0 * avctx->bit_rate / avctx->sample_rate,
             6144 * s->channels);
    avctx->bit_rate = (int64_t)FFMIN(6144 * s->channels / 1024.0 * avctx->sample_rate,
                                     avctx->bit_rate);

    /* Profile and option setting */
    avctx->profile = avctx->profile == AV_PROFILE_UNKNOWN ? AV_PROFILE_AAC_LOW :
                     avctx->profile;
    for (i = 0; i < FF_ARRAY_ELEMS(aacenc_profiles); i++)
        if (avctx->profile == aacenc_profiles[i])
            break;
    ERROR_IF(i == FF_ARRAY_ELEMS(aacenc_profiles), "Profile not supported!\n");
    if (avctx->profile == AV_PROFILE_MPEG2_AAC_LOW) {
        avctx->profile = AV_PROFILE_AAC_LOW;
        WARN_IF(s->options.pns,
                "PNS unavailable in the \"mpeg2_aac_low\" profile, turning off\n");
        s->options.pns = 0;
    }
    s->profile = avctx->profile;

    /* Coder limitations */
    s->coder = &ff_aac_coders[s->options.coder];

    /* M/S introduces horrible artifacts with multichannel files, this is temporary */
    if (s->channels > 3)
        s->options.mid_side = 0;

    /* Coding bandwidth, fixed at init time */
    if (avctx->cutoff > 0) {
        s->bandwidth = avctx->cutoff;
    } else {
        int frame_br = (avctx->flags & AV_CODEC_FLAG_QSCALE) ?
                       (avctx->bit_rate / 2.0f * (s->lambda / 120.f) * 1.5f) :
                       (avctx->bit_rate / avctx->ch_layout.nb_channels);

        /* For NMR, the rate to bandwidth conversion was tuned to maximize metrics
         * over a variable cutoff x bitrate combo */
        if (s->options.coder == AAC_CODER_NMR && frame_br >= 32000) {
            static const int rates[] = { 32000, 48000, 64000, 96000 };
            static const int bws[]   = { 14000, 15000, 16000, 18000 };
            int bw_i = 0;
            for (; bw_i < FF_ARRAY_ELEMS(rates) - 2 && frame_br > rates[bw_i + 1]; bw_i++);
            s->bandwidth = bws[bw_i] + (int)((int64_t)(bws[bw_i + 1] - bws[bw_i]) *
                                             (frame_br - rates[bw_i]) / (rates[bw_i + 1] - rates[bw_i]));
            s->bandwidth = FFMIN3(s->bandwidth, 22000, avctx->sample_rate / 2);
        } else {
            if (s->options.pns || s->options.intensity_stereo)
                frame_br *= 1.15f;
            s->bandwidth = FFMAX(3000, AAC_CUTOFF_FROM_BITRATE(frame_br, 1,
                                                               avctx->sample_rate));
        }

        s->bandwidth = FFMIN(FFMAX(s->bandwidth, 8000), avctx->sample_rate / 2);
    }

    // Initialize static tables
    ff_aac_float_common_init();

    if ((ret = dsp_init(avctx, s)) < 0)
        return ret;

    if ((ret = alloc_buffers(avctx, s)) < 0)
        return ret;

    if ((ret = put_audio_specific_config(avctx, chcfg)))
        return ret;

    sizes[0]   = ff_aac_swb_size_1024[s->samplerate_index];
    sizes[1]   = ff_aac_swb_size_128[s->samplerate_index];
    lengths[0] = ff_aac_num_swb_1024[s->samplerate_index];
    lengths[1] = ff_aac_num_swb_128[s->samplerate_index];
    for (i = 0; i < s->chan_map[0]; i++)
        grouping[i] = s->chan_map[i + 1] == TYPE_CPE;
    if ((ret = ff_psy_init(&s->psy, avctx, 2, sizes, lengths,
                           s->chan_map[0], grouping, s->bandwidth)) < 0)
        return ret;
    ff_lpc_init(&s->lpc, 2*avctx->frame_size, TNS_MAX_ORDER, FF_LPC_TYPE_LEVINSON);
    s->random_state = 0x1f2e3d4c;

    ff_aacenc_dsp_init(&s->aacdsp);

    ff_af_queue_init(avctx, &s->afq);

    return 0;
}

#define AACENC_FLAGS AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM
static const AVOption aacenc_options[] = {
    {"aac_coder", "Coding algorithm", offsetof(AACEncContext, options.coder), AV_OPT_TYPE_INT, {.i64 = AAC_CODER_NMR}, 0, AAC_CODER_NB-1, AACENC_FLAGS, .unit = "coder"},
        {"twoloop",  "Two loop searching method", 0, AV_OPT_TYPE_CONST, {.i64 = AAC_CODER_TWOLOOP}, INT_MIN, INT_MAX, AACENC_FLAGS, .unit = "coder"},
        {"fast",     "Fast search",               0, AV_OPT_TYPE_CONST, {.i64 = AAC_CODER_FAST},    INT_MIN, INT_MAX, AACENC_FLAGS, .unit = "coder"},
        {"nmr",      "Noise-to-mask ratio scalefactor trellis", 0, AV_OPT_TYPE_CONST, {.i64 = AAC_CODER_NMR}, INT_MIN, INT_MAX, AACENC_FLAGS, .unit = "coder"},
    {"aac_ms", "Force M/S stereo coding", offsetof(AACEncContext, options.mid_side), AV_OPT_TYPE_BOOL, {.i64 = -1}, -1, 1, AACENC_FLAGS},
    {"aac_is", "Intensity stereo coding", offsetof(AACEncContext, options.intensity_stereo), AV_OPT_TYPE_BOOL, {.i64 = 1}, -1, 1, AACENC_FLAGS},
    {"aac_pns", "Perceptual noise substitution", offsetof(AACEncContext, options.pns), AV_OPT_TYPE_BOOL, {.i64 = 1}, -1, 1, AACENC_FLAGS},
    {"aac_tns", "Temporal noise shaping", offsetof(AACEncContext, options.tns), AV_OPT_TYPE_BOOL, {.i64 = 1}, -1, 1, AACENC_FLAGS},
    {"aac_pce", "Forces the use of PCEs", offsetof(AACEncContext, options.pce), AV_OPT_TYPE_BOOL, {.i64 = 0}, -1, 1, AACENC_FLAGS},
    {"aac_nmr_speed", "NMR coder speed level: 0 = slowest/best, higher trades quality for speed", offsetof(AACEncContext, options.nmr_speed), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 4, AACENC_FLAGS},
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
    CODEC_LONG_NAME("AAC (Advanced Audio Coding)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_AAC,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                      AV_CODEC_CAP_SMALL_LAST_FRAME,
    .priv_data_size = sizeof(AACEncContext),
    .init           = aac_encode_init,
    FF_CODEC_ENCODE_CB(aac_encode_frame),
    .close          = aac_encode_end,
    .defaults       = aac_encode_defaults,
    CODEC_SAMPLERATES_ARRAY(ff_mpeg4audio_sample_rates),
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
    CODEC_SAMPLEFMTS(AV_SAMPLE_FMT_FLTP),
    .p.priv_class   = &aacenc_class,
};
