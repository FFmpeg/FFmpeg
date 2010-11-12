/*
 * AAC decoder
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
 *
 * AAC LATM decoder
 * Copyright (c) 2008-2010 Paul Kendall <paul@kcbbs.gen.nz>
 * Copyright (c) 2010      Janne Grunau <janne-ffmpeg@jannau.net>
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
 * AAC decoder
 * @author Oded Shimon  ( ods15 ods15 dyndns org )
 * @author Maxim Gavrilov ( maxim.gavrilov gmail com )
 */

/*
 * supported tools
 *
 * Support?             Name
 * N (code in SoC repo) gain control
 * Y                    block switching
 * Y                    window shapes - standard
 * N                    window shapes - Low Delay
 * Y                    filterbank - standard
 * N (code in SoC repo) filterbank - Scalable Sample Rate
 * Y                    Temporal Noise Shaping
 * N (code in SoC repo) Long Term Prediction
 * Y                    intensity stereo
 * Y                    channel coupling
 * Y                    frequency domain prediction
 * Y                    Perceptual Noise Substitution
 * Y                    Mid/Side stereo
 * N                    Scalable Inverse AAC Quantization
 * N                    Frequency Selective Switch
 * N                    upsampling filter
 * Y                    quantization & coding - AAC
 * N                    quantization & coding - TwinVQ
 * N                    quantization & coding - BSAC
 * N                    AAC Error Resilience tools
 * N                    Error Resilience payload syntax
 * N                    Error Protection tool
 * N                    CELP
 * N                    Silence Compression
 * N                    HVXC
 * N                    HVXC 4kbits/s VR
 * N                    Structured Audio tools
 * N                    Structured Audio Sample Bank Format
 * N                    MIDI
 * N                    Harmonic and Individual Lines plus Noise
 * N                    Text-To-Speech Interface
 * Y                    Spectral Band Replication
 * Y (not in this code) Layer-1
 * Y (not in this code) Layer-2
 * Y (not in this code) Layer-3
 * N                    SinuSoidal Coding (Transient, Sinusoid, Noise)
 * Y                    Parametric Stereo
 * N                    Direct Stream Transfer
 *
 * Note: - HE AAC v1 comprises LC AAC with Spectral Band Replication.
 *       - HE AAC v2 comprises LC AAC with Spectral Band Replication and
           Parametric Stereo.
 */


#include "avcodec.h"
#include "internal.h"
#include "get_bits.h"
#include "dsputil.h"
#include "fft.h"
#include "lpc.h"

#include "aac.h"
#include "aactab.h"
#include "aacdectab.h"
#include "cbrt_tablegen.h"
#include "sbr.h"
#include "aacsbr.h"
#include "mpeg4audio.h"
#include "aacadtsdec.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <string.h>

#if ARCH_ARM
#   include "arm/aac.h"
#endif

union float754 {
    float f;
    uint32_t i;
};

static VLC vlc_scalefactors;
static VLC vlc_spectral[11];

static const char overread_err[] = "Input buffer exhausted before END element found\n";

static ChannelElement *get_che(AACContext *ac, int type, int elem_id)
{
    // For PCE based channel configurations map the channels solely based on tags.
    if (!ac->m4ac.chan_config) {
        return ac->tag_che_map[type][elem_id];
    }
    // For indexed channel configurations map the channels solely based on position.
    switch (ac->m4ac.chan_config) {
    case 7:
        if (ac->tags_mapped == 3 && type == TYPE_CPE) {
            ac->tags_mapped++;
            return ac->tag_che_map[TYPE_CPE][elem_id] = ac->che[TYPE_CPE][2];
        }
    case 6:
        /* Some streams incorrectly code 5.1 audio as SCE[0] CPE[0] CPE[1] SCE[1]
           instead of SCE[0] CPE[0] CPE[1] LFE[0]. If we seem to have
           encountered such a stream, transfer the LFE[0] element to the SCE[1]'s mapping */
        if (ac->tags_mapped == tags_per_config[ac->m4ac.chan_config] - 1 && (type == TYPE_LFE || type == TYPE_SCE)) {
            ac->tags_mapped++;
            return ac->tag_che_map[type][elem_id] = ac->che[TYPE_LFE][0];
        }
    case 5:
        if (ac->tags_mapped == 2 && type == TYPE_CPE) {
            ac->tags_mapped++;
            return ac->tag_che_map[TYPE_CPE][elem_id] = ac->che[TYPE_CPE][1];
        }
    case 4:
        if (ac->tags_mapped == 2 && ac->m4ac.chan_config == 4 && type == TYPE_SCE) {
            ac->tags_mapped++;
            return ac->tag_che_map[TYPE_SCE][elem_id] = ac->che[TYPE_SCE][1];
        }
    case 3:
    case 2:
        if (ac->tags_mapped == (ac->m4ac.chan_config != 2) && type == TYPE_CPE) {
            ac->tags_mapped++;
            return ac->tag_che_map[TYPE_CPE][elem_id] = ac->che[TYPE_CPE][0];
        } else if (ac->m4ac.chan_config == 2) {
            return NULL;
        }
    case 1:
        if (!ac->tags_mapped && type == TYPE_SCE) {
            ac->tags_mapped++;
            return ac->tag_che_map[TYPE_SCE][elem_id] = ac->che[TYPE_SCE][0];
        }
    default:
        return NULL;
    }
}

/**
 * Check for the channel element in the current channel position configuration.
 * If it exists, make sure the appropriate element is allocated and map the
 * channel order to match the internal FFmpeg channel layout.
 *
 * @param   che_pos current channel position configuration
 * @param   type channel element type
 * @param   id channel element id
 * @param   channels count of the number of channels in the configuration
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static av_cold int che_configure(AACContext *ac,
                         enum ChannelPosition che_pos[4][MAX_ELEM_ID],
                         int type, int id,
                         int *channels)
{
    if (che_pos[type][id]) {
        if (!ac->che[type][id] && !(ac->che[type][id] = av_mallocz(sizeof(ChannelElement))))
            return AVERROR(ENOMEM);
        ff_aac_sbr_ctx_init(&ac->che[type][id]->sbr);
        if (type != TYPE_CCE) {
            ac->output_data[(*channels)++] = ac->che[type][id]->ch[0].ret;
            if (type == TYPE_CPE ||
                (type == TYPE_SCE && ac->m4ac.ps == 1)) {
                ac->output_data[(*channels)++] = ac->che[type][id]->ch[1].ret;
            }
        }
    } else {
        if (ac->che[type][id])
            ff_aac_sbr_ctx_close(&ac->che[type][id]->sbr);
        av_freep(&ac->che[type][id]);
    }
    return 0;
}

/**
 * Configure output channel order based on the current program configuration element.
 *
 * @param   che_pos current channel position configuration
 * @param   new_che_pos New channel position configuration - we only do something if it differs from the current one.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static av_cold int output_configure(AACContext *ac,
                            enum ChannelPosition che_pos[4][MAX_ELEM_ID],
                            enum ChannelPosition new_che_pos[4][MAX_ELEM_ID],
                            int channel_config, enum OCStatus oc_type)
{
    AVCodecContext *avctx = ac->avctx;
    int i, type, channels = 0, ret;

    if (new_che_pos != che_pos)
    memcpy(che_pos, new_che_pos, 4 * MAX_ELEM_ID * sizeof(new_che_pos[0][0]));

    if (channel_config) {
        for (i = 0; i < tags_per_config[channel_config]; i++) {
            if ((ret = che_configure(ac, che_pos,
                                     aac_channel_layout_map[channel_config - 1][i][0],
                                     aac_channel_layout_map[channel_config - 1][i][1],
                                     &channels)))
                return ret;
        }

        memset(ac->tag_che_map, 0,       4 * MAX_ELEM_ID * sizeof(ac->che[0][0]));

        avctx->channel_layout = aac_channel_layout[channel_config - 1];
    } else {
        /* Allocate or free elements depending on if they are in the
         * current program configuration.
         *
         * Set up default 1:1 output mapping.
         *
         * For a 5.1 stream the output order will be:
         *    [ Center ] [ Front Left ] [ Front Right ] [ LFE ] [ Surround Left ] [ Surround Right ]
         */

        for (i = 0; i < MAX_ELEM_ID; i++) {
            for (type = 0; type < 4; type++) {
                if ((ret = che_configure(ac, che_pos, type, i, &channels)))
                    return ret;
            }
        }

        memcpy(ac->tag_che_map, ac->che, 4 * MAX_ELEM_ID * sizeof(ac->che[0][0]));

        avctx->channel_layout = 0;
    }

    avctx->channels = channels;

    ac->output_configured = oc_type;

    return 0;
}

/**
 * Decode an array of 4 bit element IDs, optionally interleaved with a stereo/mono switching bit.
 *
 * @param cpe_map Stereo (Channel Pair Element) map, NULL if stereo bit is not present.
 * @param sce_map mono (Single Channel Element) map
 * @param type speaker type/position for these channels
 */
static void decode_channel_map(enum ChannelPosition *cpe_map,
                               enum ChannelPosition *sce_map,
                               enum ChannelPosition type,
                               GetBitContext *gb, int n)
{
    while (n--) {
        enum ChannelPosition *map = cpe_map && get_bits1(gb) ? cpe_map : sce_map; // stereo or mono map
        map[get_bits(gb, 4)] = type;
    }
}

/**
 * Decode program configuration element; reference: table 4.2.
 *
 * @param   new_che_pos New channel position configuration - we only do something if it differs from the current one.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_pce(AVCodecContext *avctx, MPEG4AudioConfig *m4ac,
                      enum ChannelPosition new_che_pos[4][MAX_ELEM_ID],
                      GetBitContext *gb)
{
    int num_front, num_side, num_back, num_lfe, num_assoc_data, num_cc, sampling_index;
    int comment_len;

    skip_bits(gb, 2);  // object_type

    sampling_index = get_bits(gb, 4);
    if (m4ac->sampling_index != sampling_index)
        av_log(avctx, AV_LOG_WARNING, "Sample rate index in program config element does not match the sample rate index configured by the container.\n");

    num_front       = get_bits(gb, 4);
    num_side        = get_bits(gb, 4);
    num_back        = get_bits(gb, 4);
    num_lfe         = get_bits(gb, 2);
    num_assoc_data  = get_bits(gb, 3);
    num_cc          = get_bits(gb, 4);

    if (get_bits1(gb))
        skip_bits(gb, 4); // mono_mixdown_tag
    if (get_bits1(gb))
        skip_bits(gb, 4); // stereo_mixdown_tag

    if (get_bits1(gb))
        skip_bits(gb, 3); // mixdown_coeff_index and pseudo_surround

    decode_channel_map(new_che_pos[TYPE_CPE], new_che_pos[TYPE_SCE], AAC_CHANNEL_FRONT, gb, num_front);
    decode_channel_map(new_che_pos[TYPE_CPE], new_che_pos[TYPE_SCE], AAC_CHANNEL_SIDE,  gb, num_side );
    decode_channel_map(new_che_pos[TYPE_CPE], new_che_pos[TYPE_SCE], AAC_CHANNEL_BACK,  gb, num_back );
    decode_channel_map(NULL,                  new_che_pos[TYPE_LFE], AAC_CHANNEL_LFE,   gb, num_lfe  );

    skip_bits_long(gb, 4 * num_assoc_data);

    decode_channel_map(new_che_pos[TYPE_CCE], new_che_pos[TYPE_CCE], AAC_CHANNEL_CC,    gb, num_cc   );

    align_get_bits(gb);

    /* comment field, first byte is length */
    comment_len = get_bits(gb, 8) * 8;
    if (get_bits_left(gb) < comment_len) {
        av_log(avctx, AV_LOG_ERROR, overread_err);
        return -1;
    }
    skip_bits_long(gb, comment_len);
    return 0;
}

