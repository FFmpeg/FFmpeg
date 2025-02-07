/*
 * Copyright (c) 2024 Lynne <dev@lynne.ee>
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

#include "aacdec_usac.h"
#include "aacdec_tab.h"
#include "aacdec_lpd.h"
#include "aacdec_ac.h"

#include "libavcodec/aacsbr.h"

#include "libavcodec/aactab.h"
#include "libavutil/mem.h"
#include "libavcodec/mpeg4audio.h"
#include "libavcodec/unary.h"

/* Number of scalefactor bands per complex prediction band, equal to 2. */
#define SFB_PER_PRED_BAND 2

static inline uint32_t get_escaped_value(GetBitContext *gb, int nb1, int nb2, int nb3)
{
    uint32_t val = get_bits(gb, nb1), val2;
    if (val < ((1 << nb1) - 1))
        return val;

    val += val2 = get_bits(gb, nb2);
    if (nb3 && (val2 == ((1 << nb2) - 1)))
        val += get_bits(gb, nb3);

    return val;
}

/* ISO/IEC 23003-3, Table 74 — bsOutputChannelPos */
static const enum AVChannel usac_ch_pos_to_av[64] = {
    [0] = AV_CHAN_FRONT_LEFT,
    [1] = AV_CHAN_FRONT_RIGHT,
    [2] = AV_CHAN_FRONT_CENTER,
    [3] = AV_CHAN_LOW_FREQUENCY,
    [4] = AV_CHAN_SIDE_LEFT, // +110 degrees, Ls|LS|kAudioChannelLabel_LeftSurround
    [5] = AV_CHAN_SIDE_RIGHT, // -110 degrees, Rs|RS|kAudioChannelLabel_RightSurround
    [6] = AV_CHAN_FRONT_LEFT_OF_CENTER,
    [7] = AV_CHAN_FRONT_RIGHT_OF_CENTER,
    [8] = AV_CHAN_BACK_LEFT, // +135 degrees, Lsr|BL|kAudioChannelLabel_RearSurroundLeft
    [9] = AV_CHAN_BACK_RIGHT, // -135 degrees, Rsr|BR|kAudioChannelLabel_RearSurroundRight
    [10] = AV_CHAN_BACK_CENTER,
    [11] = AV_CHAN_SURROUND_DIRECT_LEFT,
    [12] = AV_CHAN_SURROUND_DIRECT_RIGHT,
    [13] = AV_CHAN_SIDE_SURROUND_LEFT, // +90 degrees, Lss|SL|kAudioChannelLabel_LeftSideSurround
    [14] = AV_CHAN_SIDE_SURROUND_RIGHT, // -90 degrees, Rss|SR|kAudioChannelLabel_RightSideSurround
    [15] = AV_CHAN_WIDE_LEFT, // +60 degrees, Lw|FLw|kAudioChannelLabel_LeftWide
    [16] = AV_CHAN_WIDE_RIGHT, // -60 degrees, Rw|FRw|kAudioChannelLabel_RightWide
    [17] = AV_CHAN_TOP_FRONT_LEFT,
    [18] = AV_CHAN_TOP_FRONT_RIGHT,
    [19] = AV_CHAN_TOP_FRONT_CENTER,
    [20] = AV_CHAN_TOP_BACK_LEFT,
    [21] = AV_CHAN_TOP_BACK_RIGHT,
    [22] = AV_CHAN_TOP_BACK_CENTER,
    [23] = AV_CHAN_TOP_SIDE_LEFT,
    [24] = AV_CHAN_TOP_SIDE_RIGHT,
    [25] = AV_CHAN_TOP_CENTER,
    [26] = AV_CHAN_LOW_FREQUENCY_2,
    [27] = AV_CHAN_BOTTOM_FRONT_LEFT,
    [28] = AV_CHAN_BOTTOM_FRONT_RIGHT,
    [29] = AV_CHAN_BOTTOM_FRONT_CENTER,
    [30] = AV_CHAN_TOP_SURROUND_LEFT, ///< +110 degrees, Lvs, TpLS
    [31] = AV_CHAN_TOP_SURROUND_RIGHT, ///< -110 degrees, Rvs, TpRS
};

static int decode_loudness_info(AACDecContext *ac, AACUSACLoudnessInfo *info,
                                GetBitContext *gb)
{
    info->drc_set_id = get_bits(gb, 6);
    info->downmix_id = get_bits(gb, 7);

    if ((info->sample_peak.present = get_bits1(gb))) /* samplePeakLevelPresent */
        info->sample_peak.lvl = get_bits(gb, 12);

    if ((info->true_peak.present = get_bits1(gb))) { /* truePeakLevelPresent */
        info->true_peak.lvl = get_bits(gb, 12);
        info->true_peak.measurement = get_bits(gb, 4);
        info->true_peak.reliability = get_bits(gb, 2);
    }

    info->nb_measurements = get_bits(gb, 4);
    for (int i = 0; i < info->nb_measurements; i++) {
        info->measurements[i].method_def = get_bits(gb, 4);
        info->measurements[i].method_val = get_unary(gb, 0, 8);
        info->measurements[i].measurement = get_bits(gb, 4);
        info->measurements[i].reliability = get_bits(gb, 2);
    }

    return 0;
}

static int decode_loudness_set(AACDecContext *ac, AACUSACConfig *usac,
                               GetBitContext *gb)
{
    int ret;

    usac->loudness.nb_album = get_bits(gb, 6); /* loudnessInfoAlbumCount */
    usac->loudness.nb_info = get_bits(gb, 6); /* loudnessInfoCount */

    for (int i = 0; i < usac->loudness.nb_album; i++) {
        ret = decode_loudness_info(ac, &usac->loudness.album_info[i], gb);
        if (ret < 0)
            return ret;
    }

    for (int i = 0; i < usac->loudness.nb_info; i++) {
        ret = decode_loudness_info(ac, &usac->loudness.info[i], gb);
        if (ret < 0)
            return ret;
    }

    if (get_bits1(gb)) { /* loudnessInfoSetExtPresent */
        enum AACUSACLoudnessExt type;
        while ((type = get_bits(gb, 4)) != UNIDRCLOUDEXT_TERM) {
            uint8_t size_bits = get_bits(gb, 4) + 4;
            uint8_t bit_size = get_bits(gb, size_bits) + 1;
            switch (type) {
            case UNIDRCLOUDEXT_EQ:
                avpriv_report_missing_feature(ac->avctx, "loudnessInfoV1");
                return AVERROR_PATCHWELCOME;
            default:
                for (int i = 0; i < bit_size; i++)
                    skip_bits1(gb);
            }
        }
    }

    return 0;
}

static int decode_usac_sbr_data(AACDecContext *ac,
                                AACUsacElemConfig *e, GetBitContext *gb)
{
    uint8_t header_extra1;
    uint8_t header_extra2;

    e->sbr.harmonic_sbr = get_bits1(gb); /* harmonicSBR */
    e->sbr.bs_intertes = get_bits1(gb); /* bs_interTes */
    e->sbr.bs_pvc = get_bits1(gb); /* bs_pvc */
    if (e->sbr.harmonic_sbr || e->sbr.bs_intertes || e->sbr.bs_pvc) {
        avpriv_report_missing_feature(ac->avctx, "AAC USAC eSBR");
        return AVERROR_PATCHWELCOME;
    }

    e->sbr.dflt.start_freq = get_bits(gb, 4); /* dflt_start_freq */
    e->sbr.dflt.stop_freq = get_bits(gb, 4); /* dflt_stop_freq */

    header_extra1 = get_bits1(gb); /* dflt_header_extra1 */
    header_extra2 = get_bits1(gb); /* dflt_header_extra2 */

    e->sbr.dflt.freq_scale = 2;
    e->sbr.dflt.alter_scale = 1;
    e->sbr.dflt.noise_bands = 2;
    if (header_extra1) {
        e->sbr.dflt.freq_scale = get_bits(gb, 2); /* dflt_freq_scale */
        e->sbr.dflt.alter_scale = get_bits1(gb); /* dflt_alter_scale */
        e->sbr.dflt.noise_bands = get_bits(gb, 2); /* dflt_noise_bands */
    }

    e->sbr.dflt.limiter_bands = 2;
    e->sbr.dflt.limiter_gains = 2;
    e->sbr.dflt.interpol_freq = 1;
    e->sbr.dflt.smoothing_mode = 1;
    if (header_extra2) {
        e->sbr.dflt.limiter_bands = get_bits(gb, 2); /* dflt_limiter_bands */
        e->sbr.dflt.limiter_gains = get_bits(gb, 2); /* dflt_limiter_gains */
        e->sbr.dflt.interpol_freq = get_bits1(gb); /* dflt_interpol_freq */
        e->sbr.dflt.smoothing_mode = get_bits1(gb); /* dflt_smoothing_mode */
    }

    return 0;
}

