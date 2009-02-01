/*
 * AAC decoder
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
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
 * @file libavcodec/aac.c
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
 * N (in progress)      Spectral Band Replication
 * Y (not in this code) Layer-1
 * Y (not in this code) Layer-2
 * Y (not in this code) Layer-3
 * N                    SinuSoidal Coding (Transient, Sinusoid, Noise)
 * N (planned)          Parametric Stereo
 * N                    Direct Stream Transfer
 *
 * Note: - HE AAC v1 comprises LC AAC with Spectral Band Replication.
 *       - HE AAC v2 comprises LC AAC with Spectral Band Replication and
           Parametric Stereo.
 */


#include "avcodec.h"
#include "internal.h"
#include "bitstream.h"
#include "dsputil.h"
#include "lpc.h"

#include "aac.h"
#include "aactab.h"
#include "aacdectab.h"
#include "mpeg4audio.h"
#include "aac_parser.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <string.h>

static VLC vlc_scalefactors;
static VLC vlc_spectral[11];


/**
 * Configure output channel order based on the current program configuration element.
 *
 * @param   che_pos current channel position configuration
 * @param   new_che_pos New channel position configuration - we only do something if it differs from the current one.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int output_configure(AACContext *ac, enum ChannelPosition che_pos[4][MAX_ELEM_ID],
        enum ChannelPosition new_che_pos[4][MAX_ELEM_ID]) {
    AVCodecContext *avctx = ac->avccontext;
    int i, type, channels = 0;

    if(!memcmp(che_pos, new_che_pos, 4 * MAX_ELEM_ID * sizeof(new_che_pos[0][0])))
        return 0; /* no change */

    memcpy(che_pos, new_che_pos, 4 * MAX_ELEM_ID * sizeof(new_che_pos[0][0]));

    /* Allocate or free elements depending on if they are in the
     * current program configuration.
     *
     * Set up default 1:1 output mapping.
     *
     * For a 5.1 stream the output order will be:
     *    [ Center ] [ Front Left ] [ Front Right ] [ LFE ] [ Surround Left ] [ Surround Right ]
     */

    for(i = 0; i < MAX_ELEM_ID; i++) {
        for(type = 0; type < 4; type++) {
            if(che_pos[type][i]) {
                if(!ac->che[type][i] && !(ac->che[type][i] = av_mallocz(sizeof(ChannelElement))))
                    return AVERROR(ENOMEM);
                if(type != TYPE_CCE) {
                    ac->output_data[channels++] = ac->che[type][i]->ch[0].ret;
                    if(type == TYPE_CPE) {
                        ac->output_data[channels++] = ac->che[type][i]->ch[1].ret;
                    }
                }
            } else
                av_freep(&ac->che[type][i]);
        }
    }

    avctx->channels = channels;
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
        enum ChannelPosition *sce_map, enum ChannelPosition type, GetBitContext * gb, int n) {
    while(n--) {
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
static int decode_pce(AACContext * ac, enum ChannelPosition new_che_pos[4][MAX_ELEM_ID],
        GetBitContext * gb) {
    int num_front, num_side, num_back, num_lfe, num_assoc_data, num_cc, sampling_index;

    skip_bits(gb, 2);  // object_type

    sampling_index = get_bits(gb, 4);
    if(sampling_index > 11) {
        av_log(ac->avccontext, AV_LOG_ERROR, "invalid sampling rate index %d\n", ac->m4ac.sampling_index);
        return -1;
    }
    ac->m4ac.sampling_index = sampling_index;
    ac->m4ac.sample_rate = ff_mpeg4audio_sample_rates[ac->m4ac.sampling_index];
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
    skip_bits_long(gb, 8 * get_bits(gb, 8));
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
static int set_default_channel_config(AACContext *ac, enum ChannelPosition new_che_pos[4][MAX_ELEM_ID],
        int channel_config)
{
    if(channel_config < 1 || channel_config > 7) {
        av_log(ac->avccontext, AV_LOG_ERROR, "invalid default channel configuration (%d)\n",
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

    if(channel_config != 2)
        new_che_pos[TYPE_SCE][0] = AAC_CHANNEL_FRONT; // front center (or mono)
    if(channel_config > 1)
        new_che_pos[TYPE_CPE][0] = AAC_CHANNEL_FRONT; // L + R (or stereo)
    if(channel_config == 4)
        new_che_pos[TYPE_SCE][1] = AAC_CHANNEL_BACK;  // back center
    if(channel_config > 4)
        new_che_pos[TYPE_CPE][(channel_config == 7) + 1]
                                 = AAC_CHANNEL_BACK;  // back stereo
    if(channel_config > 5)
        new_che_pos[TYPE_LFE][0] = AAC_CHANNEL_LFE;   // LFE
    if(channel_config == 7)
        new_che_pos[TYPE_CPE][1] = AAC_CHANNEL_FRONT; // outer front left + outer front right

    return 0;
}

/**
 * Decode GA "General Audio" specific configuration; reference: table 4.1.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_ga_specific_config(AACContext * ac, GetBitContext * gb, int channel_config) {
    enum ChannelPosition new_che_pos[4][MAX_ELEM_ID];
    int extension_flag, ret;

    if(get_bits1(gb)) {  // frameLengthFlag
        ff_log_missing_feature(ac->avccontext, "960/120 MDCT window is", 1);
        return -1;
    }

    if (get_bits1(gb))       // dependsOnCoreCoder
        skip_bits(gb, 14);   // coreCoderDelay
    extension_flag = get_bits1(gb);

    if(ac->m4ac.object_type == AOT_AAC_SCALABLE ||
       ac->m4ac.object_type == AOT_ER_AAC_SCALABLE)
        skip_bits(gb, 3);     // layerNr

    memset(new_che_pos, 0, 4 * MAX_ELEM_ID * sizeof(new_che_pos[0][0]));
    if (channel_config == 0) {
        skip_bits(gb, 4);  // element_instance_tag
        if((ret = decode_pce(ac, new_che_pos, gb)))
            return ret;
    } else {
        if((ret = set_default_channel_config(ac, new_che_pos, channel_config)))
            return ret;
    }
    if((ret = output_configure(ac, ac->che_pos, new_che_pos)))
        return ret;

    if (extension_flag) {
        switch (ac->m4ac.object_type) {
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
 * @param   data        pointer to AVCodecContext extradata
 * @param   data_size   size of AVCCodecContext extradata
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_audio_specific_config(AACContext * ac, void *data, int data_size) {
    GetBitContext gb;
    int i;

    init_get_bits(&gb, data, data_size * 8);

    if((i = ff_mpeg4audio_get_config(&ac->m4ac, data, data_size)) < 0)
        return -1;
    if(ac->m4ac.sampling_index > 11) {
        av_log(ac->avccontext, AV_LOG_ERROR, "invalid sampling rate index %d\n", ac->m4ac.sampling_index);
        return -1;
    }

    skip_bits_long(&gb, i);

    switch (ac->m4ac.object_type) {
    case AOT_AAC_MAIN:
    case AOT_AAC_LC:
        if (decode_ga_specific_config(ac, &gb, ac->m4ac.chan_config))
            return -1;
        break;
    default:
        av_log(ac->avccontext, AV_LOG_ERROR, "Audio object type %s%d is not supported.\n",
               ac->m4ac.sbr == 1? "SBR+" : "", ac->m4ac.object_type);
        return -1;
    }
    return 0;
}

/**
 * linear congruential pseudorandom number generator
 *
 * @param   previous_val    pointer to the current state of the generator
 *
 * @return  Returns a 32-bit pseudorandom integer
 */
static av_always_inline int lcg_random(int previous_val) {
    return previous_val * 1664525 + 1013904223;
}

static void reset_predict_state(PredictorState * ps) {
    ps->r0 = 0.0f;
    ps->r1 = 0.0f;
    ps->cor0 = 0.0f;
    ps->cor1 = 0.0f;
    ps->var0 = 1.0f;
    ps->var1 = 1.0f;
}

static void reset_all_predictors(PredictorState * ps) {
    int i;
    for (i = 0; i < MAX_PREDICTORS; i++)
        reset_predict_state(&ps[i]);
}

static void reset_predictor_group(PredictorState * ps, int group_num) {
    int i;
    for (i = group_num-1; i < MAX_PREDICTORS; i+=30)
        reset_predict_state(&ps[i]);
}

static av_cold int aac_decode_init(AVCodecContext * avccontext) {
    AACContext * ac = avccontext->priv_data;
    int i;

    ac->avccontext = avccontext;

    if (avccontext->extradata_size > 0) {
        if(decode_audio_specific_config(ac, avccontext->extradata, avccontext->extradata_size))
            return -1;
        avccontext->sample_rate = ac->m4ac.sample_rate;
    } else if (avccontext->channels > 0) {
        enum ChannelPosition new_che_pos[4][MAX_ELEM_ID];
        memset(new_che_pos, 0, 4 * MAX_ELEM_ID * sizeof(new_che_pos[0][0]));
        if(set_default_channel_config(ac, new_che_pos, avccontext->channels - (avccontext->channels == 8)))
            return -1;
        if(output_configure(ac, ac->che_pos, new_che_pos))
            return -1;
        ac->m4ac.sample_rate = avccontext->sample_rate;
    } else {
        ff_log_missing_feature(ac->avccontext, "Implicit channel configuration is", 0);
        return -1;
    }

    avccontext->sample_fmt  = SAMPLE_FMT_S16;
    avccontext->frame_size  = 1024;

    AAC_INIT_VLC_STATIC( 0, 144);
    AAC_INIT_VLC_STATIC( 1, 114);
    AAC_INIT_VLC_STATIC( 2, 188);
    AAC_INIT_VLC_STATIC( 3, 180);
    AAC_INIT_VLC_STATIC( 4, 172);
    AAC_INIT_VLC_STATIC( 5, 140);
    AAC_INIT_VLC_STATIC( 6, 168);
    AAC_INIT_VLC_STATIC( 7, 114);
    AAC_INIT_VLC_STATIC( 8, 262);
    AAC_INIT_VLC_STATIC( 9, 248);
    AAC_INIT_VLC_STATIC(10, 384);

    dsputil_init(&ac->dsp, avccontext);

    ac->random_state = 0x1f2e3d4c;

    // -1024 - Compensate wrong IMDCT method.
    // 32768 - Required to scale values to the correct range for the bias method
    //         for float to int16 conversion.

    if(ac->dsp.float_to_int16 == ff_float_to_int16_c) {
        ac->add_bias = 385.0f;
        ac->sf_scale = 1. / (-1024. * 32768.);
        ac->sf_offset = 0;
    } else {
        ac->add_bias = 0.0f;
        ac->sf_scale = 1. / -1024.;
        ac->sf_offset = 60;
    }

#if !CONFIG_HARDCODED_TABLES
    for (i = 0; i < 428; i++)
        ff_aac_pow2sf_tab[i] = pow(2, (i - 200)/4.);
#endif /* CONFIG_HARDCODED_TABLES */

    INIT_VLC_STATIC(&vlc_scalefactors,7,FF_ARRAY_ELEMS(ff_aac_scalefactor_code),
        ff_aac_scalefactor_bits, sizeof(ff_aac_scalefactor_bits[0]), sizeof(ff_aac_scalefactor_bits[0]),
        ff_aac_scalefactor_code, sizeof(ff_aac_scalefactor_code[0]), sizeof(ff_aac_scalefactor_code[0]),
        352);

    ff_mdct_init(&ac->mdct, 11, 1);
    ff_mdct_init(&ac->mdct_small, 8, 1);
    // window initialization
    ff_kbd_window_init(ff_aac_kbd_long_1024, 4.0, 1024);
    ff_kbd_window_init(ff_aac_kbd_short_128, 6.0, 128);
    ff_sine_window_init(ff_sine_1024, 1024);
    ff_sine_window_init(ff_sine_128, 128);

    return 0;
}

/**
 * Skip data_stream_element; reference: table 4.10.
 */
static void skip_data_stream_element(GetBitContext * gb) {
    int byte_align = get_bits1(gb);
    int count = get_bits(gb, 8);
    if (count == 255)
        count += get_bits(gb, 8);
    if (byte_align)
        align_get_bits(gb);
    skip_bits_long(gb, 8 * count);
}

static int decode_prediction(AACContext * ac, IndividualChannelStream * ics, GetBitContext * gb) {
    int sfb;
    if (get_bits1(gb)) {
        ics->predictor_reset_group = get_bits(gb, 5);
        if (ics->predictor_reset_group == 0 || ics->predictor_reset_group > 30) {
            av_log(ac->avccontext, AV_LOG_ERROR, "Invalid Predictor Reset Group.\n");
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
static int decode_ics_info(AACContext * ac, IndividualChannelStream * ics, GetBitContext * gb, int common_window) {
    if (get_bits1(gb)) {
        av_log(ac->avccontext, AV_LOG_ERROR, "Reserved bit set.\n");
        memset(ics, 0, sizeof(IndividualChannelStream));
        return -1;
    }
    ics->window_sequence[1] = ics->window_sequence[0];
    ics->window_sequence[0] = get_bits(gb, 2);
    ics->use_kb_window[1] = ics->use_kb_window[0];
    ics->use_kb_window[0] = get_bits1(gb);
    ics->num_window_groups = 1;
    ics->group_len[0] = 1;
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        int i;
        ics->max_sfb = get_bits(gb, 4);
        for (i = 0; i < 7; i++) {
            if (get_bits1(gb)) {
                ics->group_len[ics->num_window_groups-1]++;
            } else {
                ics->num_window_groups++;
                ics->group_len[ics->num_window_groups-1] = 1;
            }
        }
        ics->num_windows   = 8;
        ics->swb_offset    =      swb_offset_128[ac->m4ac.sampling_index];
        ics->num_swb       =  ff_aac_num_swb_128[ac->m4ac.sampling_index];
        ics->tns_max_bands =   tns_max_bands_128[ac->m4ac.sampling_index];
        ics->predictor_present = 0;
    } else {
        ics->max_sfb       = get_bits(gb, 6);
        ics->num_windows   = 1;
        ics->swb_offset    =     swb_offset_1024[ac->m4ac.sampling_index];
        ics->num_swb       = ff_aac_num_swb_1024[ac->m4ac.sampling_index];
        ics->tns_max_bands =  tns_max_bands_1024[ac->m4ac.sampling_index];
        ics->predictor_present = get_bits1(gb);
        ics->predictor_reset_group = 0;
        if (ics->predictor_present) {
            if (ac->m4ac.object_type == AOT_AAC_MAIN) {
                if (decode_prediction(ac, ics, gb)) {
                    memset(ics, 0, sizeof(IndividualChannelStream));
                    return -1;
                }
            } else if (ac->m4ac.object_type == AOT_AAC_LC) {
                av_log(ac->avccontext, AV_LOG_ERROR, "Prediction is not allowed in AAC-LC.\n");
                memset(ics, 0, sizeof(IndividualChannelStream));
                return -1;
            } else {
                ff_log_missing_feature(ac->avccontext, "Predictor bit set but LTP is", 1);
                memset(ics, 0, sizeof(IndividualChannelStream));
                return -1;
            }
        }
    }

    if(ics->max_sfb > ics->num_swb) {
        av_log(ac->avccontext, AV_LOG_ERROR,
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
static int decode_band_types(AACContext * ac, enum BandType band_type[120],
        int band_type_run_end[120], GetBitContext * gb, IndividualChannelStream * ics) {
    int g, idx = 0;
    const int bits = (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) ? 3 : 5;
    for (g = 0; g < ics->num_window_groups; g++) {
        int k = 0;
        while (k < ics->max_sfb) {
            uint8_t sect_len = k;
            int sect_len_incr;
            int sect_band_type = get_bits(gb, 4);
            if (sect_band_type == 12) {
                av_log(ac->avccontext, AV_LOG_ERROR, "invalid band type\n");
                return -1;
            }
            while ((sect_len_incr = get_bits(gb, bits)) == (1 << bits)-1)
                sect_len += sect_len_incr;
            sect_len += sect_len_incr;
            if (sect_len > ics->max_sfb) {
                av_log(ac->avccontext, AV_LOG_ERROR,
                    "Number of bands (%d) exceeds limit (%d).\n",
                    sect_len, ics->max_sfb);
                return -1;
            }
            for (; k < sect_len; k++) {
                band_type        [idx]   = sect_band_type;
                band_type_run_end[idx++] = sect_len;
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
static int decode_scalefactors(AACContext * ac, float sf[120], GetBitContext * gb,
        unsigned int global_gain, IndividualChannelStream * ics,
        enum BandType band_type[120], int band_type_run_end[120]) {
    const int sf_offset = ac->sf_offset + (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE ? 12 : 0);
    int g, i, idx = 0;
    int offset[3] = { global_gain, global_gain - 90, 100 };
    int noise_flag = 1;
    static const char *sf_str[3] = { "Global gain", "Noise gain", "Intensity stereo position" };
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb;) {
            int run_end = band_type_run_end[idx];
            if (band_type[idx] == ZERO_BT) {
                for(; i < run_end; i++, idx++)
                    sf[idx] = 0.;
            }else if((band_type[idx] == INTENSITY_BT) || (band_type[idx] == INTENSITY_BT2)) {
                for(; i < run_end; i++, idx++) {
                    offset[2] += get_vlc2(gb, vlc_scalefactors.table, 7, 3) - 60;
                    if(offset[2] > 255U) {
                        av_log(ac->avccontext, AV_LOG_ERROR,
                            "%s (%d) out of range.\n", sf_str[2], offset[2]);
                        return -1;
                    }
                    sf[idx]  = ff_aac_pow2sf_tab[-offset[2] + 300];
                }
            }else if(band_type[idx] == NOISE_BT) {
                for(; i < run_end; i++, idx++) {
                    if(noise_flag-- > 0)
                        offset[1] += get_bits(gb, 9) - 256;
                    else
                        offset[1] += get_vlc2(gb, vlc_scalefactors.table, 7, 3) - 60;
                    if(offset[1] > 255U) {
                        av_log(ac->avccontext, AV_LOG_ERROR,
                            "%s (%d) out of range.\n", sf_str[1], offset[1]);
                        return -1;
                    }
                    sf[idx]  = -ff_aac_pow2sf_tab[ offset[1] + sf_offset + 100];
                }
            }else {
                for(; i < run_end; i++, idx++) {
                    offset[0] += get_vlc2(gb, vlc_scalefactors.table, 7, 3) - 60;
                    if(offset[0] > 255U) {
                        av_log(ac->avccontext, AV_LOG_ERROR,
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
static int decode_pulses(Pulse * pulse, GetBitContext * gb, const uint16_t * swb_offset, int num_swb) {
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
        pulse->pos[i] = get_bits(gb, 5) + pulse->pos[i-1];
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
static int decode_tns(AACContext * ac, TemporalNoiseShaping * tns,
        GetBitContext * gb, const IndividualChannelStream * ics) {
    int w, filt, i, coef_len, coef_res, coef_compress;
    const int is8 = ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE;
    const int tns_max_order = is8 ? 7 : ac->m4ac.object_type == AOT_AAC_MAIN ? 20 : 12;
    for (w = 0; w < ics->num_windows; w++) {
        if ((tns->n_filt[w] = get_bits(gb, 2 - is8))) {
            coef_res = get_bits1(gb);

            for (filt = 0; filt < tns->n_filt[w]; filt++) {
                int tmp2_idx;
                tns->length[w][filt] = get_bits(gb, 6 - 2*is8);

                if ((tns->order[w][filt] = get_bits(gb, 5 - 2*is8)) > tns_max_order) {
                    av_log(ac->avccontext, AV_LOG_ERROR, "TNS filter order %d is greater than maximum %d.",
                           tns->order[w][filt], tns_max_order);
                    tns->order[w][filt] = 0;
                    return -1;
                }
                if (tns->order[w][filt]) {
                    tns->direction[w][filt] = get_bits1(gb);
                    coef_compress = get_bits1(gb);
                    coef_len = coef_res + 3 - coef_compress;
                    tmp2_idx = 2*coef_compress + coef_res;

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
static void decode_mid_side_stereo(ChannelElement * cpe, GetBitContext * gb,
        int ms_present) {
    int idx;
    if (ms_present == 1) {
        for (idx = 0; idx < cpe->ch[0].ics.num_window_groups * cpe->ch[0].ics.max_sfb; idx++)
            cpe->ms_mask[idx] = get_bits1(gb);
    } else if (ms_present == 2) {
        memset(cpe->ms_mask, 1, cpe->ch[0].ics.num_window_groups * cpe->ch[0].ics.max_sfb * sizeof(cpe->ms_mask[0]));
    }
}

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
static int decode_spectrum_and_dequant(AACContext * ac, float coef[1024], GetBitContext * gb, float sf[120],
        int pulse_present, const Pulse * pulse, const IndividualChannelStream * ics, enum BandType band_type[120]) {
    int i, k, g, idx = 0;
    const int c = 1024/ics->num_windows;
    const uint16_t * offsets = ics->swb_offset;
    float *coef_base = coef;
    static const float sign_lookup[] = { 1.0f, -1.0f };

    for (g = 0; g < ics->num_windows; g++)
        memset(coef + g * 128 + offsets[ics->max_sfb], 0, sizeof(float)*(c - offsets[ics->max_sfb]));

    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb; i++, idx++) {
            const int cur_band_type = band_type[idx];
            const int dim = cur_band_type >= FIRST_PAIR_BT ? 2 : 4;
            const int is_cb_unsigned = IS_CODEBOOK_UNSIGNED(cur_band_type);
            int group;
            if (cur_band_type == ZERO_BT || cur_band_type == INTENSITY_BT2 || cur_band_type == INTENSITY_BT) {
                for (group = 0; group < ics->group_len[g]; group++) {
                    memset(coef + group * 128 + offsets[i], 0, (offsets[i+1] - offsets[i])*sizeof(float));
                }
            }else if (cur_band_type == NOISE_BT) {
                for (group = 0; group < ics->group_len[g]; group++) {
                    float scale;
                    float band_energy = 0;
                    for (k = offsets[i]; k < offsets[i+1]; k++) {
                        ac->random_state  = lcg_random(ac->random_state);
                        coef[group*128+k] = ac->random_state;
                        band_energy += coef[group*128+k]*coef[group*128+k];
                    }
                    scale = sf[idx] / sqrtf(band_energy);
                    for (k = offsets[i]; k < offsets[i+1]; k++) {
                        coef[group*128+k] *= scale;
                    }
                }
            }else {
                for (group = 0; group < ics->group_len[g]; group++) {
                    for (k = offsets[i]; k < offsets[i+1]; k += dim) {
                        const int index = get_vlc2(gb, vlc_spectral[cur_band_type - 1].table, 6, 3);
                        const int coef_tmp_idx = (group << 7) + k;
                        const float *vq_ptr;
                        int j;
                        if(index >= ff_aac_spectral_sizes[cur_band_type - 1]) {
                            av_log(ac->avccontext, AV_LOG_ERROR,
                                "Read beyond end of ff_aac_codebook_vectors[%d][]. index %d >= %d\n",
                                cur_band_type - 1, index, ff_aac_spectral_sizes[cur_band_type - 1]);
                            return -1;
                        }
                        vq_ptr = &ff_aac_codebook_vectors[cur_band_type - 1][index * dim];
                        if (is_cb_unsigned) {
                            if (vq_ptr[0]) coef[coef_tmp_idx    ] = sign_lookup[get_bits1(gb)];
                            if (vq_ptr[1]) coef[coef_tmp_idx + 1] = sign_lookup[get_bits1(gb)];
                            if (dim == 4) {
                                if (vq_ptr[2]) coef[coef_tmp_idx + 2] = sign_lookup[get_bits1(gb)];
                                if (vq_ptr[3]) coef[coef_tmp_idx + 3] = sign_lookup[get_bits1(gb)];
                            }
                        }else {
                            coef[coef_tmp_idx    ] = 1.0f;
                            coef[coef_tmp_idx + 1] = 1.0f;
                            if (dim == 4) {
                                coef[coef_tmp_idx + 2] = 1.0f;
                                coef[coef_tmp_idx + 3] = 1.0f;
                            }
                        }
                        if (cur_band_type == ESC_BT) {
                            for (j = 0; j < 2; j++) {
                                if (vq_ptr[j] == 64.0f) {
                                    int n = 4;
                                    /* The total length of escape_sequence must be < 22 bits according
                                       to the specification (i.e. max is 11111111110xxxxxxxxxx). */
                                    while (get_bits1(gb) && n < 15) n++;
                                    if(n == 15) {
                                        av_log(ac->avccontext, AV_LOG_ERROR, "error in spectral data, ESC overflow\n");
                                        return -1;
                                    }
                                    n = (1<<n) + get_bits(gb, n);
                                    coef[coef_tmp_idx + j] *= cbrtf(n) * n;
                                }else
                                    coef[coef_tmp_idx + j] *= vq_ptr[j];
                            }
                        }else
                        {
                            coef[coef_tmp_idx    ] *= vq_ptr[0];
                            coef[coef_tmp_idx + 1] *= vq_ptr[1];
                            if (dim == 4) {
                                coef[coef_tmp_idx + 2] *= vq_ptr[2];
                                coef[coef_tmp_idx + 3] *= vq_ptr[3];
                            }
                        }
                        coef[coef_tmp_idx    ] *= sf[idx];
                        coef[coef_tmp_idx + 1] *= sf[idx];
                        if (dim == 4) {
                            coef[coef_tmp_idx + 2] *= sf[idx];
                            coef[coef_tmp_idx + 3] *= sf[idx];
                        }
                    }
                }
            }
        }
        coef += ics->group_len[g]<<7;
    }

    if (pulse_present) {
        idx = 0;
        for(i = 0; i < pulse->num_pulse; i++){
            float co  = coef_base[ pulse->pos[i] ];
            while(offsets[idx + 1] <= pulse->pos[i])
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

static av_always_inline float flt16_round(float pf) {
    int exp;
    pf = frexpf(pf, &exp);
    pf = ldexpf(roundf(ldexpf(pf, 8)), exp-8);
    return pf;
}

static av_always_inline float flt16_even(float pf) {
    int exp;
    pf = frexpf(pf, &exp);
    pf = ldexpf(rintf(ldexpf(pf, 8)), exp-8);
    return pf;
}

static av_always_inline float flt16_trunc(float pf) {
    int exp;
    pf = frexpf(pf, &exp);
    pf = ldexpf(truncf(ldexpf(pf, 8)), exp-8);
    return pf;
}

static void predict(AACContext * ac, PredictorState * ps, float* coef, int output_enable) {
    const float a     = 0.953125; // 61.0/64
    const float alpha = 0.90625;  // 29.0/32
    float e0, e1;
    float pv;
    float k1, k2;

    k1 = ps->var0 > 1 ? ps->cor0 * flt16_even(a / ps->var0) : 0;
    k2 = ps->var1 > 1 ? ps->cor1 * flt16_even(a / ps->var1) : 0;

    pv = flt16_round(k1 * ps->r0 + k2 * ps->r1);
    if (output_enable)
        *coef += pv * ac->sf_scale;

    e0 = *coef / ac->sf_scale;
    e1 = e0 - k1 * ps->r0;

    ps->cor1 = flt16_trunc(alpha * ps->cor1 + ps->r1 * e1);
    ps->var1 = flt16_trunc(alpha * ps->var1 + 0.5 * (ps->r1 * ps->r1 + e1 * e1));
    ps->cor0 = flt16_trunc(alpha * ps->cor0 + ps->r0 * e0);
    ps->var0 = flt16_trunc(alpha * ps->var0 + 0.5 * (ps->r0 * ps->r0 + e0 * e0));

    ps->r1 = flt16_trunc(a * (ps->r0 - k1 * e0));
    ps->r0 = flt16_trunc(a * e0);
}

/**
 * Apply AAC-Main style frequency domain prediction.
 */
static void apply_prediction(AACContext * ac, SingleChannelElement * sce) {
    int sfb, k;

    if (!sce->ics.predictor_initialized) {
        reset_all_predictors(sce->predictor_state);
        sce->ics.predictor_initialized = 1;
    }

    if (sce->ics.window_sequence[0] != EIGHT_SHORT_SEQUENCE) {
        for (sfb = 0; sfb < ff_aac_pred_sfb_max[ac->m4ac.sampling_index]; sfb++) {
            for (k = sce->ics.swb_offset[sfb]; k < sce->ics.swb_offset[sfb + 1]; k++) {
                predict(ac, &sce->predictor_state[k], &sce->coeffs[k],
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
static int decode_ics(AACContext * ac, SingleChannelElement * sce, GetBitContext * gb, int common_window, int scale_flag) {
    Pulse pulse;
    TemporalNoiseShaping * tns = &sce->tns;
    IndividualChannelStream * ics = &sce->ics;
    float * out = sce->coeffs;
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
                av_log(ac->avccontext, AV_LOG_ERROR, "Pulse tool not allowed in eight short sequence.\n");
                return -1;
            }
            if (decode_pulses(&pulse, gb, ics->swb_offset, ics->num_swb)) {
                av_log(ac->avccontext, AV_LOG_ERROR, "Pulse data corrupt or invalid.\n");
                return -1;
            }
        }
        if ((tns->present = get_bits1(gb)) && decode_tns(ac, tns, gb, ics))
            return -1;
        if (get_bits1(gb)) {
            ff_log_missing_feature(ac->avccontext, "SSR", 1);
            return -1;
        }
    }

    if (decode_spectrum_and_dequant(ac, out, gb, sce->sf, pulse_present, &pulse, ics, sce->band_type) < 0)
        return -1;

    if(ac->m4ac.object_type == AOT_AAC_MAIN && !common_window)
        apply_prediction(ac, sce);

    return 0;
}

/**
 * Mid/Side stereo decoding; reference: 4.6.8.1.3.
 */
static void apply_mid_side_stereo(ChannelElement * cpe) {
    const IndividualChannelStream * ics = &cpe->ch[0].ics;
    float *ch0 = cpe->ch[0].coeffs;
    float *ch1 = cpe->ch[1].coeffs;
    int g, i, k, group, idx = 0;
    const uint16_t * offsets = ics->swb_offset;
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb; i++, idx++) {
            if (cpe->ms_mask[idx] &&
                cpe->ch[0].band_type[idx] < NOISE_BT && cpe->ch[1].band_type[idx] < NOISE_BT) {
                for (group = 0; group < ics->group_len[g]; group++) {
                    for (k = offsets[i]; k < offsets[i+1]; k++) {
                        float tmp = ch0[group*128 + k] - ch1[group*128 + k];
                        ch0[group*128 + k] += ch1[group*128 + k];
                        ch1[group*128 + k] = tmp;
                    }
                }
            }
        }
        ch0 += ics->group_len[g]*128;
        ch1 += ics->group_len[g]*128;
    }
}

/**
 * intensity stereo decoding; reference: 4.6.8.2.3
 *
 * @param   ms_present  Indicates mid/side stereo presence. [0] mask is all 0s;
 *                      [1] mask is decoded from bitstream; [2] mask is all 1s;
 *                      [3] reserved for scalable AAC
 */
static void apply_intensity_stereo(ChannelElement * cpe, int ms_present) {
    const IndividualChannelStream * ics = &cpe->ch[1].ics;
    SingleChannelElement * sce1 = &cpe->ch[1];
    float *coef0 = cpe->ch[0].coeffs, *coef1 = cpe->ch[1].coeffs;
    const uint16_t * offsets = ics->swb_offset;
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
                        for (k = offsets[i]; k < offsets[i+1]; k++)
                            coef1[group*128 + k] = scale * coef0[group*128 + k];
                }
            } else {
                int bt_run_end = sce1->band_type_run_end[idx];
                idx += bt_run_end - i;
                i    = bt_run_end;
            }
        }
        coef0 += ics->group_len[g]*128;
        coef1 += ics->group_len[g]*128;
    }
}

/**
 * Decode a channel_pair_element; reference: table 4.4.
 *
 * @param   elem_id Identifies the instance of a syntax element.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_cpe(AACContext * ac, GetBitContext * gb, int elem_id) {
    int i, ret, common_window, ms_present = 0;
    ChannelElement * cpe;

    cpe = ac->che[TYPE_CPE][elem_id];
    common_window = get_bits1(gb);
    if (common_window) {
        if (decode_ics_info(ac, &cpe->ch[0].ics, gb, 1))
            return -1;
        i = cpe->ch[1].ics.use_kb_window[0];
        cpe->ch[1].ics = cpe->ch[0].ics;
        cpe->ch[1].ics.use_kb_window[1] = i;
        ms_present = get_bits(gb, 2);
        if(ms_present == 3) {
            av_log(ac->avccontext, AV_LOG_ERROR, "ms_present = 3 is reserved.\n");
            return -1;
        } else if(ms_present)
            decode_mid_side_stereo(cpe, gb, ms_present);
    }
    if ((ret = decode_ics(ac, &cpe->ch[0], gb, common_window, 0)))
        return ret;
    if ((ret = decode_ics(ac, &cpe->ch[1], gb, common_window, 0)))
        return ret;

    if (common_window) {
        if (ms_present)
            apply_mid_side_stereo(cpe);
        if (ac->m4ac.object_type == AOT_AAC_MAIN) {
            apply_prediction(ac, &cpe->ch[0]);
            apply_prediction(ac, &cpe->ch[1]);
        }
    }

    apply_intensity_stereo(cpe, ms_present);
    return 0;
}

/**
 * Decode coupling_channel_element; reference: table 4.8.
 *
 * @param   elem_id Identifies the instance of a syntax element.
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_cce(AACContext * ac, GetBitContext * gb, ChannelElement * che) {
    int num_gain = 0;
    int c, g, sfb, ret;
    int sign;
    float scale;
    SingleChannelElement * sce = &che->ch[0];
    ChannelCoupling * coup     = &che->coup;

    coup->coupling_point = 2*get_bits1(gb);
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
    coup->coupling_point += get_bits1(gb);

    if (coup->coupling_point == 2) {
        av_log(ac->avccontext, AV_LOG_ERROR,
            "Independently switched CCE with 'invalid' domain signalled.\n");
        memset(coup, 0, sizeof(ChannelCoupling));
        return -1;
    }

    sign = get_bits(gb, 1);
    scale = pow(2., pow(2., (int)get_bits(gb, 2) - 3));

    if ((ret = decode_ics(ac, sce, gb, 0, 0)))
        return ret;

    for (c = 0; c < num_gain; c++) {
        int idx = 0;
        int cge = 1;
        int gain = 0;
        float gain_cache = 1.;
        if (c) {
            cge = coup->coupling_point == AFTER_IMDCT ? 1 : get_bits1(gb);
            gain = cge ? get_vlc2(gb, vlc_scalefactors.table, 7, 3) - 60: 0;
            gain_cache = pow(scale, -gain);
        }
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
                            gain_cache = pow(scale, -t) * s;
                        }
                    }
                    coup->gain[c][idx] = gain_cache;
                }
            }
        }
    }
    return 0;
}

/**
 * Decode Spectral Band Replication extension data; reference: table 4.55.
 *
 * @param   crc flag indicating the presence of CRC checksum
 * @param   cnt length of TYPE_FIL syntactic element in bytes
 *
 * @return  Returns number of bytes consumed from the TYPE_FIL element.
 */
static int decode_sbr_extension(AACContext * ac, GetBitContext * gb, int crc, int cnt) {
    // TODO : sbr_extension implementation
    ff_log_missing_feature(ac->avccontext, "SBR", 0);
    skip_bits_long(gb, 8*cnt - 4); // -4 due to reading extension type
    return cnt;
}

/**
 * Parse whether channels are to be excluded from Dynamic Range Compression; reference: table 4.53.
 *
 * @return  Returns number of bytes consumed.
 */
static int decode_drc_channel_exclusions(DynamicRangeControl *che_drc, GetBitContext * gb) {
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
static int decode_dynamic_range(DynamicRangeControl *che_drc, GetBitContext * gb, int cnt) {
    int n = 1;
    int drc_num_bands = 1;
    int i;

    /* pce_tag_present? */
    if(get_bits1(gb)) {
        che_drc->pce_instance_tag  = get_bits(gb, 4);
        skip_bits(gb, 4); // tag_reserved_bits
        n++;
    }

    /* excluded_chns_present? */
    if(get_bits1(gb)) {
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
static int decode_extension_payload(AACContext * ac, GetBitContext * gb, int cnt) {
    int crc_flag = 0;
    int res = cnt;
    switch (get_bits(gb, 4)) { // extension type
        case EXT_SBR_DATA_CRC:
            crc_flag++;
        case EXT_SBR_DATA:
            res = decode_sbr_extension(ac, gb, crc_flag, cnt);
            break;
        case EXT_DYNAMIC_RANGE:
            res = decode_dynamic_range(&ac->che_drc, gb, cnt);
            break;
        case EXT_FILL:
        case EXT_FILL_DATA:
        case EXT_DATA_ELEMENT:
        default:
            skip_bits_long(gb, 8*cnt - 4);
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
static void apply_tns(float coef[1024], TemporalNoiseShaping * tns, IndividualChannelStream * ics, int decode) {
    const int mmm = FFMIN(ics->tns_max_bands,  ics->max_sfb);
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
                inc = -1; start = end - 1;
            } else {
                inc = 1;
            }
            start += w * 128;

            // ar filter
            for (m = 0; m < size; m++, start += inc)
                for (i = 1; i <= FFMIN(m, order); i++)
                    coef[start] -= coef[start - i*inc] * lpc[i-1];
        }
    }
}

/**
 * Conduct IMDCT and windowing.
 */
static void imdct_and_windowing(AACContext * ac, SingleChannelElement * sce) {
    IndividualChannelStream * ics = &sce->ics;
    float * in = sce->coeffs;
    float * out = sce->ret;
    float * saved = sce->saved;
    const float * swindow      = ics->use_kb_window[0] ? ff_aac_kbd_short_128 : ff_sine_128;
    const float * lwindow_prev = ics->use_kb_window[1] ? ff_aac_kbd_long_1024 : ff_sine_1024;
    const float * swindow_prev = ics->use_kb_window[1] ? ff_aac_kbd_short_128 : ff_sine_128;
    float * buf = ac->buf_mdct;
    float * temp = ac->temp;
    int i;

    // imdct
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        if (ics->window_sequence[1] == ONLY_LONG_SEQUENCE || ics->window_sequence[1] == LONG_STOP_SEQUENCE)
            av_log(ac->avccontext, AV_LOG_WARNING,
                   "Transition from an ONLY_LONG or LONG_STOP to an EIGHT_SHORT sequence detected. "
                   "If you heard an audible artifact, please submit the sample to the FFmpeg developers.\n");
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
        ac->dsp.vector_fmul_window(    out,               saved,            buf,         lwindow_prev, ac->add_bias, 512);
    } else {
        for (i = 0; i < 448; i++)
            out[i] = saved[i] + ac->add_bias;

        if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
            ac->dsp.vector_fmul_window(out + 448 + 0*128, saved + 448,      buf + 0*128, swindow_prev, ac->add_bias, 64);
            ac->dsp.vector_fmul_window(out + 448 + 1*128, buf + 0*128 + 64, buf + 1*128, swindow,      ac->add_bias, 64);
            ac->dsp.vector_fmul_window(out + 448 + 2*128, buf + 1*128 + 64, buf + 2*128, swindow,      ac->add_bias, 64);
            ac->dsp.vector_fmul_window(out + 448 + 3*128, buf + 2*128 + 64, buf + 3*128, swindow,      ac->add_bias, 64);
            ac->dsp.vector_fmul_window(temp,              buf + 3*128 + 64, buf + 4*128, swindow,      ac->add_bias, 64);
            memcpy(                    out + 448 + 4*128, temp, 64 * sizeof(float));
        } else {
            ac->dsp.vector_fmul_window(out + 448,         saved + 448,      buf,         swindow_prev, ac->add_bias, 64);
            for (i = 576; i < 1024; i++)
                out[i] = buf[i-512] + ac->add_bias;
        }
    }

    // buffer update
    if (ics->window_sequence[0] == EIGHT_SHORT_SEQUENCE) {
        for (i = 0; i < 64; i++)
            saved[i] = temp[64 + i] - ac->add_bias;
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
static void apply_dependent_coupling(AACContext * ac, SingleChannelElement * target, ChannelElement * cce, int index) {
    IndividualChannelStream * ics = &cce->ch[0].ics;
    const uint16_t * offsets = ics->swb_offset;
    float * dest = target->coeffs;
    const float * src = cce->ch[0].coeffs;
    int g, i, group, k, idx = 0;
    if(ac->m4ac.object_type == AOT_AAC_LTP) {
        av_log(ac->avccontext, AV_LOG_ERROR,
               "Dependent coupling is not supported together with LTP\n");
        return;
    }
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb; i++, idx++) {
            if (cce->ch[0].band_type[idx] != ZERO_BT) {
                for (group = 0; group < ics->group_len[g]; group++) {
                    for (k = offsets[i]; k < offsets[i+1]; k++) {
                        // XXX dsputil-ize
                        dest[group*128+k] += cce->coup.gain[index][idx] * src[group*128+k];
                    }
                }
            }
        }
        dest += ics->group_len[g]*128;
        src  += ics->group_len[g]*128;
    }
}

/**
 * Apply independent channel coupling (applied after IMDCT).
 *
 * @param   index   index into coupling gain array
 */
static void apply_independent_coupling(AACContext * ac, SingleChannelElement * target, ChannelElement * cce, int index) {
    int i;
    for (i = 0; i < 1024; i++)
        target->ret[i] += cce->coup.gain[index][0] * (cce->ch[0].ret[i] - ac->add_bias);
}

/**
 * channel coupling transformation interface
 *
 * @param   index   index into coupling gain array
 * @param   apply_coupling_method   pointer to (in)dependent coupling function
 */
static void apply_channel_coupling(AACContext * ac, ChannelElement * cc,
        enum RawDataBlockType type, int elem_id, enum CouplingPoint coupling_point,
        void (*apply_coupling_method)(AACContext * ac, SingleChannelElement * target, ChannelElement * cce, int index))
{
    int i, c;

    for (i = 0; i < MAX_ELEM_ID; i++) {
        ChannelElement *cce = ac->che[TYPE_CCE][i];
        int index = 0;

        if (cce && cce->coup.coupling_point == coupling_point) {
            ChannelCoupling * coup = &cce->coup;

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
static void spectral_to_sample(AACContext * ac) {
    int i, type;
    for(type = 3; type >= 0; type--) {
        for (i = 0; i < MAX_ELEM_ID; i++) {
            ChannelElement *che = ac->che[type][i];
            if(che) {
                if(type <= TYPE_CPE)
                    apply_channel_coupling(ac, che, type, i, BEFORE_TNS, apply_dependent_coupling);
                if(che->ch[0].tns.present)
                    apply_tns(che->ch[0].coeffs, &che->ch[0].tns, &che->ch[0].ics, 1);
                if(che->ch[1].tns.present)
                    apply_tns(che->ch[1].coeffs, &che->ch[1].tns, &che->ch[1].ics, 1);
                if(type <= TYPE_CPE)
                    apply_channel_coupling(ac, che, type, i, BETWEEN_TNS_AND_IMDCT, apply_dependent_coupling);
                if(type != TYPE_CCE || che->coup.coupling_point == AFTER_IMDCT)
                    imdct_and_windowing(ac, &che->ch[0]);
                if(type == TYPE_CPE)
                    imdct_and_windowing(ac, &che->ch[1]);
                if(type <= TYPE_CCE)
                    apply_channel_coupling(ac, che, type, i, AFTER_IMDCT, apply_independent_coupling);
            }
        }
    }
}

static int parse_adts_frame_header(AACContext * ac, GetBitContext * gb) {

    int size;
    AACADTSHeaderInfo hdr_info;

    size = ff_aac_parse_header(gb, &hdr_info);
    if (size > 0) {
        if (hdr_info.chan_config)
            ac->m4ac.chan_config = hdr_info.chan_config;
        ac->m4ac.sample_rate     = hdr_info.sample_rate;
        ac->m4ac.sampling_index  = hdr_info.sampling_index;
        ac->m4ac.object_type     = hdr_info.object_type;
    }
    if (hdr_info.num_aac_frames == 1) {
        if (!hdr_info.crc_absent)
            skip_bits(gb, 16);
    } else {
        ff_log_missing_feature(ac->avccontext, "More than one AAC RDB per ADTS frame is", 0);
        return -1;
    }
    return size;
}

static int aac_decode_frame(AVCodecContext * avccontext, void * data, int * data_size, const uint8_t * buf, int buf_size) {
    AACContext * ac = avccontext->priv_data;
    GetBitContext gb;
    enum RawDataBlockType elem_type;
    int err, elem_id, data_size_tmp;

    init_get_bits(&gb, buf, buf_size*8);

    if (show_bits(&gb, 12) == 0xfff) {
        if ((err = parse_adts_frame_header(ac, &gb)) < 0) {
            av_log(avccontext, AV_LOG_ERROR, "Error decoding AAC frame header.\n");
            return -1;
        }
    }

    // parse
    while ((elem_type = get_bits(&gb, 3)) != TYPE_END) {
        elem_id = get_bits(&gb, 4);
        err = -1;

        if(elem_type == TYPE_SCE && elem_id == 1 &&
                !ac->che[TYPE_SCE][elem_id] && ac->che[TYPE_LFE][0]) {
            /* Some streams incorrectly code 5.1 audio as SCE[0] CPE[0] CPE[1] SCE[1]
               instead of SCE[0] CPE[0] CPE[0] LFE[0]. If we seem to have
               encountered such a stream, transfer the LFE[0] element to SCE[1] */
            ac->che[TYPE_SCE][elem_id] = ac->che[TYPE_LFE][0];
            ac->che[TYPE_LFE][0] = NULL;
        }
        if(elem_type < TYPE_DSE) {
            if(!ac->che[elem_type][elem_id])
                return -1;
            if(elem_type != TYPE_CCE)
                ac->che[elem_type][elem_id]->coup.coupling_point = 4;
        }

        switch (elem_type) {

        case TYPE_SCE:
            err = decode_ics(ac, &ac->che[TYPE_SCE][elem_id]->ch[0], &gb, 0, 0);
            break;

        case TYPE_CPE:
            err = decode_cpe(ac, &gb, elem_id);
            break;

        case TYPE_CCE:
            err = decode_cce(ac, &gb, ac->che[TYPE_CCE][elem_id]);
            break;

        case TYPE_LFE:
            err = decode_ics(ac, &ac->che[TYPE_LFE][elem_id]->ch[0], &gb, 0, 0);
            break;

        case TYPE_DSE:
            skip_data_stream_element(&gb);
            err = 0;
            break;

        case TYPE_PCE:
        {
            enum ChannelPosition new_che_pos[4][MAX_ELEM_ID];
            memset(new_che_pos, 0, 4 * MAX_ELEM_ID * sizeof(new_che_pos[0][0]));
            if((err = decode_pce(ac, new_che_pos, &gb)))
                break;
            err = output_configure(ac, ac->che_pos, new_che_pos);
            break;
        }

        case TYPE_FIL:
            if (elem_id == 15)
                elem_id += get_bits(&gb, 8) - 1;
            while (elem_id > 0)
                elem_id -= decode_extension_payload(ac, &gb, elem_id);
            err = 0; /* FIXME */
            break;

        default:
            err = -1; /* should not happen, but keeps compiler happy */
            break;
        }

        if(err)
            return err;
    }

    spectral_to_sample(ac);

    if (!ac->is_saved) {
        ac->is_saved = 1;
        *data_size = 0;
        return buf_size;
    }

    data_size_tmp = 1024 * avccontext->channels * sizeof(int16_t);
    if(*data_size < data_size_tmp) {
        av_log(avccontext, AV_LOG_ERROR,
               "Output buffer too small (%d) or trying to output too many samples (%d) for this frame.\n",
               *data_size, data_size_tmp);
        return -1;
    }
    *data_size = data_size_tmp;

    ac->dsp.float_to_int16_interleave(data, (const float **)ac->output_data, 1024, avccontext->channels);

    return buf_size;
}

static av_cold int aac_decode_close(AVCodecContext * avccontext) {
    AACContext * ac = avccontext->priv_data;
    int i, type;

    for (i = 0; i < MAX_ELEM_ID; i++) {
        for(type = 0; type < 4; type++)
            av_freep(&ac->che[type][i]);
    }

    ff_mdct_end(&ac->mdct);
    ff_mdct_end(&ac->mdct_small);
    return 0 ;
}

AVCodec aac_decoder = {
    "aac",
    CODEC_TYPE_AUDIO,
    CODEC_ID_AAC,
    sizeof(AACContext),
    aac_decode_init,
    NULL,
    aac_decode_close,
    aac_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("Advanced Audio Coding"),
    .sample_fmts = (enum SampleFormat[]){SAMPLE_FMT_S16,SAMPLE_FMT_NONE},
};