/**
 * Set up channel positions based on a default channel configuration
 * as specified in table 1.17.
 *
 * @param   new_che_pos New channel position configuration - we only do something if it differs from the current one.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static av_cold int set_default_channel_config(AVCodecContext *avctx,
                                      enum ChannelPosition new_che_pos[4][MAX_ELEM_ID],
                                      int channel_config)
{
    if (channel_config < 1 || channel_config > 7) {
        av_log(avctx, AV_LOG_ERROR, "invalid default channel configuration (%d)\n",
               channel_config);
        return -1;
    }

    /* default channel configurations:
     *
     * 1ch : front center (mono)
     * 2ch : L + R (stereo)
     * 3ch : front center + L + R
     * 4ch : front center + L + R + back center
     * 5ch : front center + L + R + back stereo
     * 6ch : front center + L + R + back stereo + LFE
     * 7ch : front center + L + R + outer front left + outer front right + back stereo + LFE
     */

    if (channel_config != 2)
        new_che_pos[TYPE_SCE][0] = AAC_CHANNEL_FRONT; // front center (or mono)
    if (channel_config > 1)
        new_che_pos[TYPE_CPE][0] = AAC_CHANNEL_FRONT; // L + R (or stereo)
    if (channel_config == 4)
        new_che_pos[TYPE_SCE][1] = AAC_CHANNEL_BACK;  // back center
    if (channel_config > 4)
        new_che_pos[TYPE_CPE][(channel_config == 7) + 1]
        = AAC_CHANNEL_BACK;  // back stereo
    if (channel_config > 5)
        new_che_pos[TYPE_LFE][0] = AAC_CHANNEL_LFE;   // LFE
    if (channel_config == 7)
        new_che_pos[TYPE_CPE][1] = AAC_CHANNEL_FRONT; // outer front left + outer front right

    return 0;
}

/**
 * Decode GA "General Audio" specific configuration; reference: table 4.1.
 *
 * @param   ac          pointer to AACContext, may be null
 * @param   avctx       pointer to AVCCodecContext, used for logging
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_ga_specific_config(AACContext *ac, AVCodecContext *avctx,
                                     GetBitContext *gb,
                                     MPEG4AudioConfig *m4ac,
                                     int channel_config)
{
    enum ChannelPosition new_che_pos[4][MAX_ELEM_ID];
    int extension_flag, ret;

    if (get_bits1(gb)) { // frameLengthFlag
        av_log_missing_feature(avctx, "960/120 MDCT window is", 1);
        return -1;
    }

    if (get_bits1(gb))       // dependsOnCoreCoder
        skip_bits(gb, 14);   // coreCoderDelay
    extension_flag = get_bits1(gb);

    if (m4ac->object_type == AOT_AAC_SCALABLE ||
        m4ac->object_type == AOT_ER_AAC_SCALABLE)
        skip_bits(gb, 3);     // layerNr

    memset(new_che_pos, 0, 4 * MAX_ELEM_ID * sizeof(new_che_pos[0][0]));
    if (channel_config == 0) {
        skip_bits(gb, 4);  // element_instance_tag
        if ((ret = decode_pce(avctx, m4ac, new_che_pos, gb)))
            return ret;
    } else {
        if ((ret = set_default_channel_config(avctx, new_che_pos, channel_config)))
            return ret;
    }
    if (ac && (ret = output_configure(ac, ac->che_pos, new_che_pos, channel_config, OC_GLOBAL_HDR)))
        return ret;

    if (extension_flag) {
        switch (m4ac->object_type) {
        case AOT_ER_BSAC:
            skip_bits(gb, 5);    // numOfSubFrame
            skip_bits(gb, 11);   // layer_length
            break;
        case AOT_ER_AAC_LC:
        case AOT_ER_AAC_LTP:
        case AOT_ER_AAC_SCALABLE:
        case AOT_ER_AAC_LD:
            skip_bits(gb, 3);  /* aacSectionDataResilienceFlag
                                    * aacScalefactorDataResilienceFlag
                                    * aacSpectralDataResilienceFlag
                                    */
            break;
        }
        skip_bits1(gb);    // extensionFlag3 (TBD in version 3)
    }
    return 0;
}

/**
 * Decode audio specific configuration; reference: table 1.13.
 *
 * @param   ac          pointer to AACContext, may be null
 * @param   avctx       pointer to AVCCodecContext, used for logging
 * @param   m4ac        pointer to MPEG4AudioConfig, used for parsing
 * @param   data        pointer to AVCodecContext extradata
 * @param   data_size   size of AVCCodecContext extradata
 *
 * @return  Returns error status or number of consumed bits. <0 - error
 */
static int decode_audio_specific_config(AACContext *ac,
                                        AVCodecContext *avctx,
                                        MPEG4AudioConfig *m4ac,
                                        const uint8_t *data, int data_size)
{
    GetBitContext gb;
    int i;

    init_get_bits(&gb, data, data_size * 8);

    if ((i = ff_mpeg4audio_get_config(m4ac, data, data_size)) < 0)
        return -1;
    if (m4ac->sampling_index > 12) {
        av_log(avctx, AV_LOG_ERROR, "invalid sampling rate index %d\n", m4ac->sampling_index);
        return -1;
    }
    if (m4ac->sbr == 1 && m4ac->ps == -1)
        m4ac->ps = 1;

    skip_bits_long(&gb, i);

    switch (m4ac->object_type) {
    case AOT_AAC_MAIN:
    case AOT_AAC_LC:
        if (decode_ga_specific_config(ac, avctx, &gb, m4ac, m4ac->chan_config))
            return -1;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Audio object type %s%d is not supported.\n",
               m4ac->sbr == 1? "SBR+" : "", m4ac->object_type);
        return -1;
    }

    return get_bits_count(&gb);
}

/**
 * linear congruential pseudorandom number generator
 *
 * @param   previous_val    pointer to the current state of the generator
 *
 * @return  Returns a 32-bit pseudorandom integer
 */
static av_always_inline int lcg_random(int previous_val)
{
    return previous_val * 1664525 + 1013904223;
}

static av_always_inline void reset_predict_state(PredictorState *ps)
{
    ps->r0   = 0.0f;
    ps->r1   = 0.0f;
    ps->cor0 = 0.0f;
    ps->cor1 = 0.0f;
    ps->var0 = 1.0f;
    ps->var1 = 1.0f;
}

static void reset_all_predictors(PredictorState *ps)
{
    int i;
    for (i = 0; i < MAX_PREDICTORS; i++)
        reset_predict_state(&ps[i]);
}

static void reset_predictor_group(PredictorState *ps, int group_num)
{
    int i;
    for (i = group_num - 1; i < MAX_PREDICTORS; i += 30)
        reset_predict_state(&ps[i]);
}

#define AAC_INIT_VLC_STATIC(num, size) \
    INIT_VLC_STATIC(&vlc_spectral[num], 8, ff_aac_spectral_sizes[num], \
         ff_aac_spectral_bits[num], sizeof( ff_aac_spectral_bits[num][0]), sizeof( ff_aac_spectral_bits[num][0]), \
        ff_aac_spectral_codes[num], sizeof(ff_aac_spectral_codes[num][0]), sizeof(ff_aac_spectral_codes[num][0]), \
        size);

static av_cold int aac_decode_init(AVCodecContext *avctx)
{
    AACContext *ac = avctx->priv_data;

    ac->avctx = avctx;
    ac->m4ac.sample_rate = avctx->sample_rate;

    if (avctx->extradata_size > 0) {
        if (decode_audio_specific_config(ac, ac->avctx, &ac->m4ac,
                                         avctx->extradata,
                                         avctx->extradata_size) < 0)
            return -1;
    }

    avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    AAC_INIT_VLC_STATIC( 0, 304);
    AAC_INIT_VLC_STATIC( 1, 270);
    AAC_INIT_VLC_STATIC( 2, 550);
    AAC_INIT_VLC_STATIC( 3, 300);
    AAC_INIT_VLC_STATIC( 4, 328);
    AAC_INIT_VLC_STATIC( 5, 294);
    AAC_INIT_VLC_STATIC( 6, 306);
    AAC_INIT_VLC_STATIC( 7, 268);
    AAC_INIT_VLC_STATIC( 8, 510);
    AAC_INIT_VLC_STATIC( 9, 366);
    AAC_INIT_VLC_STATIC(10, 462);

    ff_aac_sbr_init();

    dsputil_init(&ac->dsp, avctx);

    ac->random_state = 0x1f2e3d4c;

    // -1024 - Compensate wrong IMDCT method.
    // 32768 - Required to scale values to the correct range for the bias method
    //         for float to int16 conversion.

    if (ac->dsp.float_to_int16_interleave == ff_float_to_int16_interleave_c) {
        ac->add_bias  = 385.0f;
        ac->sf_scale  = 1. / (-1024. * 32768.);
        ac->sf_offset = 0;
    } else {
        ac->add_bias  = 0.0f;
        ac->sf_scale  = 1. / -1024.;
        ac->sf_offset = 60;
    }

    ff_aac_tableinit();

    INIT_VLC_STATIC(&vlc_scalefactors,7,FF_ARRAY_ELEMS(ff_aac_scalefactor_code),
                    ff_aac_scalefactor_bits, sizeof(ff_aac_scalefactor_bits[0]), sizeof(ff_aac_scalefactor_bits[0]),
                    ff_aac_scalefactor_code, sizeof(ff_aac_scalefactor_code[0]), sizeof(ff_aac_scalefactor_code[0]),
                    352);

    ff_mdct_init(&ac->mdct, 11, 1, 1.0);
    ff_mdct_init(&ac->mdct_small, 8, 1, 1.0);
    // window initialization
    ff_kbd_window_init(ff_aac_kbd_long_1024, 4.0, 1024);
    ff_kbd_window_init(ff_aac_kbd_short_128, 6.0, 128);
    ff_init_ff_sine_windows(10);
    ff_init_ff_sine_windows( 7);

    cbrt_tableinit();

    return 0;
}