static void decode_usac_element_core(AACUsacElemConfig *e,
                                     GetBitContext *gb,
                                     int sbr_ratio)
{
    e->tw_mdct = get_bits1(gb); /* tw_mdct */
    e->noise_fill = get_bits1(gb);
    e->sbr.ratio = sbr_ratio;
}

static int decode_usac_element_pair(AACDecContext *ac,
                                    AACUsacElemConfig *e, GetBitContext *gb)
{
    e->stereo_config_index = 0;
    if (e->sbr.ratio) {
        int ret = decode_usac_sbr_data(ac, e, gb);
        if (ret < 0)
            return ret;
        e->stereo_config_index = get_bits(gb, 2);
    }

    if (e->stereo_config_index) {
        e->mps.freq_res = get_bits(gb, 3); /* bsFreqRes */
        e->mps.fixed_gain = get_bits(gb, 3); /* bsFixedGainDMX */
        e->mps.temp_shape_config = get_bits(gb, 2); /* bsTempShapeConfig */
        e->mps.decorr_config = get_bits(gb, 2); /* bsDecorrConfig */
        e->mps.high_rate_mode = get_bits1(gb); /* bsHighRateMode */
        e->mps.phase_coding = get_bits1(gb); /* bsPhaseCoding */

        if (get_bits1(gb)) /* bsOttBandsPhasePresent */
            e->mps.otts_bands_phase = get_bits(gb, 5); /* bsOttBandsPhase */

        e->mps.residual_coding = e->stereo_config_index >= 2; /* bsResidualCoding */
        if (e->mps.residual_coding) {
            e->mps.residual_bands = get_bits(gb, 5); /* bsResidualBands */
            e->mps.pseudo_lr = get_bits1(gb); /* bsPseudoLr */
        }
        if (e->mps.temp_shape_config == 2)
            e->mps.env_quant_mode = get_bits1(gb); /* bsEnvQuantMode */
    }

    return 0;
}

static int decode_usac_extension(AACDecContext *ac, AACUsacElemConfig *e,
                                 GetBitContext *gb)
{
    int len = 0, ext_config_len;

    e->ext.type = get_escaped_value(gb, 4, 8, 16); /* usacExtElementType */
    ext_config_len = get_escaped_value(gb, 4, 8, 16); /* usacExtElementConfigLength */

    if (get_bits1(gb)) /* usacExtElementDefaultLengthPresent */
        len = get_escaped_value(gb, 8, 16, 0) + 1;

    e->ext.default_len = len;
    e->ext.payload_frag = get_bits1(gb); /* usacExtElementPayloadFrag */

    av_log(ac->avctx, AV_LOG_DEBUG, "Extension present: type %i, len %i\n",
           e->ext.type, ext_config_len);

    switch (e->ext.type) {
#if 0 /* Skip unsupported values */
    case ID_EXT_ELE_MPEGS:
        break;
    case ID_EXT_ELE_SAOC:
        break;
    case ID_EXT_ELE_UNI_DRC:
        break;
#endif
    case ID_EXT_ELE_FILL:
        break; /* This is what the spec does */
    case ID_EXT_ELE_AUDIOPREROLL:
        /* No configuration needed - fallthrough (len should be 0) */
    default:
        skip_bits(gb, 8*ext_config_len);
        e->ext.type = ID_EXT_ELE_FILL;
        break;
    };

    return 0;
}

int ff_aac_usac_reset_state(AACDecContext *ac, OutputConfiguration *oc)
{
    AACUSACConfig *usac = &oc->usac;
    int elem_id[3 /* SCE, CPE, LFE */] = { 0, 0, 0 };

    ChannelElement *che;
    enum RawDataBlockType type;
    int id, ch;

    /* Initialize state */
    for (int i = 0; i < usac->nb_elems; i++) {
        AACUsacElemConfig *e = &usac->elems[i];
        if (e->type == ID_USAC_EXT)
            continue;

        switch (e->type) {
        case ID_USAC_SCE:
            ch = 1;
            type = TYPE_SCE;
            id = elem_id[0]++;
            break;
        case ID_USAC_CPE:
            ch = 2;
            type = TYPE_CPE;
            id = elem_id[1]++;
            break;
        case ID_USAC_LFE:
            ch = 1;
            type = TYPE_LFE;
            id = elem_id[2]++;
            break;
        }

        che = ff_aac_get_che(ac, type, id);
        if (che) {
            AACUsacStereo *us = &che->us;
            memset(us, 0, sizeof(*us));

            if (e->sbr.ratio)
                ff_aac_sbr_config_usac(ac, che, e);

            for (int j = 0; j < ch; j++) {
                SingleChannelElement *sce = &che->ch[ch];
                AACUsacElemData *ue = &sce->ue;

                memset(ue, 0, sizeof(*ue));

                if (!ch)
                    ue->noise.seed = 0x3039;
                else
                    che->ch[1].ue.noise.seed = 0x10932;
            }
        }
    }

    return 0;
}

/* UsacConfig */
int ff_aac_usac_config_decode(AACDecContext *ac, AVCodecContext *avctx,
                              GetBitContext *gb, OutputConfiguration *oc,
                              int channel_config)
{
    int ret;
    uint8_t freq_idx;
    uint8_t channel_config_idx;
    int nb_channels = 0;
    int ratio_mult, ratio_dec;
    int samplerate;
    int sbr_ratio;
    MPEG4AudioConfig *m4ac = &oc->m4ac;
    AACUSACConfig *usac = &oc->usac;
    int elem_id[3 /* SCE, CPE, LFE */];

    int map_pos_set = 0;
    uint8_t layout_map[MAX_ELEM_ID*4][3] = { 0 };

    if (!ac)
        return AVERROR_PATCHWELCOME;

    memset(usac, 0, sizeof(*usac));

    freq_idx = get_bits(gb, 5); /* usacSamplingFrequencyIndex */
    if (freq_idx == 0x1f) {
        samplerate = get_bits(gb, 24); /* usacSamplingFrequency */
    } else {
        samplerate = ff_aac_usac_samplerate[freq_idx];
        if (samplerate < 0)
            return AVERROR(EINVAL);
    }

    usac->core_sbr_frame_len_idx = get_bits(gb, 3); /* coreSbrFrameLengthIndex */
    m4ac->frame_length_short = usac->core_sbr_frame_len_idx == 0 ||
                               usac->core_sbr_frame_len_idx == 2;

    usac->core_frame_len = (usac->core_sbr_frame_len_idx == 0 ||
                            usac->core_sbr_frame_len_idx == 2) ? 768 : 1024;

    sbr_ratio = usac->core_sbr_frame_len_idx == 2 ? 2 :
                usac->core_sbr_frame_len_idx == 3 ? 3 :
                usac->core_sbr_frame_len_idx == 4 ? 1 :
                0;

    if (sbr_ratio == 2) {
        ratio_mult = 8;
        ratio_dec = 3;
    } else if (sbr_ratio == 3) {
        ratio_mult = 2;
        ratio_dec = 1;
    } else if (sbr_ratio == 4) {
        ratio_mult = 4;
        ratio_dec = 1;
    } else {
        ratio_mult = 1;
        ratio_dec = 1;
    }

    avctx->sample_rate = samplerate;
    m4ac->ext_sample_rate = samplerate;
    m4ac->sample_rate = (samplerate * ratio_dec) / ratio_mult;

    m4ac->sampling_index = ff_aac_sample_rate_idx(m4ac->sample_rate);
    m4ac->sbr = sbr_ratio > 0;

    channel_config_idx = get_bits(gb, 5); /* channelConfigurationIndex */
    if (!channel_config_idx) {
        /* UsacChannelConfig() */
        nb_channels = get_escaped_value(gb, 5, 8, 16); /* numOutChannels */
        if (nb_channels > 64)
            return AVERROR(EINVAL);

        av_channel_layout_uninit(&ac->oc[1].ch_layout);

        ret = av_channel_layout_custom_init(&ac->oc[1].ch_layout, nb_channels);
        if (ret < 0)
            return ret;

        for (int i = 0; i < nb_channels; i++) {
            AVChannelCustom *cm = &ac->oc[1].ch_layout.u.map[i];
            cm->id = usac_ch_pos_to_av[get_bits(gb, 5)]; /* bsOutputChannelPos */
        }

        ret = av_channel_layout_retype(&ac->oc[1].ch_layout,
                                       AV_CHANNEL_ORDER_NATIVE,
                                       AV_CHANNEL_LAYOUT_RETYPE_FLAG_CANONICAL);
        if (ret < 0)
            return ret;

        ret = av_channel_layout_copy(&avctx->ch_layout, &ac->oc[1].ch_layout);
        if (ret < 0)
            return ret;
    } else {
        int nb_elements;
        if ((ret = ff_aac_set_default_channel_config(ac, avctx, layout_map,
                                                     &nb_elements, channel_config_idx)))
            return ret;

        /* Fill in the number of expected channels */
        for (int i = 0; i < nb_elements; i++)
            nb_channels += layout_map[i][0] == TYPE_CPE ? 2 : 1;

        map_pos_set = 1;
    }

    /* UsacDecoderConfig */
    elem_id[0] = elem_id[1] = elem_id[2] = 0;
    usac->nb_elems = get_escaped_value(gb, 4, 8, 16) + 1;
    if (usac->nb_elems > 64) {
        av_log(ac->avctx, AV_LOG_ERROR, "Too many elements: %i\n",
               usac->nb_elems);
        usac->nb_elems = 0;
        return AVERROR(EINVAL);
    }

    for (int i = 0; i < usac->nb_elems; i++) {
        int map_count = elem_id[0] + elem_id[1] + elem_id[2];
        AACUsacElemConfig *e = &usac->elems[i];
        memset(e, 0, sizeof(*e));

        e->type = get_bits(gb, 2); /* usacElementType */
        if (e->type != ID_USAC_EXT && (map_count + 1) > nb_channels) {
            av_log(ac->avctx, AV_LOG_ERROR, "Too many channels for the channel "
                                            "configuration\n");
            usac->nb_elems = 0;
            return AVERROR(EINVAL);
        }

        av_log(ac->avctx, AV_LOG_DEBUG, "Element present: idx %i, type %i\n",
               i, e->type);

        switch (e->type) {
        case ID_USAC_SCE: /* SCE */
            /* UsacCoreConfig */
            decode_usac_element_core(e, gb, sbr_ratio);
            if (e->sbr.ratio > 0) {
                ret = decode_usac_sbr_data(ac, e, gb);
                if (ret < 0)
                    return ret;
            }
            layout_map[map_count][0] = TYPE_SCE;
            layout_map[map_count][1] = elem_id[0]++;
            if (!map_pos_set)
                layout_map[map_count][2] = AAC_CHANNEL_FRONT;

            break;
        case ID_USAC_CPE: /* UsacChannelPairElementConf */
            /* UsacCoreConfig */
            decode_usac_element_core(e, gb, sbr_ratio);
            ret = decode_usac_element_pair(ac, e, gb);
            if (ret < 0)
                return ret;
            layout_map[map_count][0] = TYPE_CPE;
            layout_map[map_count][1] = elem_id[1]++;
            if (!map_pos_set)
                layout_map[map_count][2] = AAC_CHANNEL_FRONT;

            break;
        case ID_USAC_LFE: /* LFE */
            /* LFE has no need for any configuration */
            e->tw_mdct = 0;
            e->noise_fill = 0;
            layout_map[map_count][0] = TYPE_LFE;
            layout_map[map_count][1] = elem_id[2]++;
            if (!map_pos_set)
                layout_map[map_count][2] = AAC_CHANNEL_LFE;

            break;
        case ID_USAC_EXT: /* EXT */
            ret = decode_usac_extension(ac, e, gb);
            if (ret < 0)
                return ret;
            break;
        };
    }

    ret = ff_aac_output_configure(ac, layout_map, elem_id[0] + elem_id[1] + elem_id[2],
                                  OC_GLOBAL_HDR, 0);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Unable to parse channel config!\n");
        usac->nb_elems = 0;
        return ret;
    }

    if (get_bits1(gb)) { /* usacConfigExtensionPresent */
        int invalid;
        int nb_extensions = get_escaped_value(gb, 2, 4, 8) + 1; /* numConfigExtensions */
        for (int i = 0; i < nb_extensions; i++) {
            int type = get_escaped_value(gb, 4, 8, 16);
            int len = get_escaped_value(gb, 4, 8, 16);
            switch (type) {
            case ID_CONFIG_EXT_LOUDNESS_INFO:
                ret = decode_loudness_set(ac, usac, gb);
                if (ret < 0)
                    return ret;
                break;
            case ID_CONFIG_EXT_STREAM_ID:
                usac->stream_identifier = get_bits(gb, 16);
                break;
            case ID_CONFIG_EXT_FILL: /* fallthrough */
                invalid = 0;
                while (len--) {
                    if (get_bits(gb, 8) != 0xA5)
                        invalid++;
                }
                if (invalid)
                    av_log(avctx, AV_LOG_WARNING, "Invalid fill bytes: %i\n",
                           invalid);
                break;
            default:
                while (len--)
                    skip_bits(gb, 8);
                break;
            }
        }
    }

    ac->avctx->profile = AV_PROFILE_AAC_USAC;

    ret = ff_aac_usac_reset_state(ac, oc);
    if (ret < 0)
        return ret;

    return 0;
}

static int decode_usac_scale_factors(AACDecContext *ac,
                                     SingleChannelElement *sce,
                                     GetBitContext *gb, uint8_t global_gain)
{
    IndividualChannelStream *ics = &sce->ics;

    /* Decode all scalefactors. */
    int offset_sf = global_gain;
    for (int g = 0; g < ics->num_window_groups; g++) {
        for (int sfb = 0; sfb < ics->max_sfb; sfb++) {
            if (g || sfb)
                offset_sf += get_vlc2(gb, ff_vlc_scalefactors, 7, 3) - SCALE_DIFF_ZERO;
            if (offset_sf > 255U) {
                av_log(ac->avctx, AV_LOG_ERROR,
                       "Scalefactor (%d) out of range.\n", offset_sf);
                return AVERROR_INVALIDDATA;
            }

            sce->sfo[g*ics->max_sfb + sfb] = offset_sf - 100;
        }
    }

    return 0;
}

/**
 * Decode and dequantize arithmetically coded, uniformly quantized value
 *
 * @param   coef            array of dequantized, scaled spectral data
 * @param   sf              array of scalefactors or intensity stereo positions
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_spectrum_ac(AACDecContext *s, float coef[1024],
                              GetBitContext *gb, AACArithState *state,
                              int reset, uint16_t len, uint16_t N)
{
    AACArith ac;
    int i, a, b;
    uint32_t c;

    int gb_count;
    GetBitContext gb2;

    c = ff_aac_ac_map_process(state, reset, N);

    if (!len) {
        ff_aac_ac_finish(state, 0, N);
        return 0;
    }

    ff_aac_ac_init(&ac, gb);

    /* Backup reader for rolling back by 14 bits at the end */
    gb2 = *gb;
    gb_count = get_bits_count(&gb2);

    for (i = 0; i < len/2; i++) {
        /* MSB */
        int lvl, esc_nb, m;
        c = ff_aac_ac_get_context(state, c, i, N);
        for (lvl=esc_nb=0;;) {
            uint32_t pki = ff_aac_ac_get_pk(c + (esc_nb << 17));
            m = ff_aac_ac_decode(&ac, &gb2, ff_aac_ac_msb_cdfs[pki],
                                 FF_ARRAY_ELEMS(ff_aac_ac_msb_cdfs[pki]));
            if (m < FF_AAC_AC_ESCAPE)
                break;
            lvl++;

            /* Cargo-culted value. */
            if (lvl > 23)
                return AVERROR(EINVAL);

            if ((esc_nb = lvl) > 7)
                esc_nb = 7;
        }

        b = m >> 2;
        a = m - (b << 2);

        /* ARITH_STOP detection */
        if (!m) {
            if (esc_nb)
                break;
            a = b = 0;
        }

        /* LSB */
        for (int l = lvl; l > 0; l--) {
            int lsbidx = !a ? 1 : (!b ? 0 : 2);
            uint8_t r = ff_aac_ac_decode(&ac, &gb2, ff_aac_ac_lsb_cdfs[lsbidx],
                                         FF_ARRAY_ELEMS(ff_aac_ac_lsb_cdfs[lsbidx]));
            a = (a << 1) | (r & 1);
            b = (b << 1) | ((r >> 1) & 1);
        }

        /* Dequantize coeffs here */
        coef[2*i + 0] = a * cbrt(a);
        coef[2*i + 1] = b * cbrt(b);
        ff_aac_ac_update_context(state, i, a, b);
    }

    if (len > 1) {
        /* "Rewind" bitstream back by 14 bits */
        int gb_count2 = get_bits_count(&gb2);
        skip_bits(gb, gb_count2 - gb_count - 14);
    } else {
        *gb = gb2;
    }

    ff_aac_ac_finish(state, i, N);

    for (; i < N/2; i++) {
        coef[2*i + 0] = 0;
        coef[2*i + 1] = 0;
    }

    /* Signs */
    for (i = 0; i < len; i++) {
        if (coef[i]) {
            if (!get_bits1(gb)) /* s */
                coef[i] *= -1;
        }
    }

    return 0;
}