/**
 * Skip data_stream_element; reference: table 4.10.
 */
static int skip_data_stream_element(AACContext *ac, GetBitContext *gb)
{
    int byte_align = get_bits1(gb);
    int count = get_bits(gb, 8);
    if (count == 255)
        count += get_bits(gb, 8);
    if (byte_align)
        align_get_bits(gb);

    if (get_bits_left(gb) < 8 * count) {
        av_log(ac->avctx, AV_LOG_ERROR, overread_err);
        return -1;
    }
    skip_bits_long(gb, 8 * count);
    return 0;
}

static int decode_prediction(AACContext *ac, IndividualChannelStream *ics,
                             GetBitContext *gb)
{
    int sfb;
    if (get_bits1(gb)) {
        ics->predictor_reset_group = get_bits(gb, 5);
        if (ics->predictor_reset_group == 0 || ics->predictor_reset_group > 30) {
            av_log(ac->avctx, AV_LOG_ERROR, "Invalid Predictor Reset Group.\n");
            return -1;
        }
    }
    for (sfb = 0; sfb < FFMIN(ics->max_sfb, ff_aac_pred_sfb_max[ac->m4ac.sampling_index]); sfb++) {
        ics->prediction_used[sfb] = get_bits1(gb);
    }
    return 0;
}

/**
 * Decode Individual Channel Stream info; reference: table 4.6.
 *
 * @param   common_window   Channels have independent [0], or shared [1], Individual Channel Stream information.
 */
static int decode_ics_info(AACContext *ac, IndividualChannelStream *ics,
                           GetBitContext *gb, int common_window)
{
    if (get_bits1(gb)) {
        av_log(ac->avctx, AV_LOG_ERROR, "Reserved bit set.\n");
        memset(ics, 0, sizeof(IndividualChannelStream));
        return -1;
    }
    ics->window_sequence[1] = ics->window_sequence[0];
    ics->window_sequence[0] = get_bits(gb, 2);
    ics->use_kb_window[1]   = ics->use_kb_window[0];
    ics->use_kb_window[0]   = get_bits1(gb);
    ics->num_window_groups  = 1;
    ics->group_len[0]       = 1;
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        int i;
        ics->max_sfb = get_bits(gb, 4);
        for (i = 0; i < 7; i++) {
            if (get_bits1(gb)) {
                ics->group_len[ics->num_window_groups - 1]++;
            } else {
                ics->num_window_groups++;
                ics->group_len[ics->num_window_groups - 1] = 1;
            }
        }
        ics->num_windows       = 8;
        ics->swb_offset        =    ff_swb_offset_128[ac->m4ac.sampling_index];
        ics->num_swb           =   ff_aac_num_swb_128[ac->m4ac.sampling_index];
        ics->tns_max_bands     = ff_tns_max_bands_128[ac->m4ac.sampling_index];
        ics->predictor_present = 0;
    } else {
        ics->max_sfb               = get_bits(gb, 6);
        ics->num_windows           = 1;
        ics->swb_offset            =    ff_swb_offset_1024[ac->m4ac.sampling_index];
        ics->num_swb               =   ff_aac_num_swb_1024[ac->m4ac.sampling_index];
        ics->tns_max_bands         = ff_tns_max_bands_1024[ac->m4ac.sampling_index];
        ics->predictor_present     = get_bits1(gb);
        ics->predictor_reset_group = 0;
        if (ics->predictor_present) {
            if (ac->m4ac.object_type == AOT_AAC_MAIN) {
                if (decode_prediction(ac, ics, gb)) {
                    memset(ics, 0, sizeof(IndividualChannelStream));
                    return -1;
                }
            } else if (ac->m4ac.object_type == AOT_AAC_LC) {
                av_log(ac->avctx, AV_LOG_ERROR, "Prediction is not allowed in AAC-LC.\n");
                memset(ics, 0, sizeof(IndividualChannelStream));
                return -1;
            } else {
                av_log_missing_feature(ac->avctx, "Predictor bit set but LTP is", 1);
                memset(ics, 0, sizeof(IndividualChannelStream));
                return -1;
            }
        }
    }

    if (ics->max_sfb > ics->num_swb) {
        av_log(ac->avctx, AV_LOG_ERROR,
               "Number of scalefactor bands in group (%d) exceeds limit (%d).\n",
               ics->max_sfb, ics->num_swb);
        memset(ics, 0, sizeof(IndividualChannelStream));
        return -1;
    }

    return 0;
}

/**
 * Decode band types (section_data payload); reference: table 4.46.
 *
 * @param   band_type           array of the used band type
 * @param   band_type_run_end   array of the last scalefactor band of a band type run
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_band_types(AACContext *ac, enum BandType band_type[120],
                             int band_type_run_end[120], GetBitContext *gb,
                             IndividualChannelStream *ics)
{
    int g, idx = 0;
    const int bits = (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) ? 3 : 5;
    for (g = 0; g < ics->num_window_groups; g++) {
        int k = 0;
        while (k < ics->max_sfb) {
            uint8_t sect_end = k;
            int sect_len_incr;
            int sect_band_type = get_bits(gb, 4);
            if (sect_band_type == 12) {
                av_log(ac->avctx, AV_LOG_ERROR, "invalid band type\n");
                return -1;
            }
            while ((sect_len_incr = get_bits(gb, bits)) == (1 << bits) - 1)
                sect_end += sect_len_incr;
            sect_end += sect_len_incr;
            if (get_bits_left(gb) < 0) {
                av_log(ac->avctx, AV_LOG_ERROR, overread_err);
                return -1;
            }
            if (sect_end > ics->max_sfb) {
                av_log(ac->avctx, AV_LOG_ERROR,
                       "Number of bands (%d) exceeds limit (%d).\n",
                       sect_end, ics->max_sfb);
                return -1;
            }
            for (; k < sect_end; k++) {
                band_type        [idx]   = sect_band_type;
                band_type_run_end[idx++] = sect_end;
            }
        }
    }
    return 0;
}

/**
 * Decode scalefactors; reference: table 4.47.
 *
 * @param   global_gain         first scalefactor value as scalefactors are differentially coded
 * @param   band_type           array of the used band type
 * @param   band_type_run_end   array of the last scalefactor band of a band type run
 * @param   sf                  array of scalefactors or intensity stereo positions
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_scalefactors(AACContext *ac, float sf[120], GetBitContext *gb,
                               unsigned int global_gain,
                               IndividualChannelStream *ics,
                               enum BandType band_type[120],
                               int band_type_run_end[120])
{
    const int sf_offset = ac->sf_offset + (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE ? 12 : 0);
    int g, i, idx = 0;
    int offset[3] = { global_gain, global_gain - 90, 100 };
    int noise_flag = 1;
    static const char *sf_str[3] = { "Global gain", "Noise gain", "Intensity stereo position" };
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb;) {
            int run_end = band_type_run_end[idx];
            if (band_type[idx] == ZERO_BT) {
                for (; i < run_end; i++, idx++)
                    sf[idx] = 0.;
            } else if ((band_type[idx] == INTENSITY_BT) || (band_type[idx] == INTENSITY_BT2)) {
                for (; i < run_end; i++, idx++) {
                    offset[2] += get_vlc2(gb, vlc_scalefactors.table, 7, 3) - 60;
                    if (offset[2] > 255U) {
                        av_log(ac->avctx, AV_LOG_ERROR,
                               "%s (%d) out of range.\n", sf_str[2], offset[2]);
                        return -1;
                    }
                    sf[idx] = ff_aac_pow2sf_tab[-offset[2] + 300];
                }
            } else if (band_type[idx] == NOISE_BT) {
                for (; i < run_end; i++, idx++) {
                    if (noise_flag-- > 0)
                        offset[1] += get_bits(gb, 9) - 256;
                    else
                        offset[1] += get_vlc2(gb, vlc_scalefactors.table, 7, 3) - 60;
                    if (offset[1] > 255U) {
                        av_log(ac->avctx, AV_LOG_ERROR,
                               "%s (%d) out of range.\n", sf_str[1], offset[1]);
                        return -1;
                    }
                    sf[idx] = -ff_aac_pow2sf_tab[offset[1] + sf_offset + 100];
                }
            } else {
                for (; i < run_end; i++, idx++) {
                    offset[0] += get_vlc2(gb, vlc_scalefactors.table, 7, 3) - 60;
                    if (offset[0] > 255U) {
                        av_log(ac->avctx, AV_LOG_ERROR,
                               "%s (%d) out of range.\n", sf_str[0], offset[0]);
                        return -1;
                    }
                    sf[idx] = -ff_aac_pow2sf_tab[ offset[0] + sf_offset];
                }
            }
        }
    }
    return 0;
}

/**
 * Decode pulse data; reference: table 4.7.
 */
static int decode_pulses(Pulse *pulse, GetBitContext *gb,
                         const uint16_t *swb_offset, int num_swb)
{
    int i, pulse_swb;
    pulse->num_pulse = get_bits(gb, 2) + 1;
    pulse_swb        = get_bits(gb, 6);
    if (pulse_swb >= num_swb)
        return -1;
    pulse->pos[0]    = swb_offset[pulse_swb];
    pulse->pos[0]   += get_bits(gb, 5);
    if (pulse->pos[0] > 1023)
        return -1;
    pulse->amp[0]    = get_bits(gb, 4);
    for (i = 1; i < pulse->num_pulse; i++) {
        pulse->pos[i] = get_bits(gb, 5) + pulse->pos[i - 1];
        if (pulse->pos[i] > 1023)
            return -1;
        pulse->amp[i] = get_bits(gb, 4);
    }
    return 0;
}

/**
 * Decode Temporal Noise Shaping data; reference: table 4.48.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_tns(AACContext *ac, TemporalNoiseShaping *tns,
                      GetBitContext *gb, const IndividualChannelStream *ics)
{
    int w, filt, i, coef_len, coef_res, coef_compress;
    const int is8 = ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE;
    const int tns_max_order = is8 ? 7 : ac->m4ac.object_type == AOT_AAC_MAIN ? 20 : 12;
    for (w = 0; w < ics->num_windows; w++) {
        if ((tns->n_filt[w] = get_bits(gb, 2 - is8))) {
            coef_res = get_bits1(gb);

            for (filt = 0; filt < tns->n_filt[w]; filt++) {
                int tmp2_idx;
                tns->length[w][filt] = get_bits(gb, 6 - 2 * is8);

                if ((tns->order[w][filt] = get_bits(gb, 5 - 2 * is8)) > tns_max_order) {
                    av_log(ac->avctx, AV_LOG_ERROR, "TNS filter order %d is greater than maximum %d.\n",
                           tns->order[w][filt], tns_max_order);
                    tns->order[w][filt] = 0;
                    return -1;
                }
                if (tns->order[w][filt]) {
                    tns->direction[w][filt] = get_bits1(gb);
                    coef_compress = get_bits1(gb);
                    coef_len = coef_res + 3 - coef_compress;
                    tmp2_idx = 2 * coef_compress + coef_res;

                    for (i = 0; i < tns->order[w][filt]; i++)
                        tns->coef[w][filt][i] = tns_tmp2_map[tmp2_idx][get_bits(gb, coef_len)];
                }
            }
        }
    }
    return 0;
}

/**
 * Decode Mid/Side data; reference: table 4.54.
 *
 * @param   ms_present  Indicates mid/side stereo presence. [0] mask is all 0s;
 *                      [1] mask is decoded from bitstream; [2] mask is all 1s;
 *                      [3] reserved for scalable AAC
 */