static int decode_usac_stereo_cplx(AACDecContext *ac, AACUsacStereo *us,
                                   ChannelElement *cpe, GetBitContext *gb,
                                   int num_window_groups,
                                   int prev_num_window_groups,
                                   int indep_flag)
{
    int delta_code_time;
    IndividualChannelStream *ics = &cpe->ch[0].ics;

    if (!get_bits1(gb)) { /* cplx_pred_all */
        for (int g = 0; g < num_window_groups; g++) {
            for (int sfb = 0; sfb < cpe->max_sfb_ste; sfb += SFB_PER_PRED_BAND) {
                const uint8_t val = get_bits1(gb);
                us->pred_used[g*cpe->max_sfb_ste + sfb] = val;
                if ((sfb + 1) < cpe->max_sfb_ste)
                    us->pred_used[g*cpe->max_sfb_ste + sfb + 1] = val;
            }
        }
    } else {
        for (int g = 0; g < num_window_groups; g++)
            for (int sfb = 0; sfb < cpe->max_sfb_ste; sfb++)
                us->pred_used[g*cpe->max_sfb_ste + sfb] = 1;
    }

    us->pred_dir = get_bits1(gb);
    us->complex_coef = get_bits1(gb);

    us->use_prev_frame = 0;
    if (us->complex_coef && !indep_flag)
        us->use_prev_frame = get_bits1(gb);

    delta_code_time = 0;
    if (!indep_flag)
        delta_code_time = get_bits1(gb);

    /* TODO: shouldn't be needed */
    for (int g = 0; g < num_window_groups; g++) {
        for (int sfb = 0; sfb < cpe->max_sfb_ste; sfb += SFB_PER_PRED_BAND) {
            float last_alpha_q_re = 0;
            float last_alpha_q_im = 0;
            if (delta_code_time) {
                if (g) {
                    /* Transient, after the first group - use the current frame,
                     * previous window, alpha values. */
                    last_alpha_q_re = us->alpha_q_re[(g - 1)*cpe->max_sfb_ste + sfb];
                    last_alpha_q_im = us->alpha_q_im[(g - 1)*cpe->max_sfb_ste + sfb];
                } else if (!g &&
                           (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) &&
                           (ics->window_sequence[1] == EIGHT_SHORT_SEQUENCE)) {
                    /* The spec doesn't explicitly mention this, but it doesn't make
                     * any other sense otherwise! */
                    const int wg = prev_num_window_groups - 1;
                    last_alpha_q_re = us->prev_alpha_q_re[wg*cpe->max_sfb_ste + sfb];
                    last_alpha_q_im = us->prev_alpha_q_im[wg*cpe->max_sfb_ste + sfb];
                } else {
                    last_alpha_q_re = us->prev_alpha_q_re[g*cpe->max_sfb_ste + sfb];
                    last_alpha_q_im = us->prev_alpha_q_im[g*cpe->max_sfb_ste + sfb];
                }
            } else {
                if (sfb) {
                    last_alpha_q_re = us->alpha_q_re[g*cpe->max_sfb_ste + sfb - 1];
                    last_alpha_q_im = us->alpha_q_im[g*cpe->max_sfb_ste + sfb - 1];
                }
            }

            if (us->pred_used[g*cpe->max_sfb_ste + sfb]) {
                int val = -get_vlc2(gb, ff_vlc_scalefactors, 7, 3) + 60;
                last_alpha_q_re += val * 0.1f;
                if (us->complex_coef) {
                    val = -get_vlc2(gb, ff_vlc_scalefactors, 7, 3) + 60;
                    last_alpha_q_im += val * 0.1f;
                }
                us->alpha_q_re[g*cpe->max_sfb_ste + sfb] = last_alpha_q_re;
                us->alpha_q_im[g*cpe->max_sfb_ste + sfb] = last_alpha_q_im;
            } else {
                us->alpha_q_re[g*cpe->max_sfb_ste + sfb] = 0;
                us->alpha_q_im[g*cpe->max_sfb_ste + sfb] = 0;
            }

            if ((sfb + 1) < cpe->max_sfb_ste) {
                us->alpha_q_re[g*cpe->max_sfb_ste + sfb + 1] =
                    us->alpha_q_re[g*cpe->max_sfb_ste + sfb];
                us->alpha_q_im[g*cpe->max_sfb_ste + sfb + 1] =
                    us->alpha_q_im[g*cpe->max_sfb_ste + sfb];
            }
        }
    }

    return 0;
}

static int setup_sce(AACDecContext *ac, SingleChannelElement *sce,
                     AACUSACConfig *usac)
{
    AACUsacElemData *ue = &sce->ue;
    IndividualChannelStream *ics = &sce->ics;
    const int sampling_index = ac->oc[1].m4ac.sampling_index;

    /* Setup window parameters */
    ics->prev_num_window_groups = FFMAX(ics->num_window_groups, 1);
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        if (usac->core_frame_len == 768) {
            ics->swb_offset = ff_swb_offset_96[sampling_index];
            ics->num_swb = ff_aac_num_swb_96[sampling_index];
        } else {
            ics->swb_offset = ff_swb_offset_128[sampling_index];
            ics->num_swb = ff_aac_num_swb_128[sampling_index];
        }
        ics->tns_max_bands = ff_tns_max_bands_usac_128[sampling_index];

        /* Setup scalefactor grouping. 7 bit mask. */
        ics->num_window_groups = 0;
        for (int j = 0; j < 7; j++) {
            ics->group_len[j] = 1;
            if (ue->scale_factor_grouping & (1 << (6 - j)))
                ics->group_len[ics->num_window_groups] += 1;
            else
                ics->num_window_groups++;
        }

        ics->group_len[7] = 1;
        ics->num_window_groups++;
        ics->num_windows = 8;
    } else {
        if (usac->core_frame_len == 768) {
            ics->swb_offset = ff_swb_offset_768[sampling_index];
            ics->num_swb = ff_aac_num_swb_768[sampling_index];
        } else {
            ics->swb_offset = ff_swb_offset_1024[sampling_index];
            ics->num_swb = ff_aac_num_swb_1024[sampling_index];
        }
        ics->tns_max_bands = ff_tns_max_bands_usac_1024[sampling_index];

        ics->group_len[0] = 1;
        ics->num_window_groups = 1;
        ics->num_windows  = 1;
    }

    if (ics->max_sfb > ics->num_swb) {
        av_log(ac->avctx, AV_LOG_ERROR,
               "Number of scalefactor bands in group (%d) "
               "exceeds limit (%d).\n",
               ics->max_sfb, ics->num_swb);
        ics->max_sfb = 0;
        return AVERROR(EINVAL);
    }

    /* Just some defaults for the band types */
    for (int i = 0; i < FF_ARRAY_ELEMS(sce->band_type); i++)
        sce->band_type[i] = ESC_BT;

    return 0;
}

static int decode_usac_stereo_info(AACDecContext *ac, AACUSACConfig *usac,
                                   AACUsacElemConfig *ec, ChannelElement *cpe,
                                   GetBitContext *gb, int indep_flag)
{
    int ret, tns_active;

    AACUsacStereo *us = &cpe->us;
    SingleChannelElement *sce1 = &cpe->ch[0];
    SingleChannelElement *sce2 = &cpe->ch[1];
    IndividualChannelStream *ics1 = &sce1->ics;
    IndividualChannelStream *ics2 = &sce2->ics;
    AACUsacElemData *ue1 = &sce1->ue;
    AACUsacElemData *ue2 = &sce2->ue;

    us->common_window = 0;
    us->common_tw = 0;

    /* Alpha values must always be zeroed out for the current frame,
     * as they are propagated to the next frame and may be used. */
    memset(us->alpha_q_re, 0, sizeof(us->alpha_q_re));
    memset(us->alpha_q_im, 0, sizeof(us->alpha_q_im));

    if (!(!ue1->core_mode && !ue2->core_mode))
        return 0;

    tns_active = get_bits1(gb);
    us->common_window = get_bits1(gb);

    if (!us->common_window || indep_flag) {
        memset(us->prev_alpha_q_re, 0, sizeof(us->prev_alpha_q_re));
        memset(us->prev_alpha_q_im, 0, sizeof(us->prev_alpha_q_im));
    }

    if (us->common_window) {
        /* ics_info() */
        ics1->window_sequence[1] = ics1->window_sequence[0];
        ics2->window_sequence[1] = ics2->window_sequence[0];
        ics1->window_sequence[0] = ics2->window_sequence[0] = get_bits(gb, 2);

        ics1->use_kb_window[1] = ics1->use_kb_window[0];
        ics2->use_kb_window[1] = ics2->use_kb_window[0];
        ics1->use_kb_window[0] = ics2->use_kb_window[0] = get_bits1(gb);

        /* If there's a change in the transform sequence, zero out last frame's
         * stereo prediction coefficients */
        if ((ics1->window_sequence[0] == EIGHT_SHORT_SEQUENCE &&
             ics1->window_sequence[1] != EIGHT_SHORT_SEQUENCE) ||
            (ics1->window_sequence[1] == EIGHT_SHORT_SEQUENCE &&
             ics1->window_sequence[0] != EIGHT_SHORT_SEQUENCE) ||
            (ics2->window_sequence[0] == EIGHT_SHORT_SEQUENCE &&
             ics2->window_sequence[1] != EIGHT_SHORT_SEQUENCE) ||
            (ics2->window_sequence[1] == EIGHT_SHORT_SEQUENCE &&
             ics2->window_sequence[0] != EIGHT_SHORT_SEQUENCE)) {
            memset(us->prev_alpha_q_re, 0, sizeof(us->prev_alpha_q_re));
            memset(us->prev_alpha_q_im, 0, sizeof(us->prev_alpha_q_im));
        }

        if (ics1->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
            ics1->max_sfb = ics2->max_sfb = get_bits(gb, 4);
            ue1->scale_factor_grouping = ue2->scale_factor_grouping = get_bits(gb, 7);
        } else {
            ics1->max_sfb = ics2->max_sfb = get_bits(gb, 6);
        }

        if (!get_bits1(gb)) { /* common_max_sfb */
            if (ics2->window_sequence[0] == EIGHT_SHORT_SEQUENCE)
                ics2->max_sfb = get_bits(gb, 4);
            else
                ics2->max_sfb = get_bits(gb, 6);
        }

        ret = setup_sce(ac, sce1, usac);
        if (ret < 0) {
            ics2->max_sfb = 0;
            return ret;
        }

        ret = setup_sce(ac, sce2, usac);
        if (ret < 0)
            return ret;

        cpe->max_sfb_ste = FFMAX(ics1->max_sfb, ics2->max_sfb);

        us->ms_mask_mode = get_bits(gb, 2); /* ms_mask_present */
        memset(cpe->ms_mask, 0, sizeof(cpe->ms_mask));
        if (us->ms_mask_mode == 1) {
            for (int g = 0; g < ics1->num_window_groups; g++)
                for (int sfb = 0; sfb < cpe->max_sfb_ste; sfb++)
                    cpe->ms_mask[g*cpe->max_sfb_ste + sfb] = get_bits1(gb);
        } else if (us->ms_mask_mode == 2) {
            memset(cpe->ms_mask, 0xFF, sizeof(cpe->ms_mask));
        } else if ((us->ms_mask_mode == 3) && !ec->stereo_config_index) {
            ret = decode_usac_stereo_cplx(ac, us, cpe, gb,
                                          ics1->num_window_groups,
                                          ics1->prev_num_window_groups,
                                          indep_flag);
            if (ret < 0)
                return ret;
        }
    }

    if (ec->tw_mdct) {
        us->common_tw = get_bits1(gb);
        avpriv_report_missing_feature(ac->avctx,
                                      "AAC USAC timewarping");
        return AVERROR_PATCHWELCOME;
    }

    us->tns_on_lr = 0;
    ue1->tns_data_present = ue2->tns_data_present = 0;
    if (tns_active) {
        int common_tns = 0;
        if (us->common_window)
            common_tns = get_bits1(gb);

        us->tns_on_lr = get_bits1(gb);
        if (common_tns) {
            ret = ff_aac_decode_tns(ac, &sce1->tns, gb, ics1);
            if (ret < 0)
                return ret;
            memcpy(&sce2->tns, &sce1->tns, sizeof(sce1->tns));
            sce2->tns.present = 1;
            sce1->tns.present = 1;
            ue1->tns_data_present = 0;
            ue2->tns_data_present = 0;
        } else {
            if (get_bits1(gb)) {
                ue1->tns_data_present = 1;
                ue2->tns_data_present = 1;
            } else {
                ue2->tns_data_present = get_bits1(gb);
                ue1->tns_data_present = !ue2->tns_data_present;
            }
        }
    }

    return 0;
}

/* 7.2.4 Generation of random signs for spectral noise filling
 * This function is exactly defined, though we've helped the definition
 * along with being slightly faster. */
static inline float noise_random_sign(unsigned int *seed)
{
    unsigned int new_seed = *seed = ((*seed) * 69069) + 5;
    if (((new_seed) & 0x10000) > 0)
        return -1.f;
    return +1.f;
}

static void apply_noise_fill(AACDecContext *ac, SingleChannelElement *sce,
                             AACUsacElemData *ue)
{
    float *coef;
    IndividualChannelStream *ics = &sce->ics;

    float noise_val = powf(2, ((float)ue->noise.level - 14.0f)/3.0f);
    int noise_offset = ue->noise.offset - 16;
    int band_off;

    band_off = ff_usac_noise_fill_start_offset[ac->oc[1].m4ac.frame_length_short]
                                              [ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE];

    coef = sce->coeffs;
    for (int g = 0; g < ics->num_window_groups; g++) {
        unsigned g_len = ics->group_len[g];

        for (int sfb = 0; sfb < ics->max_sfb; sfb++) {
            float *cb = coef + ics->swb_offset[sfb];
            int cb_len = ics->swb_offset[sfb + 1] - ics->swb_offset[sfb];
            int band_quantized_to_zero = 1;

            if (ics->swb_offset[sfb] < band_off)
                continue;

            for (int group = 0; group < (unsigned)g_len; group++, cb += 128) {
                for (int z = 0; z < cb_len; z++) {
                    if (cb[z] == 0)
                        cb[z] = noise_random_sign(&sce->ue.noise.seed) * noise_val;
                    else
                        band_quantized_to_zero = 0;
                }
            }

            if (band_quantized_to_zero)
                sce->sfo[g*ics->max_sfb + sfb] += noise_offset;
        }
        coef += g_len << 7;
    }
}

static void spectrum_scale(AACDecContext *ac, SingleChannelElement *sce,
                           AACUsacElemData *ue)
{
    IndividualChannelStream *ics = &sce->ics;
    float *coef;

    /* Synthesise noise */
    if (ue->noise.level)
        apply_noise_fill(ac, sce, ue);

    /* Noise filling may apply an offset to the scalefactor offset */
    ac->dsp.dequant_scalefactors(sce);

    /* Apply scalefactors */
    coef = sce->coeffs;
    for (int g = 0; g < ics->num_window_groups; g++) {
        unsigned g_len = ics->group_len[g];

        for (int sfb = 0; sfb < ics->max_sfb; sfb++) {
            float *cb = coef + ics->swb_offset[sfb];
            int cb_len = ics->swb_offset[sfb + 1] - ics->swb_offset[sfb];
            float sf = sce->sf[g*ics->max_sfb + sfb];

            for (int group = 0; group < (unsigned)g_len; group++, cb += 128)
                ac->fdsp->vector_fmul_scalar(cb, cb, sf, cb_len);
        }
        coef += g_len << 7;
    }
}

static void complex_stereo_downmix_prev(AACDecContext *ac, ChannelElement *cpe,
                                        float *dmix_re)
{
    IndividualChannelStream *ics = &cpe->ch[0].ics;
    int sign = !cpe->us.pred_dir ? +1 : -1;
    float *coef1 = cpe->ch[0].coeffs;
    float *coef2 = cpe->ch[1].coeffs;

    for (int g = 0; g < ics->num_window_groups; g++) {
        unsigned g_len = ics->group_len[g];
        for (int sfb = 0; sfb < cpe->max_sfb_ste; sfb++) {
            int off = ics->swb_offset[sfb];
            int cb_len = ics->swb_offset[sfb + 1] - off;

            float *c1 = coef1 + off;
            float *c2 = coef2 + off;
            float *dm = dmix_re + off;

            for (int group = 0; group < (unsigned)g_len;
                 group++, c1 += 128, c2 += 128, dm += 128) {
                for (int z = 0; z < cb_len; z++)
                    dm[z] = 0.5*(c1[z] + sign*c2[z]);
            }
        }

        coef1 += g_len << 7;
        coef2 += g_len << 7;
        dmix_re += g_len << 7;
    }
}