static void decode_mid_side_stereo(ChannelElement *cpe, GetBitContext *gb,
                                   int ms_present)
{
    int idx;
    if (ms_present == 1) {
        for (idx = 0; idx < cpe->ch[0].ics.num_window_groups * cpe->ch[0].ics.max_sfb; idx++)
            cpe->ms_mask[idx] = get_bits1(gb);
    } else if (ms_present == 2) {
        memset(cpe->ms_mask, 1, cpe->ch[0].ics.num_window_groups * cpe->ch[0].ics.max_sfb * sizeof(cpe->ms_mask[0]));
    }
}

#ifndef VMUL2
static inline float *VMUL2(float *dst, const float *v, unsigned idx,
                           const float *scale)
{
    float s = *scale;
    *dst++ = v[idx    & 15] * s;
    *dst++ = v[idx>>4 & 15] * s;
    return dst;
}
#endif

#ifndef VMUL4
static inline float *VMUL4(float *dst, const float *v, unsigned idx,
                           const float *scale)
{
    float s = *scale;
    *dst++ = v[idx    & 3] * s;
    *dst++ = v[idx>>2 & 3] * s;
    *dst++ = v[idx>>4 & 3] * s;
    *dst++ = v[idx>>6 & 3] * s;
    return dst;
}
#endif

#ifndef VMUL2S
static inline float *VMUL2S(float *dst, const float *v, unsigned idx,
                            unsigned sign, const float *scale)
{
    union float754 s0, s1;

    s0.f = s1.f = *scale;
    s0.i ^= sign >> 1 << 31;
    s1.i ^= sign      << 31;

    *dst++ = v[idx    & 15] * s0.f;
    *dst++ = v[idx>>4 & 15] * s1.f;

    return dst;
}
#endif

#ifndef VMUL4S
static inline float *VMUL4S(float *dst, const float *v, unsigned idx,
                            unsigned sign, const float *scale)
{
    unsigned nz = idx >> 12;
    union float754 s = { .f = *scale };
    union float754 t;

    t.i = s.i ^ (sign & 1<<31);
    *dst++ = v[idx    & 3] * t.f;

    sign <<= nz & 1; nz >>= 1;
    t.i = s.i ^ (sign & 1<<31);
    *dst++ = v[idx>>2 & 3] * t.f;

    sign <<= nz & 1; nz >>= 1;
    t.i = s.i ^ (sign & 1<<31);
    *dst++ = v[idx>>4 & 3] * t.f;

    sign <<= nz & 1; nz >>= 1;
    t.i = s.i ^ (sign & 1<<31);
    *dst++ = v[idx>>6 & 3] * t.f;

    return dst;
}
#endif

/**
 * Decode spectral data; reference: table 4.50.
 * Dequantize and scale spectral data; reference: 4.6.3.3.
 *
 * @param   coef            array of dequantized, scaled spectral data
 * @param   sf              array of scalefactors or intensity stereo positions
 * @param   pulse_present   set if pulses are present
 * @param   pulse           pointer to pulse data struct
 * @param   band_type       array of the used band type
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_spectrum_and_dequant(AACContext *ac, float coef[1024],
                                       GetBitContext *gb, const float sf[120],
                                       int pulse_present, const Pulse *pulse,
                                       const IndividualChannelStream *ics,
                                       enum BandType band_type[120])
{
    int i, k, g, idx = 0;
    const int c = 1024 / ics->num_windows;
    const uint16_t *offsets = ics->swb_offset;
    float *coef_base = coef;

    for (g = 0; g < ics->num_windows; g++)
        memset(coef + g * 128 + offsets[ics->max_sfb], 0, sizeof(float) * (c - offsets[ics->max_sfb]));

    for (g = 0; g < ics->num_window_groups; g++) {
        unsigned g_len = ics->group_len[g];

        for (i = 0; i < ics->max_sfb; i++, idx++) {
            const unsigned cbt_m1 = band_type[idx] - 1;
            float *cfo = coef + offsets[i];
            int off_len = offsets[i + 1] - offsets[i];
            int group;

            if (cbt_m1 >= INTENSITY_BT2 - 1) {
                for (group = 0; group < g_len; group++, cfo+=128) {
                    memset(cfo, 0, off_len * sizeof(float));
                }
            } else if (cbt_m1 == NOISE_BT - 1) {
                for (group = 0; group < g_len; group++, cfo+=128) {
                    float scale;
                    float band_energy;

                    for (k = 0; k < off_len; k++) {
                        ac->random_state  = lcg_random(ac->random_state);
                        cfo[k] = ac->random_state;
                    }

                    band_energy = ac->dsp.scalarproduct_float(cfo, cfo, off_len);
                    scale = sf[idx] / sqrtf(band_energy);
                    ac->dsp.vector_fmul_scalar(cfo, cfo, scale, off_len);
                }
            } else {
                const float *vq = ff_aac_codebook_vector_vals[cbt_m1];
                const uint16_t *cb_vector_idx = ff_aac_codebook_vector_idx[cbt_m1];
                VLC_TYPE (*vlc_tab)[2] = vlc_spectral[cbt_m1].table;
                OPEN_READER(re, gb);

                switch (cbt_m1 >> 1) {
                case 0:
                    for (group = 0; group < g_len; group++, cfo+=128) {
                        float *cf = cfo;
                        int len = off_len;

                        do {
                            int code;
                            unsigned cb_idx;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = cb_vector_idx[code];
                            cf = VMUL4(cf, vq, cb_idx, sf + idx);
                        } while (len -= 4);
                    }
                    break;

                case 1:
                    for (group = 0; group < g_len; group++, cfo+=128) {
                        float *cf = cfo;
                        int len = off_len;

                        do {
                            int code;
                            unsigned nnz;
                            unsigned cb_idx;
                            uint32_t bits;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
#if MIN_CACHE_BITS < 20
                            UPDATE_CACHE(re, gb);
#endif
                            cb_idx = cb_vector_idx[code];
                            nnz = cb_idx >> 8 & 15;
                            bits = SHOW_UBITS(re, gb, nnz) << (32-nnz);
                            LAST_SKIP_BITS(re, gb, nnz);
                            cf = VMUL4S(cf, vq, cb_idx, bits, sf + idx);
                        } while (len -= 4);
                    }
                    break;

                case 2:
                    for (group = 0; group < g_len; group++, cfo+=128) {
                        float *cf = cfo;
                        int len = off_len;

                        do {
                            int code;
                            unsigned cb_idx;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = cb_vector_idx[code];
                            cf = VMUL2(cf, vq, cb_idx, sf + idx);
                        } while (len -= 2);
                    }
                    break;

                case 3:
                case 4:
                    for (group = 0; group < g_len; group++, cfo+=128) {
                        float *cf = cfo;
                        int len = off_len;

                        do {
                            int code;
                            unsigned nnz;
                            unsigned cb_idx;
                            unsigned sign;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);
                            cb_idx = cb_vector_idx[code];
                            nnz = cb_idx >> 8 & 15;
                            sign = SHOW_UBITS(re, gb, nnz) << (cb_idx >> 12);
                            LAST_SKIP_BITS(re, gb, nnz);
                            cf = VMUL2S(cf, vq, cb_idx, sign, sf + idx);
                        } while (len -= 2);
                    }
                    break;

                default:
                    for (group = 0; group < g_len; group++, cfo+=128) {
                        float *cf = cfo;
                        uint32_t *icf = (uint32_t *) cf;
                        int len = off_len;

                        do {
                            int code;
                            unsigned nzt, nnz;
                            unsigned cb_idx;
                            uint32_t bits;
                            int j;

                            UPDATE_CACHE(re, gb);
                            GET_VLC(code, re, gb, vlc_tab, 8, 2);

                            if (!code) {
                                *icf++ = 0;
                                *icf++ = 0;
                                continue;
                            }

                            cb_idx = cb_vector_idx[code];
                            nnz = cb_idx >> 12;
                            nzt = cb_idx >> 8;
                            bits = SHOW_UBITS(re, gb, nnz) << (32-nnz);
                            LAST_SKIP_BITS(re, gb, nnz);

                            for (j = 0; j < 2; j++) {
                                if (nzt & 1<<j) {
                                    uint32_t b;
                                    int n;
                                    /* The total length of escape_sequence must be < 22 bits according
                                       to the specification (i.e. max is 111111110xxxxxxxxxxxx). */
                                    UPDATE_CACHE(re, gb);
                                    b = GET_CACHE(re, gb);
                                    b = 31 - av_log2(~b);

                                    if (b > 8) {
                                        av_log(ac->avctx, AV_LOG_ERROR, "error in spectral data, ESC overflow\n");
                                        return -1;
                                    }

#if MIN_CACHE_BITS < 21
                                    LAST_SKIP_BITS(re, gb, b + 1);
                                    UPDATE_CACHE(re, gb);
#else
                                    SKIP_BITS(re, gb, b + 1);
#endif
                                    b += 4;
                                    n = (1 << b) + SHOW_UBITS(re, gb, b);
                                    LAST_SKIP_BITS(re, gb, b);
                                    *icf++ = cbrt_tab[n] | (bits & 1<<31);
                                    bits <<= 1;
                                } else {
                                    unsigned v = ((const uint32_t*)vq)[cb_idx & 15];
                                    *icf++ = (bits & 1<<31) | v;
                                    bits <<= !!v;
                                }
                                cb_idx >>= 4;
                            }
                        } while (len -= 2);

                        ac->dsp.vector_fmul_scalar(cfo, cfo, sf[idx], off_len);
                    }
                }

                CLOSE_READER(re, gb);
            }
        }
        coef += g_len << 7;
    }

    if (pulse_present) {
        idx = 0;
        for (i = 0; i < pulse->num_pulse; i++) {
            float co = coef_base[ pulse->pos[i] ];
            while (offsets[idx + 1] <= pulse->pos[i])
                idx++;
            if (band_type[idx] != NOISE_BT && sf[idx]) {
                float ico = -pulse->amp[i];
                if (co) {
                    co /= sf[idx];
                    ico = co / sqrtf(sqrtf(fabsf(co))) + (co > 0 ? -ico : ico);
                }
                coef_base[ pulse->pos[i] ] = cbrtf(fabsf(ico)) * ico * sf[idx];
            }
        }
    }
    return 0;
}

static av_always_inline float flt16_round(float pf)
{
    union float754 tmp;
    tmp.f = pf;
    tmp.i = (tmp.i + 0x00008000U) & 0xFFFF0000U;
    return tmp.f;
}

static av_always_inline float flt16_even(float pf)
{
    union float754 tmp;
    tmp.f = pf;
    tmp.i = (tmp.i + 0x00007FFFU + (tmp.i & 0x00010000U >> 16)) & 0xFFFF0000U;
    return tmp.f;
}

static av_always_inline float flt16_trunc(float pf)
{
    union float754 pun;
    pun.f = pf;
    pun.i &= 0xFFFF0000U;
    return pun.f;
}

static av_always_inline void predict(PredictorState *ps, float *coef,
                                     float sf_scale, float inv_sf_scale,
                    int output_enable)
{
    const float a     = 0.953125; // 61.0 / 64
    const float alpha = 0.90625;  // 29.0 / 32
    float e0, e1;
    float pv;
    float k1, k2;
    float   r0 = ps->r0,     r1 = ps->r1;
    float cor0 = ps->cor0, cor1 = ps->cor1;
    float var0 = ps->var0, var1 = ps->var1;

    k1 = var0 > 1 ? cor0 * flt16_even(a / var0) : 0;
    k2 = var1 > 1 ? cor1 * flt16_even(a / var1) : 0;

    pv = flt16_round(k1 * r0 + k2 * r1);
    if (output_enable)
        *coef += pv * sf_scale;

    e0 = *coef * inv_sf_scale;
    e1 = e0 - k1 * r0;

    ps->cor1 = flt16_trunc(alpha * cor1 + r1 * e1);
    ps->var1 = flt16_trunc(alpha * var1 + 0.5f * (r1 * r1 + e1 * e1));
    ps->cor0 = flt16_trunc(alpha * cor0 + r0 * e0);
    ps->var0 = flt16_trunc(alpha * var0 + 0.5f * (r0 * r0 + e0 * e0));

    ps->r1 = flt16_trunc(a * (r0 - k1 * e0));
    ps->r0 = flt16_trunc(a * e0);
}

/**
 * Apply AAC-Main style frequency domain prediction.
 */
static void apply_prediction(AACContext *ac, SingleChannelElement *sce)
{
    int sfb, k;
    float sf_scale = ac->sf_scale, inv_sf_scale = 1 / ac->sf_scale;

    if (!sce->ics.predictor_initialized) {
        reset_all_predictors(sce->predictor_state);
        sce->ics.predictor_initialized = 1;
    }

    if (sce->ics.window_sequence[0] != EIGHT_SHORT_SEQUENCE) {
        for (sfb = 0; sfb < ff_aac_pred_sfb_max[ac->m4ac.sampling_index]; sfb++) {
            for (k = sce->ics.swb_offset[sfb]; k < sce->ics.swb_offset[sfb + 1]; k++) {
                predict(&sce->predictor_state[k], &sce->coeffs[k],
                        sf_scale, inv_sf_scale,
                        sce->ics.predictor_present && sce->ics.prediction_used[sfb]);
            }
        }
        if (sce->ics.predictor_reset_group)
            reset_predictor_group(sce->predictor_state, sce->ics.predictor_reset_group);
    } else
        reset_all_predictors(sce->predictor_state);
}

/**
 * Decode an individual_channel_stream payload; reference: table 4.44.
 *
 * @param   common_window   Channels have independent [0], or shared [1], Individual Channel Stream information.
 * @param   scale_flag      scalable [1] or non-scalable [0] AAC (Unused until scalable AAC is implemented.)
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_ics(AACContext *ac, SingleChannelElement *sce,
                      GetBitContext *gb, int common_window, int scale_flag)
{
    Pulse pulse;
    TemporalNoiseShaping    *tns = &sce->tns;
    IndividualChannelStream *ics = &sce->ics;
    float *out = sce->coeffs;
    int global_gain, pulse_present = 0;

    /* This assignment is to silence a GCC warning about the variable being used
     * uninitialized when in fact it always is.
     */
    pulse.num_pulse = 0;

    global_gain = get_bits(gb, 8);

    if (!common_window && !scale_flag) {
        if (decode_ics_info(ac, ics, gb, 0) < 0)
            return -1;
    }

    if (decode_band_types(ac, sce->band_type, sce->band_type_run_end, gb, ics) < 0)
        return -1;
    if (decode_scalefactors(ac, sce->sf, gb, global_gain, ics, sce->band_type, sce->band_type_run_end) < 0)
        return -1;

    pulse_present = 0;
    if (!scale_flag) {
        if ((pulse_present = get_bits1(gb))) {
            if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
                av_log(ac->avctx, AV_LOG_ERROR, "Pulse tool not allowed in eight short sequence.\n");
                return -1;
            }
            if (decode_pulses(&pulse, gb, ics->swb_offset, ics->num_swb)) {
                av_log(ac->avctx, AV_LOG_ERROR, "Pulse data corrupt or invalid.\n");
                return -1;
            }
        }
        if ((tns->present = get_bits1(gb)) && decode_tns(ac, tns, gb, ics))
            return -1;
        if (get_bits1(gb)) {
            av_log_missing_feature(ac->avctx, "SSR", 1);
            return -1;
        }
    }

    if (decode_spectrum_and_dequant(ac, out, gb, sce->sf, pulse_present, &pulse, ics, sce->band_type) < 0)
        return -1;

    if (ac->m4ac.object_type == AOT_AAC_MAIN && !common_window)
        apply_prediction(ac, sce);

    return 0;
}

/**
 * Mid/Side stereo decoding; reference: 4.6.8.1.3.
 */
static void apply_mid_side_stereo(AACContext *ac, ChannelElement *cpe)
{
    const IndividualChannelStream *ics = &cpe->ch[0].ics;
    float *ch0 = cpe->ch[0].coeffs;
    float *ch1 = cpe->ch[1].coeffs;
    int g, i, group, idx = 0;
    const uint16_t *offsets = ics->swb_offset;
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb; i++, idx++) {
            if (cpe->ms_mask[idx] &&
                    cpe->ch[0].band_type[idx] < NOISE_BT && cpe->ch[1].band_type[idx] < NOISE_BT) {
                for (group = 0; group < ics->group_len[g]; group++) {
                    ac->dsp.butterflies_float(ch0 + group * 128 + offsets[i],
                                              ch1 + group * 128 + offsets[i],
                                              offsets[i+1] - offsets[i]);
                }
            }
        }
        ch0 += ics->group_len[g] * 128;
        ch1 += ics->group_len[g] * 128;
    }
}

/**
 * intensity stereo decoding; reference: 4.6.8.2.3
 *
 * @param   ms_present  Indicates mid/side stereo presence. [0] mask is all 0s;
 *                      [1] mask is decoded from bitstream; [2] mask is all 1s;
 *                      [3] reserved for scalable AAC
 */
static void apply_intensity_stereo(ChannelElement *cpe, int ms_present)
{
    const IndividualChannelStream *ics = &cpe->ch[1].ics;
    SingleChannelElement         *sce1 = &cpe->ch[1];
    float *coef0 = cpe->ch[0].coeffs, *coef1 = cpe->ch[1].coeffs;
    const uint16_t *offsets = ics->swb_offset;
    int g, group, i, k, idx = 0;
    int c;
    float scale;
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb;) {
            if (sce1->band_type[idx] == INTENSITY_BT || sce1->band_type[idx] == INTENSITY_BT2) {
                const int bt_run_end = sce1->band_type_run_end[idx];
                for (; i < bt_run_end; i++, idx++) {
                    c = -1 + 2 * (sce1->band_type[idx] - 14);
                    if (ms_present)
                        c *= 1 - 2 * cpe->ms_mask[idx];
                    scale = c * sce1->sf[idx];
                    for (group = 0; group < ics->group_len[g]; group++)
                        for (k = offsets[i]; k < offsets[i + 1]; k++)
                            coef1[group * 128 + k] = scale * coef0[group * 128 + k];
                }
            } else {
                int bt_run_end = sce1->band_type_run_end[idx];
                idx += bt_run_end - i;
                i    = bt_run_end;
            }
        }
        coef0 += ics->group_len[g] * 128;
        coef1 += ics->group_len[g] * 128;
    }
}

/**
 * Decode a channel_pair_element; reference: table 4.4.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_cpe(AACContext *ac, GetBitContext *gb, ChannelElement *cpe)
{
    int i, ret, common_window, ms_present = 0;

    common_window = get_bits1(gb);
    if (common_window) {
        if (decode_ics_info(ac, &cpe->ch[0].ics, gb, 1))
            return -1;
        i = cpe->ch[1].ics.use_kb_window[0];
        cpe->ch[1].ics = cpe->ch[0].ics;
        cpe->ch[1].ics.use_kb_window[1] = i;
        ms_present = get_bits(gb, 2);
        if (ms_present == 3) {
            av_log(ac->avctx, AV_LOG_ERROR, "ms_present = 3 is reserved.\n");
            return -1;
        } else if (ms_present)
            decode_mid_side_stereo(cpe, gb, ms_present);
    }
    if ((ret = decode_ics(ac, &cpe->ch[0], gb, common_window, 0)))
        return ret;
    if ((ret = decode_ics(ac, &cpe->ch[1], gb, common_window, 0)))
        return ret;

    if (common_window) {
        if (ms_present)
            apply_mid_side_stereo(ac, cpe);
        if (ac->m4ac.object_type == AOT_AAC_MAIN) {
            apply_prediction(ac, &cpe->ch[0]);
            apply_prediction(ac, &cpe->ch[1]);
        }
    }

    apply_intensity_stereo(cpe, ms_present);
    return 0;
}

static const float cce_scale[] = {
    1.09050773266525765921, //2^(1/8)
    1.18920711500272106672, //2^(1/4)
    M_SQRT2,
    2,
};

/**
 * Decode coupling_channel_element; reference: table 4.8.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_cce(AACContext *ac, GetBitContext *gb, ChannelElement *che)
{
    int num_gain = 0;
    int c, g, sfb, ret;
    int sign;
    float scale;
    SingleChannelElement *sce = &che->ch[0];
    ChannelCoupling     *coup = &che->coup;

    coup->coupling_point = 2 * get_bits1(gb);
    coup->num_coupled = get_bits(gb, 3);
    for (c = 0; c <= coup->num_coupled; c++) {
        num_gain++;
        coup->type[c] = get_bits1(gb) ? TYPE_CPE : TYPE_SCE;
        coup->id_select[c] = get_bits(gb, 4);
        if (coup->type[c] == TYPE_CPE) {
            coup->ch_select[c] = get_bits(gb, 2);
            if (coup->ch_select[c] == 3)
                num_gain++;
        } else
            coup->ch_select[c] = 2;
    }
    coup->coupling_point += get_bits1(gb) || (coup->coupling_point >> 1);

    sign  = get_bits(gb, 1);
    scale = cce_scale[get_bits(gb, 2)];

    if ((ret = decode_ics(ac, sce, gb, 0, 0)))
        return ret;

    for (c = 0; c < num_gain; c++) {
        int idx  = 0;
        int cge  = 1;
        int gain = 0;
        float gain_cache = 1.;
        if (c) {
            cge = coup->coupling_point == AFTER_IMDCT ? 1 : get_bits1(gb);
            gain = cge ? get_vlc2(gb, vlc_scalefactors.table, 7, 3) - 60: 0;
            gain_cache = powf(scale, -gain);
        }
        if (coup->coupling_point == AFTER_IMDCT) {
            coup->gain[c][0] = gain_cache;
        } else {
            for (g = 0; g < sce->ics.num_window_groups; g++) {
                for (sfb = 0; sfb < sce->ics.max_sfb; sfb++, idx++) {
                    if (sce->band_type[idx] != ZERO_BT) {
                        if (!cge) {
                            int t = get_vlc2(gb, vlc_scalefactors.table, 7, 3) - 60;
                            if (t) {
                                int s = 1;
                                t = gain += t;
                                if (sign) {
                                    s  -= 2 * (t & 0x1);
                                    t >>= 1;
                                }
                                gain_cache = powf(scale, -t) * s;
                            }
                        }
                        coup->gain[c][idx] = gain_cache;
                    }
                }
            }
        }
    }
    return 0;
}

/**
 * Parse whether channels are to be excluded from Dynamic Range Compression; reference: table 4.53.
 *
 * @return  Returns number of bytes consumed.
 */