static void complex_stereo_downmix_cur(AACDecContext *ac, ChannelElement *cpe,
                                       float *dmix_re)
{
    AACUsacStereo *us = &cpe->us;
    IndividualChannelStream *ics = &cpe->ch[0].ics;
    int sign = !cpe->us.pred_dir ? +1 : -1;
    float *coef1 = cpe->ch[0].coeffs;
    float *coef2 = cpe->ch[1].coeffs;

    for (int g = 0; g < ics->num_window_groups; g++) {
        unsigned g_len = ics->group_len[g];
        for (int sfb = 0; sfb < cpe->max_sfb_ste; sfb++) {
            int off = ics->swb_offset[sfb];
            int cb_len = ics->swb_offset[sfb + 1] - off;

            float *c1 = coef1 + off;
            float *c2 = coef2 + off;
            float *dm = dmix_re + off;

            if (us->pred_used[g*cpe->max_sfb_ste + sfb]) {
                for (int group = 0; group < (unsigned)g_len;
                     group++, c1 += 128, c2 += 128, dm += 128) {
                    for (int z = 0; z < cb_len; z++)
                        dm[z] = 0.5*(c1[z] + sign*c2[z]);
                }
            } else {
                for (int group = 0; group < (unsigned)g_len;
                     group++, c1 += 128, c2 += 128, dm += 128) {
                    for (int z = 0; z < cb_len; z++)
                        dm[z] = c1[z];
                }
            }
        }

        coef1 += g_len << 7;
        coef2 += g_len << 7;
        dmix_re += g_len << 7;
    }
}

static void complex_stereo_interpolate_imag(float *im, float *re, const float f[7],
                                            int len, int factor_even, int factor_odd)
{
    int i = 0;
    float s;

    s = f[6]*re[2] + f[5]*re[1] + f[4]*re[0] +
        f[3]*re[0] +
        f[2]*re[1] + f[1]*re[2] + f[0]*re[3];
    im[i] += s*factor_even;

    i = 1;
    s = f[6]*re[1] + f[5]*re[0] + f[4]*re[0] +
        f[3]*re[1] +
        f[2]*re[2] + f[1]*re[3] + f[0]*re[4];
    im[i] += s*factor_odd;

    i = 2;
    s = f[6]*re[0] + f[5]*re[0] + f[4]*re[1] +
        f[3]*re[2] +
        f[2]*re[3] + f[1]*re[4] + f[0]*re[5];

    im[i] += s*factor_even;
    for (i = 3; i < len - 4; i += 2) {
        s = f[6]*re[i-3] + f[5]*re[i-2] + f[4]*re[i-1] +
            f[3]*re[i] +
            f[2]*re[i+1] + f[1]*re[i+2] + f[0]*re[i+3];
        im[i+0] += s*factor_odd;

        s = f[6]*re[i-2] + f[5]*re[i-1] + f[4]*re[i] +
            f[3]*re[i+1] +
            f[2]*re[i+2] + f[1]*re[i+3] + f[0]*re[i+4];
        im[i+1] += s*factor_even;
    }

    i = len - 3;
    s = f[6]*re[i-3] + f[5]*re[i-2] + f[4]*re[i-1] +
        f[3]*re[i] +
        f[2]*re[i+1] + f[1]*re[i+2] + f[0]*re[i+2];
    im[i] += s*factor_odd;

    i = len - 2;
    s = f[6]*re[i-3] + f[5]*re[i-2] + f[4]*re[i-1] +
        f[3]*re[i] +
        f[2]*re[i+1] + f[1]*re[i+1] + f[0]*re[i];
    im[i] += s*factor_even;

    i = len - 1;
    s = f[6]*re[i-3] + f[5]*re[i-2] + f[4]*re[i-1] +
        f[3]*re[i] +
        f[2]*re[i] + f[1]*re[i-1] + f[0]*re[i-2];
    im[i] += s*factor_odd;
}

static void apply_complex_stereo(AACDecContext *ac, ChannelElement *cpe)
{
    AACUsacStereo *us = &cpe->us;
    IndividualChannelStream *ics = &cpe->ch[0].ics;
    float *coef1 = cpe->ch[0].coeffs;
    float *coef2 = cpe->ch[1].coeffs;
    float *dmix_im = us->dmix_im;

    for (int g = 0; g < ics->num_window_groups; g++) {
        unsigned g_len = ics->group_len[g];
        for (int sfb = 0; sfb < cpe->max_sfb_ste; sfb++) {
            int off = ics->swb_offset[sfb];
            int cb_len = ics->swb_offset[sfb + 1] - off;

            float *c1 = coef1 + off;
            float *c2 = coef2 + off;
            float *dm_im = dmix_im + off;
            float alpha_re = us->alpha_q_re[g*cpe->max_sfb_ste + sfb];
            float alpha_im = us->alpha_q_im[g*cpe->max_sfb_ste + sfb];

            if (!us->pred_used[g*cpe->max_sfb_ste + sfb])
                continue;

            if (!cpe->us.pred_dir) {
                for (int group = 0; group < (unsigned)g_len;
                     group++, c1 += 128, c2 += 128, dm_im += 128) {
                    for (int z = 0; z < cb_len; z++) {
                        float side;
                        side = c2[z] - alpha_re*c1[z] - alpha_im*dm_im[z];
                        c2[z] = c1[z] - side;
                        c1[z] = c1[z] + side;
                    }
                }
            } else {
                for (int group = 0; group < (unsigned)g_len;
                     group++, c1 += 128, c2 += 128, dm_im += 128) {
                    for (int z = 0; z < cb_len; z++) {
                        float mid;
                        mid = c2[z] - alpha_re*c1[z] - alpha_im*dm_im[z];
                        c2[z] = mid - c1[z];
                        c1[z] = mid + c1[z];
                    }
                }
            }
        }

        coef1 += g_len << 7;
        coef2 += g_len << 7;
        dmix_im += g_len << 7;
    }
}

static const float *complex_stereo_get_filter(ChannelElement *cpe, int is_prev)
{
    int win, shape;
    if (!is_prev) {
        switch (cpe->ch[0].ics.window_sequence[0]) {
        default:
        case ONLY_LONG_SEQUENCE:
        case EIGHT_SHORT_SEQUENCE:
            win = 0;
            break;
        case LONG_START_SEQUENCE:
            win = 1;
            break;
        case LONG_STOP_SEQUENCE:
            win = 2;
            break;
        }

        if (cpe->ch[0].ics.use_kb_window[0] == 0 &&
            cpe->ch[0].ics.use_kb_window[1] == 0)
            shape = 0;
        else if (cpe->ch[0].ics.use_kb_window[0] == 1 &&
                 cpe->ch[0].ics.use_kb_window[1] == 1)
            shape = 1;
        else if (cpe->ch[0].ics.use_kb_window[0] == 0 &&
                 cpe->ch[0].ics.use_kb_window[1] == 1)
            shape = 2;
        else if (cpe->ch[0].ics.use_kb_window[0] == 1 &&
                 cpe->ch[0].ics.use_kb_window[1] == 0)
            shape = 3;
        else
            shape = 3;
    } else {
        win = cpe->ch[0].ics.window_sequence[0] == LONG_STOP_SEQUENCE;
        shape = cpe->ch[0].ics.use_kb_window[1];
    }

    return ff_aac_usac_mdst_filt_cur[win][shape];
}