static int decode_drc_channel_exclusions(DynamicRangeControl *che_drc,
                                         GetBitContext *gb)
{
    int i;
    int num_excl_chan = 0;

    do {
        for (i = 0; i < 7; i++)
            che_drc->exclude_mask[num_excl_chan++] = get_bits1(gb);
    } while (num_excl_chan < MAX_CHANNELS - 7 && get_bits1(gb));

    return num_excl_chan / 7;
}

/**
 * Decode dynamic range information; reference: table 4.52.
 *
 * @param   cnt length of TYPE_FIL syntactic element in bytes
 *
 * @return  Returns number of bytes consumed.
 */
static int decode_dynamic_range(DynamicRangeControl *che_drc,
                                GetBitContext *gb, int cnt)
{
    int n             = 1;
    int drc_num_bands = 1;
    int i;

    /* pce_tag_present? */
    if (get_bits1(gb)) {
        che_drc->pce_instance_tag  = get_bits(gb, 4);
        skip_bits(gb, 4); // tag_reserved_bits
        n++;
    }

    /* excluded_chns_present? */
    if (get_bits1(gb)) {
        n += decode_drc_channel_exclusions(che_drc, gb);
    }

    /* drc_bands_present? */
    if (get_bits1(gb)) {
        che_drc->band_incr            = get_bits(gb, 4);
        che_drc->interpolation_scheme = get_bits(gb, 4);
        n++;
        drc_num_bands += che_drc->band_incr;
        for (i = 0; i < drc_num_bands; i++) {
            che_drc->band_top[i] = get_bits(gb, 8);
            n++;
        }
    }

    /* prog_ref_level_present? */
    if (get_bits1(gb)) {
        che_drc->prog_ref_level = get_bits(gb, 7);
        skip_bits1(gb); // prog_ref_level_reserved_bits
        n++;
    }

    for (i = 0; i < drc_num_bands; i++) {
        che_drc->dyn_rng_sgn[i] = get_bits1(gb);
        che_drc->dyn_rng_ctl[i] = get_bits(gb, 7);
        n++;
    }

    return n;
}

/**
 * Decode extension data (incomplete); reference: table 4.51.
 *
 * @param   cnt length of TYPE_FIL syntactic element in bytes
 *
 * @return Returns number of bytes consumed
 */
static int decode_extension_payload(AACContext *ac, GetBitContext *gb, int cnt,
                                    ChannelElement *che, enum RawDataBlockType elem_type)
{
    int crc_flag = 0;
    int res = cnt;
    switch (get_bits(gb, 4)) { // extension type
    case EXT_SBR_DATA_CRC:
        crc_flag++;
    case EXT_SBR_DATA:
        if (!che) {
            av_log(ac->avctx, AV_LOG_ERROR, "SBR was found before the first channel element.\n");
            return res;
        } else if (!ac->m4ac.sbr) {
            av_log(ac->avctx, AV_LOG_ERROR, "SBR signaled to be not-present but was found in the bitstream.\n");
            skip_bits_long(gb, 8 * cnt - 4);
            return res;
        } else if (ac->m4ac.sbr == -1 && ac->output_configured == OC_LOCKED) {
            av_log(ac->avctx, AV_LOG_ERROR, "Implicit SBR was found with a first occurrence after the first frame.\n");
            skip_bits_long(gb, 8 * cnt - 4);
            return res;
        } else if (ac->m4ac.ps == -1 && ac->output_configured < OC_LOCKED && ac->avctx->channels == 1) {
            ac->m4ac.sbr = 1;
            ac->m4ac.ps = 1;
            output_configure(ac, ac->che_pos, ac->che_pos, ac->m4ac.chan_config, ac->output_configured);
        } else {
            ac->m4ac.sbr = 1;
        }
        res = ff_decode_sbr_extension(ac, &che->sbr, gb, crc_flag, cnt, elem_type);
        break;
    case EXT_DYNAMIC_RANGE:
        res = decode_dynamic_range(&ac->che_drc, gb, cnt);
        break;
    case EXT_FILL:
    case EXT_FILL_DATA:
    case EXT_DATA_ELEMENT:
    default:
        skip_bits_long(gb, 8 * cnt - 4);
        break;
    };
    return res;
}

/**
 * Decode Temporal Noise Shaping filter coefficients and apply all-pole filters; reference: 4.6.9.3.
 *
 * @param   decode  1 if tool is used normally, 0 if tool is used in LTP.
 * @param   coef    spectral coefficients
 */
static void apply_tns(float coef[1024], TemporalNoiseShaping *tns,
                      IndividualChannelStream *ics, int decode)
{
    const int mmm = FFMIN(ics->tns_max_bands, ics->max_sfb);
    int w, filt, m, i;
    int bottom, top, order, start, end, size, inc;
    float lpc[TNS_MAX_ORDER];

    for (w = 0; w < ics->num_windows; w++) {
        bottom = ics->num_swb;
        for (filt = 0; filt < tns->n_filt[w]; filt++) {
            top    = bottom;
            bottom = FFMAX(0, top - tns->length[w][filt]);
            order  = tns->order[w][filt];
            if (order == 0)
                continue;

            // tns_decode_coef
            compute_lpc_coefs(tns->coef[w][filt], order, lpc, 0, 0, 0);

            start = ics->swb_offset[FFMIN(bottom, mmm)];
            end   = ics->swb_offset[FFMIN(   top, mmm)];
            if ((size = end - start) <= 0)
                continue;
            if (tns->direction[w][filt]) {
                inc = -1;
                start = end - 1;
            } else {
                inc = 1;
            }
            start += w * 128;

            // ar filter
            for (m = 0; m < size; m++, start += inc)
                for (i = 1; i <= FFMIN(m, order); i++)
                    coef[start] -= coef[start - i * inc] * lpc[i - 1];
        }
    }
}

/**
 * Conduct IMDCT and windowing.
 */
static void imdct_and_windowing(AACContext *ac, SingleChannelElement *sce, float bias)
{
    IndividualChannelStream *ics = &sce->ics;
    float *in    = sce->coeffs;
    float *out   = sce->ret;
    float *saved = sce->saved;
    const float *swindow      = ics->use_kb_window[0] ? ff_aac_kbd_short_128 : ff_sine_128;
    const float *lwindow_prev = ics->use_kb_window[1] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    const float *swindow_prev = ics->use_kb_window[1] ? ff_aac_kbd_short_128 : ff_sine_128;
    float *buf  = ac->buf_mdct;
    float *temp = ac->temp;
    int i;

    // imdct
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        for (i = 0; i < 1024; i += 128)
            ff_imdct_half(&ac->mdct_small, buf + i, in + i);
    } else
        ff_imdct_half(&ac->mdct, buf, in);

    /* window overlapping
     * NOTE: To simplify the overlapping code, all 'meaningless' short to long
     * and long to short transitions are considered to be short to short
     * transitions. This leaves just two cases (long to long and short to short)
     * with a little special sauce for EIGHT_SHORT_SEQUENCE.
     */
    if ((ics->window_sequence[1] == ONLY_LONG_SEQUENCE || ics->window_sequence[1] == LONG_STOP_SEQUENCE) &&
            (ics->window_sequence[0] == ONLY_LONG_SEQUENCE || ics->window_sequence[0] == LONG_START_SEQUENCE)) {
        ac->dsp.vector_fmul_window(    out,               saved,            buf,         lwindow_prev, bias, 512);
    } else {
        for (i = 0; i < 448; i++)
            out[i] = saved[i] + bias;

        if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
            ac->dsp.vector_fmul_window(out + 448 + 0*128, saved + 448,      buf + 0*128, swindow_prev, bias, 64);
            ac->dsp.vector_fmul_window(out + 448 + 1*128, buf + 0*128 + 64, buf + 1*128, swindow,      bias, 64);
            ac->dsp.vector_fmul_window(out + 448 + 2*128, buf + 1*128 + 64, buf + 2*128, swindow,      bias, 64);
            ac->dsp.vector_fmul_window(out + 448 + 3*128, buf + 2*128 + 64, buf + 3*128, swindow,      bias, 64);
            ac->dsp.vector_fmul_window(temp,              buf + 3*128 + 64, buf + 4*128, swindow,      bias, 64);
            memcpy(                    out + 448 + 4*128, temp, 64 * sizeof(float));
        } else {
            ac->dsp.vector_fmul_window(out + 448,         saved + 448,      buf,         swindow_prev, bias, 64);
            for (i = 576; i < 1024; i++)
                out[i] = buf[i-512] + bias;
        }
    }

    // buffer update
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        for (i = 0; i < 64; i++)
            saved[i] = temp[64 + i] - bias;
        ac->dsp.vector_fmul_window(saved + 64,  buf + 4*128 + 64, buf + 5*128, swindow, 0, 64);
        ac->dsp.vector_fmul_window(saved + 192, buf + 5*128 + 64, buf + 6*128, swindow, 0, 64);
        ac->dsp.vector_fmul_window(saved + 320, buf + 6*128 + 64, buf + 7*128, swindow, 0, 64);
        memcpy(                    saved + 448, buf + 7*128 + 64,  64 * sizeof(float));
    } else if (ics->window_sequence[0] == LONG_START_SEQUENCE) {
        memcpy(                    saved,       buf + 512,        448 * sizeof(float));
        memcpy(                    saved + 448, buf + 7*128 + 64,  64 * sizeof(float));
    } else { // LONG_STOP or ONLY_LONG
        memcpy(                    saved,       buf + 512,        512 * sizeof(float));
    }
}