static void spectrum_decode(AACDecContext *ac, AACUSACConfig *usac,
                            ChannelElement *cpe, int nb_channels)
{
    AACUsacStereo *us = &cpe->us;

    for (int ch = 0; ch < nb_channels; ch++) {
        SingleChannelElement *sce = &cpe->ch[ch];
        AACUsacElemData *ue = &sce->ue;

        spectrum_scale(ac, sce, ue);
    }

    if (nb_channels > 1 && us->common_window) {
        for (int ch = 0; ch < nb_channels; ch++) {
            SingleChannelElement *sce = &cpe->ch[ch];

            /* Apply TNS, if the tns_on_lr bit is not set. */
            if (sce->tns.present && !us->tns_on_lr)
                ac->dsp.apply_tns(sce->coeffs, &sce->tns, &sce->ics, 1);
        }

        if (us->ms_mask_mode == 3) {
            const float *filt;
            complex_stereo_downmix_cur(ac, cpe, us->dmix_re);
            complex_stereo_downmix_prev(ac, cpe, us->prev_dmix_re);

            filt = complex_stereo_get_filter(cpe, 0);
            complex_stereo_interpolate_imag(us->dmix_im, us->dmix_re, filt,
                                            usac->core_frame_len, 1, 1);
            if (us->use_prev_frame) {
                filt = complex_stereo_get_filter(cpe, 1);
                complex_stereo_interpolate_imag(us->dmix_im, us->prev_dmix_re, filt,
                                                usac->core_frame_len, -1, 1);
            }

            apply_complex_stereo(ac, cpe);
        } else if (us->ms_mask_mode > 0) {
            ac->dsp.apply_mid_side_stereo(ac, cpe);
        }
    }

    /* Save coefficients and alpha values for prediction reasons */
    if (nb_channels > 1) {
        AACUsacStereo *us = &cpe->us;
        for (int ch = 0; ch < nb_channels; ch++) {
            SingleChannelElement *sce = &cpe->ch[ch];
            memcpy(sce->prev_coeffs, sce->coeffs, sizeof(sce->coeffs));
        }
        memcpy(us->prev_alpha_q_re, us->alpha_q_re, sizeof(us->alpha_q_re));
        memcpy(us->prev_alpha_q_im, us->alpha_q_im, sizeof(us->alpha_q_im));
    }

    for (int ch = 0; ch < nb_channels; ch++) {
        SingleChannelElement *sce = &cpe->ch[ch];

        /* Apply TNS, if it hasn't been applied yet. */
        if (sce->tns.present && ((nb_channels == 1) || (us->tns_on_lr)))
            ac->dsp.apply_tns(sce->coeffs, &sce->tns, &sce->ics, 1);

        ac->oc[1].m4ac.frame_length_short ? ac->dsp.imdct_and_windowing_768(ac, sce) :
                                            ac->dsp.imdct_and_windowing(ac, sce);
    }
}

static int decode_usac_core_coder(AACDecContext *ac, AACUSACConfig *usac,
                                  AACUsacElemConfig *ec, ChannelElement *che,
                                  GetBitContext *gb, int indep_flag, int nb_channels)
{
    int ret;
    int arith_reset_flag;
    AACUsacStereo *us = &che->us;
    int core_nb_channels = nb_channels;

    /* Local symbols */
    uint8_t global_gain;

    us->common_window = 0;

    for (int ch = 0; ch < core_nb_channels; ch++) {
        SingleChannelElement *sce = &che->ch[ch];
        AACUsacElemData *ue = &sce->ue;

        sce->tns.present = 0;
        ue->tns_data_present = 0;

        ue->core_mode = get_bits1(gb);
    }

    if (nb_channels > 1 && ec->stereo_config_index == 1)
        core_nb_channels = 1;

    if (core_nb_channels == 2) {
        ret = decode_usac_stereo_info(ac, usac, ec, che, gb, indep_flag);
        if (ret)
            return ret;
    }

    for (int ch = 0; ch < core_nb_channels; ch++) {
        SingleChannelElement *sce = &che->ch[ch];
        IndividualChannelStream *ics = &sce->ics;
        AACUsacElemData *ue = &sce->ue;

        if (ue->core_mode) { /* lpd_channel_stream */
            ret = ff_aac_ldp_parse_channel_stream(ac, usac, ue, gb);
            if (ret < 0)
                return ret;
            continue;
        }

        if ((core_nb_channels == 1) ||
            (che->ch[0].ue.core_mode != che->ch[1].ue.core_mode))
            ue->tns_data_present = get_bits1(gb);

        /* fd_channel_stream */
        global_gain = get_bits(gb, 8);

        ue->noise.level = 0;
        if (ec->noise_fill) {
            ue->noise.level = get_bits(gb, 3);
            ue->noise.offset = get_bits(gb, 5);
        }

        if (!us->common_window) {
            /* ics_info() */
            ics->window_sequence[1] = ics->window_sequence[0];
            ics->window_sequence[0] = get_bits(gb, 2);
            ics->use_kb_window[1] = ics->use_kb_window[0];
            ics->use_kb_window[0] = get_bits1(gb);
            if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
                ics->max_sfb = get_bits(gb, 4);
                ue->scale_factor_grouping = get_bits(gb, 7);
            } else {
                ics->max_sfb = get_bits(gb, 6);
            }

            ret = setup_sce(ac, sce, usac);
            if (ret < 0)
                return ret;
        }

        if (ec->tw_mdct && !us->common_tw) {
            /* tw_data() */
            if (get_bits1(gb)) { /* tw_data_present */
                /* Time warping is not supported in baseline profile streams. */
                avpriv_report_missing_feature(ac->avctx,
                                              "AAC USAC timewarping");
                return AVERROR_PATCHWELCOME;
            }
        }

        ret = decode_usac_scale_factors(ac, sce, gb, global_gain);
        if (ret < 0)
            return ret;

        if (ue->tns_data_present) {
            sce->tns.present = 1;
            ret = ff_aac_decode_tns(ac, &sce->tns, gb, ics);
            if (ret < 0)
                return ret;
        }

        /* ac_spectral_data */
        arith_reset_flag = indep_flag;
        if (!arith_reset_flag)
            arith_reset_flag = get_bits1(gb);

        /* Decode coeffs */
        memset(&sce->coeffs[0], 0, 1024*sizeof(float));
        for (int win = 0; win < ics->num_windows; win++) {
            int lg = ics->swb_offset[ics->max_sfb];
            int N;
            if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE)
                N = usac->core_frame_len / 8;
            else
                N = usac->core_frame_len;

            ret = decode_spectrum_ac(ac, sce->coeffs + win*128, gb, &ue->ac,
                                     arith_reset_flag && (win == 0), lg, N);
            if (ret < 0)
                return ret;
        }

        if (get_bits1(gb)) { /* fac_data_present */
            const uint16_t len_8 = usac->core_frame_len / 8;
            const uint16_t len_16 = usac->core_frame_len / 16;
            const uint16_t fac_len = ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE ? len_16 : len_8;
            ret = ff_aac_parse_fac_data(ue, gb, 1, fac_len);
            if (ret < 0)
                return ret;
        }
    }

    if (ec->sbr.ratio) {
        int sbr_ch = nb_channels;
        if (nb_channels == 2 &&
            !(ec->stereo_config_index == 0 || ec->stereo_config_index == 3))
            sbr_ch = 1;

        ret = ff_aac_sbr_decode_usac_data(ac, che, ec, gb, sbr_ch, indep_flag);
        if (ret < 0)
            return ret;

        if (ec->stereo_config_index) {
            avpriv_report_missing_feature(ac->avctx, "AAC USAC Mps212");
            return AVERROR_PATCHWELCOME;
        }
    }

    spectrum_decode(ac, usac, che, core_nb_channels);

    if (ac->oc[1].m4ac.sbr > 0) {
        ac->proc.sbr_apply(ac, che, nb_channels == 2 ? TYPE_CPE : TYPE_SCE,
                           che->ch[0].output,
                           che->ch[1].output);
    }

    return 0;
}