/**
 * Apply dependent channel coupling (applied before IMDCT).
 *
 * @param   index   index into coupling gain array
 */
static void apply_dependent_coupling(AACContext *ac,
                                     SingleChannelElement *target,
                                     ChannelElement *cce, int index)
{
    IndividualChannelStream *ics = &cce->ch[0].ics;
    const uint16_t *offsets = ics->swb_offset;
    float *dest = target->coeffs;
    const float *src = cce->ch[0].coeffs;
    int g, i, group, k, idx = 0;
    if (ac->m4ac.object_type == AOT_AAC_LTP) {
        av_log(ac->avctx, AV_LOG_ERROR,
               "Dependent coupling is not supported together with LTP\n");
        return;
    }
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb; i++, idx++) {
            if (cce->ch[0].band_type[idx] != ZERO_BT) {
                const float gain = cce->coup.gain[index][idx];
                for (group = 0; group < ics->group_len[g]; group++) {
                    for (k = offsets[i]; k < offsets[i + 1]; k++) {
                        // XXX dsputil-ize
                        dest[group * 128 + k] += gain * src[group * 128 + k];
                    }
                }
            }
        }
        dest += ics->group_len[g] * 128;
        src  += ics->group_len[g] * 128;
    }
}

/**
 * Apply independent channel coupling (applied after IMDCT).
 *
 * @param   index   index into coupling gain array
 */
static void apply_independent_coupling(AACContext *ac,
                                       SingleChannelElement *target,
                                       ChannelElement *cce, int index)
{
    int i;
    const float gain = cce->coup.gain[index][0];
    const float bias = ac->add_bias;
    const float *src = cce->ch[0].ret;
    float *dest = target->ret;
    const int len = 1024 << (ac->m4ac.sbr == 1);

    for (i = 0; i < len; i++)
        dest[i] += gain * (src[i] - bias);
}

/**
 * channel coupling transformation interface
 *
 * @param   apply_coupling_method   pointer to (in)dependent coupling function
 */
static void apply_channel_coupling(AACContext *ac, ChannelElement *cc,
                                   enum RawDataBlockType type, int elem_id,
                                   enum CouplingPoint coupling_point,
                                   void (*apply_coupling_method)(AACContext *ac, SingleChannelElement *target, ChannelElement *cce, int index))
{
    int i, c;

    for (i = 0; i < MAX_ELEM_ID; i++) {
        ChannelElement *cce = ac->che[TYPE_CCE][i];
        int index = 0;

        if (cce && cce->coup.coupling_point == coupling_point) {
            ChannelCoupling *coup = &cce->coup;

            for (c = 0; c <= coup->num_coupled; c++) {
                if (coup->type[c] == type && coup->id_select[c] == elem_id) {
                    if (coup->ch_select[c] != 1) {
                        apply_coupling_method(ac, &cc->ch[0], cce, index);
                        if (coup->ch_select[c] != 0)
                            index++;
                    }
                    if (coup->ch_select[c] != 2)
                        apply_coupling_method(ac, &cc->ch[1], cce, index++);
                } else
                    index += 1 + (coup->ch_select[c] == 3);
            }
        }
    }
}

/**
 * Convert spectral data to float samples, applying all supported tools as appropriate.
 */
static void spectral_to_sample(AACContext *ac)
{
    int i, type;
    float imdct_bias = (ac->m4ac.sbr <= 0) ? ac->add_bias : 0.0f;
    for (type = 3; type >= 0; type--) {
        for (i = 0; i < MAX_ELEM_ID; i++) {
            ChannelElement *che = ac->che[type][i];
            if (che) {
                if (type <= TYPE_CPE)
                    apply_channel_coupling(ac, che, type, i, BEFORE_TNS, apply_dependent_coupling);
                if (che->ch[0].tns.present)
                    apply_tns(che->ch[0].coeffs, &che->ch[0].tns, &che->ch[0].ics, 1);
                if (che->ch[1].tns.present)
                    apply_tns(che->ch[1].coeffs, &che->ch[1].tns, &che->ch[1].ics, 1);
                if (type <= TYPE_CPE)
                    apply_channel_coupling(ac, che, type, i, BETWEEN_TNS_AND_IMDCT, apply_dependent_coupling);
                if (type != TYPE_CCE || che->coup.coupling_point == AFTER_IMDCT) {
                    imdct_and_windowing(ac, &che->ch[0], imdct_bias);
                    if (type == TYPE_CPE) {
                        imdct_and_windowing(ac, &che->ch[1], imdct_bias);
                    }
                    if (ac->m4ac.sbr > 0) {
                        ff_sbr_apply(ac, &che->sbr, type, che->ch[0].ret, che->ch[1].ret);
                    }
                }
                if (type <= TYPE_CCE)
                    apply_channel_coupling(ac, che, type, i, AFTER_IMDCT, apply_independent_coupling);
            }
        }
    }
}

static int parse_adts_frame_header(AACContext *ac, GetBitContext *gb)
{
    int size;
    AACADTSHeaderInfo hdr_info;

    size = ff_aac_parse_header(gb, &hdr_info);
    if (size > 0) {
        if (ac->output_configured != OC_LOCKED && hdr_info.chan_config) {
            enum ChannelPosition new_che_pos[4][MAX_ELEM_ID];
            memset(new_che_pos, 0, 4 * MAX_ELEM_ID * sizeof(new_che_pos[0][0]));
            ac->m4ac.chan_config = hdr_info.chan_config;
            if (set_default_channel_config(ac->avctx, new_che_pos, hdr_info.chan_config))
                return -7;
            if (output_configure(ac, ac->che_pos, new_che_pos, hdr_info.chan_config, OC_TRIAL_FRAME))
                return -7;
        } else if (ac->output_configured != OC_LOCKED) {
            ac->output_configured = OC_NONE;
        }
        if (ac->output_configured != OC_LOCKED) {
            ac->m4ac.sbr = -1;
            ac->m4ac.ps  = -1;
        }
        ac->m4ac.sample_rate     = hdr_info.sample_rate;
        ac->m4ac.sampling_index  = hdr_info.sampling_index;
        ac->m4ac.object_type     = hdr_info.object_type;
        if (!ac->avctx->sample_rate)
            ac->avctx->sample_rate = hdr_info.sample_rate;
        if (hdr_info.num_aac_frames == 1) {
            if (!hdr_info.crc_absent)
                skip_bits(gb, 16);
        } else {
            av_log_missing_feature(ac->avctx, "More than one AAC RDB per ADTS frame is", 0);
            return -1;
        }
    }
    return size;
}

static int aac_decode_frame_int(AVCodecContext *avctx, void *data,
                                int *data_size, GetBitContext *gb)
{
    AACContext *ac = avctx->priv_data;
    ChannelElement *che = NULL, *che_prev = NULL;
    enum RawDataBlockType elem_type, elem_type_prev = TYPE_END;
    int err, elem_id, data_size_tmp;
    int samples = 0, multiplier;

    if (show_bits(gb, 12) == 0xfff) {
        if (parse_adts_frame_header(ac, gb) < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error decoding AAC frame header.\n");
            return -1;
        }
        if (ac->m4ac.sampling_index > 12) {
            av_log(ac->avctx, AV_LOG_ERROR, "invalid sampling rate index %d\n", ac->m4ac.sampling_index);
            return -1;
        }
    }

    ac->tags_mapped = 0;
    // parse
    while ((elem_type = get_bits(gb, 3)) != TYPE_END) {
        elem_id = get_bits(gb, 4);

        if (elem_type < TYPE_DSE) {
            if (!(che=get_che(ac, elem_type, elem_id))) {
                av_log(ac->avctx, AV_LOG_ERROR, "channel element %d.%d is not allocated\n",
                       elem_type, elem_id);
                return -1;
            }
            samples = 1024;
        }

        switch (elem_type) {

        case TYPE_SCE:
            err = decode_ics(ac, &che->ch[0], gb, 0, 0);
            break;

        case TYPE_CPE:
            err = decode_cpe(ac, gb, che);
            break;

        case TYPE_CCE:
            err = decode_cce(ac, gb, che);
            break;

        case TYPE_LFE:
            err = decode_ics(ac, &che->ch[0], gb, 0, 0);
            break;

        case TYPE_DSE:
            err = skip_data_stream_element(ac, gb);
            break;

        case TYPE_PCE: {
            enum ChannelPosition new_che_pos[4][MAX_ELEM_ID];
            memset(new_che_pos, 0, 4 * MAX_ELEM_ID * sizeof(new_che_pos[0][0]));
            if ((err = decode_pce(avctx, &ac->m4ac, new_che_pos, gb)))
                break;
            if (ac->output_configured > OC_TRIAL_PCE)
                av_log(avctx, AV_LOG_ERROR,
                       "Not evaluating a further program_config_element as this construct is dubious at best.\n");
            else
                err = output_configure(ac, ac->che_pos, new_che_pos, 0, OC_TRIAL_PCE);
            break;
        }

        case TYPE_FIL:
            if (elem_id == 15)
                elem_id += get_bits(gb, 8) - 1;
            if (get_bits_left(gb) < 8 * elem_id) {
                    av_log(avctx, AV_LOG_ERROR, overread_err);
                    return -1;
            }
            while (elem_id > 0)
                elem_id -= decode_extension_payload(ac, gb, elem_id, che_prev, elem_type_prev);
            err = 0; /* FIXME */
            break;

        default:
            err = -1; /* should not happen, but keeps compiler happy */
            break;
        }

        che_prev       = che;
        elem_type_prev = elem_type;

        if (err)
            return err;

        if (get_bits_left(gb) < 3) {
            av_log(avctx, AV_LOG_ERROR, overread_err);
            return -1;
        }
    }

    spectral_to_sample(ac);

    multiplier = (ac->m4ac.sbr == 1) ? ac->m4ac.ext_sample_rate > ac->m4ac.sample_rate : 0;
    samples <<= multiplier;
    if (ac->output_configured < OC_LOCKED) {
        avctx->sample_rate = ac->m4ac.sample_rate << multiplier;
        avctx->frame_size = samples;
    }

    data_size_tmp = samples * avctx->channels * sizeof(int16_t);
    if (*data_size < data_size_tmp) {
        av_log(avctx, AV_LOG_ERROR,
               "Output buffer too small (%d) or trying to output too many samples (%d) for this frame.\n",
               *data_size, data_size_tmp);
        return -1;
    }
    *data_size = data_size_tmp;

    if (samples)
        ac->dsp.float_to_int16_interleave(data, (const float **)ac->output_data, samples, avctx->channels);

    if (ac->output_configured)
        ac->output_configured = OC_LOCKED;

    return 0;
}