static int parse_audio_preroll(AACDecContext *ac, GetBitContext *gb)
{
    int ret = 0;
    GetBitContext gbc;
    OutputConfiguration *oc = &ac->oc[1];
    MPEG4AudioConfig *m4ac = &oc->m4ac;
    MPEG4AudioConfig m4ac_bak = oc->m4ac;
    uint8_t temp_data[512];
    uint8_t *tmp_buf = temp_data;
    size_t tmp_buf_size = sizeof(temp_data);

    av_unused int crossfade;
    int num_preroll_frames;

    int config_len = get_escaped_value(gb, 4, 4, 8);

    /* Implementations are free to pad the config to any length, so use a
     * different reader for this. */
    gbc = *gb;
    ret = ff_aac_usac_config_decode(ac, ac->avctx, &gbc, oc, m4ac->chan_config);
    if (ret < 0) {
        *m4ac = m4ac_bak;
        return ret;
    } else {
        ac->oc[1].m4ac.chan_config = 0;
    }

    /* 7.18.3.3 Bitrate adaption
     * If configuration didn't change after applying preroll, continue
     * without decoding it. */
    if (!memcmp(m4ac, &m4ac_bak, sizeof(m4ac_bak)))
        return 0;

    skip_bits_long(gb, config_len*8);

    crossfade = get_bits1(gb); /* applyCrossfade */
    skip_bits1(gb); /* reserved */
    num_preroll_frames = get_escaped_value(gb, 2, 4, 0); /* numPreRollFrames */

    for (int i = 0; i < num_preroll_frames; i++) {
        int got_frame_ptr = 0;
        int au_len = get_escaped_value(gb, 16, 16, 0);

        if (au_len*8 > tmp_buf_size) {
            uint8_t *tmp2;
            tmp_buf = tmp_buf == temp_data ? NULL : tmp_buf;
            tmp2 = av_realloc_array(tmp_buf, au_len, 8);
            if (!tmp2) {
                if (tmp_buf != temp_data)
                    av_free(tmp_buf);
                return AVERROR(ENOMEM);
            }
            tmp_buf = tmp2;
        }

        /* Byte alignment is not guaranteed. */
        for (int i = 0; i < au_len; i++)
            tmp_buf[i] = get_bits(gb, 8);

        ret = init_get_bits8(&gbc, tmp_buf, au_len);
        if (ret < 0)
            break;

        ret = ff_aac_usac_decode_frame(ac->avctx, ac, &gbc, &got_frame_ptr);
        if (ret < 0)
            break;
    }

    if (tmp_buf != temp_data)
        av_free(tmp_buf);

    return 0;
}

static int parse_ext_ele(AACDecContext *ac, AACUsacElemConfig *e,
                         GetBitContext *gb)
{
    uint8_t *tmp;
    uint8_t pl_frag_start = 1;
    uint8_t pl_frag_end = 1;
    uint32_t len;

    if (!get_bits1(gb)) /* usacExtElementPresent */
        return 0;

    if (get_bits1(gb)) { /* usacExtElementUseDefaultLength */
        len = e->ext.default_len;
    } else {
        len = get_bits(gb, 8); /* usacExtElementPayloadLength */
        if (len == 255)
            len += get_bits(gb, 16) - 2;
    }

    if (!len)
        return 0;

    if (e->ext.payload_frag) {
        pl_frag_start = get_bits1(gb); /* usacExtElementStart */
        pl_frag_end = get_bits1(gb); /* usacExtElementStop */
    }

    if (pl_frag_start)
        e->ext.pl_data_offset = 0;

    /* If an extension starts and ends this packet, we can directly use it */
    if (!(pl_frag_start && pl_frag_end)) {
        tmp = av_realloc(e->ext.pl_data, e->ext.pl_data_offset + len);
        if (!tmp) {
            av_free(e->ext.pl_data);
            return AVERROR(ENOMEM);
        }
        e->ext.pl_data = tmp;

        /* Readout data to a buffer */
        for (int i = 0; i < len; i++)
            e->ext.pl_data[e->ext.pl_data_offset + i] = get_bits(gb, 8);
    }

    e->ext.pl_data_offset += len;

    if (pl_frag_end) {
        int ret = 0;
        int start_bits = get_bits_count(gb);
        const int pl_len = e->ext.pl_data_offset;
        GetBitContext *gb2 = gb;
        GetBitContext gbc;
        if (!(pl_frag_start && pl_frag_end)) {
            ret = init_get_bits8(&gbc, e->ext.pl_data, pl_len);
            if (ret < 0)
                return ret;

            gb2 = &gbc;
        }

        switch (e->ext.type) {
        case ID_EXT_ELE_FILL:
            /* Filler elements have no usable payload */
            break;
        case ID_EXT_ELE_AUDIOPREROLL:
            ret = parse_audio_preroll(ac, gb2);
            break;
        default:
            /* This should never happen */
            av_assert0(0);
        }
        av_freep(&e->ext.pl_data);
        if (ret < 0)
            return ret;

        skip_bits_long(gb, pl_len*8 - (get_bits_count(gb) - start_bits));
    }

    return 0;
}

int ff_aac_usac_decode_frame(AVCodecContext *avctx, AACDecContext *ac,
                             GetBitContext *gb, int *got_frame_ptr)
{
    int ret, is_dmono = 0;
    int indep_flag, samples = 0;
    int audio_found = 0;
    int elem_id[3 /* SCE, CPE, LFE */] = { 0, 0, 0 };
    AVFrame *frame = ac->frame;

    int ratio_mult, ratio_dec;
    AACUSACConfig *usac = &ac->oc[1].usac;
    int sbr_ratio = usac->core_sbr_frame_len_idx == 2 ? 2 :
                    usac->core_sbr_frame_len_idx == 3 ? 3 :
                    usac->core_sbr_frame_len_idx == 4 ? 1 :
                    0;

    if (sbr_ratio == 2) {
        ratio_mult = 8;
        ratio_dec = 3;
    } else if (sbr_ratio == 3) {
        ratio_mult = 2;
        ratio_dec = 1;
    } else if (sbr_ratio == 4) {
        ratio_mult = 4;
        ratio_dec = 1;
    } else {
        ratio_mult = 1;
        ratio_dec = 1;
    }

    ff_aac_output_configure(ac, ac->oc[1].layout_map, ac->oc[1].layout_map_tags,
                            ac->oc[1].status, 0);

    ac->avctx->profile = AV_PROFILE_AAC_USAC;

    indep_flag = get_bits1(gb);

    for (int i = 0; i < ac->oc[1].usac.nb_elems; i++) {
        int layout_id;
        int layout_type;
        AACUsacElemConfig *e = &ac->oc[1].usac.elems[i];
        ChannelElement *che;

        if (e->type == ID_USAC_SCE) {
            layout_id = elem_id[0]++;
            layout_type = TYPE_SCE;
            che = ff_aac_get_che(ac, TYPE_SCE, layout_id);
        } else if (e->type == ID_USAC_CPE) {
            layout_id = elem_id[1]++;
            layout_type = TYPE_CPE;
            che = ff_aac_get_che(ac, TYPE_CPE, layout_id);
        } else if (e->type == ID_USAC_LFE) {
            layout_id = elem_id[2]++;
            layout_type = TYPE_LFE;
            che = ff_aac_get_che(ac, TYPE_LFE, layout_id);
       }

       if (e->type != ID_USAC_EXT && !che) {
            av_log(ac->avctx, AV_LOG_ERROR,
                   "channel element %d.%d is not allocated\n",
                   layout_type, layout_id);
            return AVERROR_INVALIDDATA;
       }

        switch (e->type) {
        case ID_USAC_LFE:
            /* Fallthrough */
        case ID_USAC_SCE:
            ret = decode_usac_core_coder(ac, &ac->oc[1].usac, e, che, gb,
                                         indep_flag, 1);
            if (ret < 0)
                return ret;

            audio_found = 1;
            che->present = 1;
            break;
        case ID_USAC_CPE:
            ret = decode_usac_core_coder(ac, &ac->oc[1].usac, e, che, gb,
                                         indep_flag, 2);
            if (ret < 0)
                return ret;

            audio_found = 1;
            che->present = 1;
            break;
        case ID_USAC_EXT:
            ret = parse_ext_ele(ac, e, gb);
            if (ret < 0)
                return ret;
            break;
        }
    }

    if (audio_found)
        samples = ac->oc[1].m4ac.frame_length_short ? 768 : 1024;

    samples = (samples * ratio_mult) / ratio_dec;

    if (ac->oc[1].status && audio_found) {
        avctx->sample_rate = ac->oc[1].m4ac.ext_sample_rate;
        avctx->frame_size = samples;
        ac->oc[1].status = OC_LOCKED;
    }

    if (!frame->data[0] && samples) {
        av_log(avctx, AV_LOG_ERROR, "no frame data found\n");
        return AVERROR_INVALIDDATA;
    }

    if (samples) {
        frame->nb_samples = samples;
        frame->sample_rate = avctx->sample_rate;
        frame->flags = indep_flag ? AV_FRAME_FLAG_KEY : 0x0;
        *got_frame_ptr = 1;
    } else {
        av_frame_unref(ac->frame);
        frame->flags = indep_flag ? AV_FRAME_FLAG_KEY : 0x0;
        *got_frame_ptr = 0;
    }

    /* for dual-mono audio (SCE + SCE) */
    is_dmono = ac->dmono_mode && elem_id[0] == 2 &&
               !av_channel_layout_compare(&ac->oc[1].ch_layout,
                                          &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO);
    if (is_dmono) {
        if (ac->dmono_mode == 1)
            frame->data[1] = frame->data[0];
        else if (ac->dmono_mode == 2)
            frame->data[0] = frame->data[1];
    }

    return 0;
}