static int aac_decode_frame(AVCodecContext *avctx, void *data,
                            int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    GetBitContext gb;
    int buf_consumed;
    int buf_offset;
    int err;

    init_get_bits(&gb, buf, buf_size * 8);

    if ((err = aac_decode_frame_int(avctx, data, data_size, &gb)) < 0)
        return err;

    buf_consumed = (get_bits_count(&gb) + 7) >> 3;
    for (buf_offset = buf_consumed; buf_offset < buf_size; buf_offset++)
        if (buf[buf_offset])
            break;

    return buf_size > buf_offset ? buf_consumed : buf_size;
}

static av_cold int aac_decode_close(AVCodecContext *avctx)
{
    AACContext *ac = avctx->priv_data;
    int i, type;

    for (i = 0; i < MAX_ELEM_ID; i++) {
        for (type = 0; type < 4; type++) {
            if (ac->che[type][i])
                ff_aac_sbr_ctx_close(&ac->che[type][i]->sbr);
            av_freep(&ac->che[type][i]);
        }
    }

    ff_mdct_end(&ac->mdct);
    ff_mdct_end(&ac->mdct_small);
    return 0;
}


#define LOAS_SYNC_WORD   0x2b7       ///< 11 bits LOAS sync word

struct LATMContext {
    AACContext      aac_ctx;             ///< containing AACContext
    int             initialized;         ///< initilized after a valid extradata was seen

    // parser data
    int             audio_mux_version_A; ///< LATM syntax version
    int             frame_length_type;   ///< 0/1 variable/fixed frame length
    int             frame_length;        ///< frame length for fixed frame length
};

static inline uint32_t latm_get_value(GetBitContext *b)
{
    int length = get_bits(b, 2);

    return get_bits_long(b, (length+1)*8);
}

static int latm_decode_audio_specific_config(struct LATMContext *latmctx,
                                             GetBitContext *gb)
{
    AVCodecContext *avctx = latmctx->aac_ctx.avctx;
    MPEG4AudioConfig m4ac;
    int  config_start_bit = get_bits_count(gb);
    int     bits_consumed, esize;

    if (config_start_bit % 8) {
        av_log_missing_feature(latmctx->aac_ctx.avctx, "audio specific "
                               "config not byte aligned.\n", 1);
        return AVERROR_INVALIDDATA;
    } else {
        bits_consumed =
            decode_audio_specific_config(NULL, avctx, &m4ac,
                                         gb->buffer + (config_start_bit / 8),
                                         get_bits_left(gb) / 8);

        if (bits_consumed < 0)
            return AVERROR_INVALIDDATA;

        esize = (bits_consumed+7) / 8;

        if (avctx->extradata_size <= esize) {
            av_free(avctx->extradata);
            avctx->extradata = av_malloc(esize + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!avctx->extradata)
                return AVERROR(ENOMEM);
        }

        avctx->extradata_size = esize;
        memcpy(avctx->extradata, gb->buffer + (config_start_bit/8), esize);
        memset(avctx->extradata+esize, 0, FF_INPUT_BUFFER_PADDING_SIZE);

        skip_bits_long(gb, bits_consumed);
    }

    return bits_consumed;
}

static int read_stream_mux_config(struct LATMContext *latmctx,
                                  GetBitContext *gb)
{
    int ret, audio_mux_version = get_bits(gb, 1);

    latmctx->audio_mux_version_A = 0;
    if (audio_mux_version)
        latmctx->audio_mux_version_A = get_bits(gb, 1);

    if (!latmctx->audio_mux_version_A) {

        if (audio_mux_version)
            latm_get_value(gb);                 // taraFullness

        skip_bits(gb, 1);                       // allStreamSameTimeFraming
        skip_bits(gb, 6);                       // numSubFrames
        // numPrograms
        if (get_bits(gb, 4)) {                  // numPrograms
            av_log_missing_feature(latmctx->aac_ctx.avctx,
                                   "multiple programs are not supported\n", 1);
            return AVERROR_PATCHWELCOME;
        }

        // for each program (which there is only on in DVB)

        // for each layer (which there is only on in DVB)
        if (get_bits(gb, 3)) {                   // numLayer
            av_log_missing_feature(latmctx->aac_ctx.avctx,
                                   "multiple layers are not supported\n", 1);
            return AVERROR_PATCHWELCOME;
        }

        // for all but first stream: use_same_config = get_bits(gb, 1);
        if (!audio_mux_version) {
            if ((ret = latm_decode_audio_specific_config(latmctx, gb)) < 0)
                return ret;
        } else {
            int ascLen = latm_get_value(gb);
            if ((ret = latm_decode_audio_specific_config(latmctx, gb)) < 0)
                return ret;
            ascLen -= ret;
            skip_bits_long(gb, ascLen);
        }

        latmctx->frame_length_type = get_bits(gb, 3);
        switch (latmctx->frame_length_type) {
        case 0:
            skip_bits(gb, 8);       // latmBufferFullness
            break;
        case 1:
            latmctx->frame_length = get_bits(gb, 9);
            break;
        case 3:
        case 4:
        case 5:
            skip_bits(gb, 6);       // CELP frame length table index
            break;
        case 6:
        case 7:
            skip_bits(gb, 1);       // HVXC frame length table index
            break;
        }

        if (get_bits(gb, 1)) {                  // other data
            if (audio_mux_version) {
                latm_get_value(gb);             // other_data_bits
            } else {
                int esc;
                do {
                    esc = get_bits(gb, 1);
                    skip_bits(gb, 8);
                } while (esc);
            }
        }

        if (get_bits(gb, 1))                     // crc present
            skip_bits(gb, 8);                    // config_crc
    }

    return 0;
}

static int read_payload_length_info(struct LATMContext *ctx, GetBitContext *gb)
{
    uint8_t tmp;

    if (ctx->frame_length_type == 0) {
        int mux_slot_length = 0;
        do {
            tmp = get_bits(gb, 8);
            mux_slot_length += tmp;
        } while (tmp == 255);
        return mux_slot_length;
    } else if (ctx->frame_length_type == 1) {
        return ctx->frame_length;
    } else if (ctx->frame_length_type == 3 ||
               ctx->frame_length_type == 5 ||
               ctx->frame_length_type == 7) {
        skip_bits(gb, 2);          // mux_slot_length_coded
    }
    return 0;
}

static int read_audio_mux_element(struct LATMContext *latmctx,
                                  GetBitContext *gb)
{
    int err;
    uint8_t use_same_mux = get_bits(gb, 1);
    if (!use_same_mux) {
        if ((err = read_stream_mux_config(latmctx, gb)) < 0)
            return err;
    } else if (!latmctx->aac_ctx.avctx->extradata) {
        av_log(latmctx->aac_ctx.avctx, AV_LOG_DEBUG,
               "no decoder config found\n");
        return AVERROR(EAGAIN);
    }
    if (latmctx->audio_mux_version_A == 0) {
        int mux_slot_length_bytes = read_payload_length_info(latmctx, gb);
        if (mux_slot_length_bytes * 8 > get_bits_left(gb)) {
            av_log(latmctx->aac_ctx.avctx, AV_LOG_ERROR, "incomplete frame\n");
            return AVERROR_INVALIDDATA;
        } else if (mux_slot_length_bytes * 8 + 256 < get_bits_left(gb)) {
            av_log(latmctx->aac_ctx.avctx, AV_LOG_ERROR,
                   "frame length mismatch %d << %d\n",
                   mux_slot_length_bytes * 8, get_bits_left(gb));
            return AVERROR_INVALIDDATA;
        }
    }
    return 0;
}


static int latm_decode_frame(AVCodecContext *avctx, void *out, int *out_size,
                             AVPacket *avpkt)
{
    struct LATMContext *latmctx = avctx->priv_data;
    int                 muxlength, err;
    GetBitContext       gb;

    if (avpkt->size == 0)
        return 0;

    init_get_bits(&gb, avpkt->data, avpkt->size * 8);

    // check for LOAS sync word
    if (get_bits(&gb, 11) != LOAS_SYNC_WORD)
        return AVERROR_INVALIDDATA;

    muxlength = get_bits(&gb, 13) + 3;
    // not enough data, the parser should have sorted this
    if (muxlength > avpkt->size)
        return AVERROR_INVALIDDATA;

    if ((err = read_audio_mux_element(latmctx, &gb)) < 0)
        return err;

    if (!latmctx->initialized) {
        if (!avctx->extradata) {
            *out_size = 0;
            return avpkt->size;
        } else {
            if ((err = aac_decode_init(avctx)) < 0)
                return err;
            latmctx->initialized = 1;
        }
    }

    if (show_bits(&gb, 12) == 0xfff) {
        av_log(latmctx->aac_ctx.avctx, AV_LOG_ERROR,
               "ADTS header detected, probably as result of configuration "
               "misparsing\n");
        return AVERROR_INVALIDDATA;
    }

    if ((err = aac_decode_frame_int(avctx, out, out_size, &gb)) < 0)
        return err;

    return muxlength;
}

av_cold static int latm_decode_init(AVCodecContext *avctx)
{
    struct LATMContext *latmctx = avctx->priv_data;
    int ret;

    ret = aac_decode_init(avctx);

    if (avctx->extradata_size > 0) {
        latmctx->initialized = !ret;
    } else {
        latmctx->initialized = 0;
    }

    return ret;
}


AVCodec aac_decoder = {
    "aac",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_AAC,
    sizeof(AACContext),
    aac_decode_init,
    NULL,
    aac_decode_close,
    aac_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("Advanced Audio Coding"),
    .sample_fmts = (const enum AVSampleFormat[]) {
        AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE
    },
    .channel_layouts = aac_channel_layout,
};

/*
    Note: This decoder filter is intended to decode LATM streams transferred
    in MPEG transport streams which only contain one program.
    To do a more complex LATM demuxing a separate LATM demuxer should be used.
*/
AVCodec aac_latm_decoder = {
    .name = "aac_latm",
    .type = CODEC_TYPE_AUDIO,
    .id   = CODEC_ID_AAC_LATM,
    .priv_data_size = sizeof(struct LATMContext),
    .init   = latm_decode_init,
    .close  = aac_decode_close,
    .decode = latm_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("AAC LATM (Advanced Audio Codec LATM syntax)"),
    .sample_fmts = (const enum AVSampleFormat[]) {
        AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE
    },
    .channel_layouts = aac_channel_layout,
};
