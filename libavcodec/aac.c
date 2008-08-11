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
 * @file aac.c
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
 * N                    frequency domain prediction
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
#include "bitstream.h"
#include "dsputil.h"

#include "aac.h"
#include "aactab.h"
#include "aacdectab.h"
#include "mpeg4audio.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <string.h>

#ifndef CONFIG_HARDCODED_TABLES
    static float ff_aac_ivquant_tab[IVQUANT_SIZE];
    static float ff_aac_pow2sf_tab[316];
#endif /* CONFIG_HARDCODED_TABLES */

static VLC vlc_scalefactors;
static VLC vlc_spectral[11];


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
    int num_front, num_side, num_back, num_lfe, num_assoc_data, num_cc;

    skip_bits(gb, 2);  // object_type

    ac->m4ac.sampling_index = get_bits(gb, 4);
    if(ac->m4ac.sampling_index > 11) {
        av_log(ac->avccontext, AV_LOG_ERROR, "invalid sampling rate index %d\n", ac->m4ac.sampling_index);
        return -1;
    }
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

static av_cold int aac_decode_init(AVCodecContext * avccontext) {
    AACContext * ac = avccontext->priv_data;
    int i;

    ac->avccontext = avccontext;

    if (avccontext->extradata_size <= 0 ||
        decode_audio_specific_config(ac, avccontext->extradata, avccontext->extradata_size))
        return -1;

    avccontext->sample_fmt  = SAMPLE_FMT_S16;
    avccontext->sample_rate = ac->m4ac.sample_rate;
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

#ifndef CONFIG_HARDCODED_TABLES
    for (i = 1 - IVQUANT_SIZE/2; i < IVQUANT_SIZE/2; i++)
        ff_aac_ivquant_tab[i + IVQUANT_SIZE/2 - 1] =  cbrt(fabs(i)) * i;
    for (i = 0; i < 316; i++)
        ff_aac_pow2sf_tab[i] = pow(2, (i - 200)/4.);
#endif /* CONFIG_HARDCODED_TABLES */

    INIT_VLC_STATIC(&vlc_scalefactors, 7, sizeof(ff_aac_scalefactor_code)/sizeof(ff_aac_scalefactor_code[0]),
        ff_aac_scalefactor_bits, sizeof(ff_aac_scalefactor_bits[0]), sizeof(ff_aac_scalefactor_bits[0]),
        ff_aac_scalefactor_code, sizeof(ff_aac_scalefactor_code[0]), sizeof(ff_aac_scalefactor_code[0]),
        352);

    ff_mdct_init(&ac->mdct, 11, 1);
    ff_mdct_init(&ac->mdct_small, 8, 1);
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

    return 0;
}

/**
 * inverse quantization
 *
 * @param   a   quantized value to be dequantized
 * @return  Returns dequantized value.
 */
static inline float ivquant(int a) {
    if (a + (unsigned int)IVQUANT_SIZE/2 - 1 < (unsigned int)IVQUANT_SIZE - 1)
        return ff_aac_ivquant_tab[a + IVQUANT_SIZE/2 - 1];
    else
        return cbrtf(fabsf(a)) * a;
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
    ics->intensity_present = 0;
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb;) {
            int run_end = band_type_run_end[idx];
            if (band_type[idx] == ZERO_BT) {
                for(; i < run_end; i++, idx++)
                    sf[idx] = 0.;
            }else if((band_type[idx] == INTENSITY_BT) || (band_type[idx] == INTENSITY_BT2)) {
                ics->intensity_present = 1;
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
                    sf[idx]  = -ff_aac_pow2sf_tab[ offset[1] + sf_offset];
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
static void decode_pulses(Pulse * pulse, GetBitContext * gb) {
    int i;
    pulse->num_pulse = get_bits(gb, 2) + 1;
    pulse->start = get_bits(gb, 6);
    for (i = 0; i < pulse->num_pulse; i++) {
        pulse->offset[i] = get_bits(gb, 5);
        pulse->amp   [i] = get_bits(gb, 4);
    }
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

/**
 * Add pulses with particular amplitudes to the quantized spectral data; reference: 4.6.3.3.
 *
 * @param   pulse   pointer to pulse data struct
 * @param   icoef   array of quantized spectral data
 */
static void add_pulses(int icoef[1024], const Pulse * pulse, const IndividualChannelStream * ics) {
    int i, off = ics->swb_offset[pulse->start];
    for (i = 0; i < pulse->num_pulse; i++) {
        int ic;
        off += pulse->offset[i];
        ic = (icoef[off] - 1)>>31;
        icoef[off] += (pulse->amp[i]^ic) - ic;
    }
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
    int icoeffs[1024];
    Pulse pulse;
    TemporalNoiseShaping * tns = &sce->tns;
    IndividualChannelStream * ics = &sce->ics;
    float * out = sce->coeffs;
    int global_gain, pulse_present = 0;

    /* These two assignments are to silence some GCC warnings about the
     * variables being used uninitialised when in fact they always are.
     */
    pulse.num_pulse = 0;
    pulse.start     = 0;

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
            decode_pulses(&pulse, gb);
        }
        if ((tns->present = get_bits1(gb)) && decode_tns(ac, tns, gb, ics))
            return -1;
        if (get_bits1(gb)) {
            av_log_missing_feature(ac->avccontext, "SSR", 1);
            return -1;
        }
    }

    if (decode_spectrum(ac, icoeffs, gb, ics, sce->band_type) < 0)
        return -1;
    if (pulse_present)
        add_pulses(icoeffs, &pulse, ics);
    dequant(ac, out, icoeffs, sce->sf, ics, sce->band_type);
    return 0;
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

    if (common_window && ms_present)
        apply_mid_side_stereo(cpe);

    if (cpe->ch[1].ics.intensity_present)
        apply_intensity_stereo(cpe, ms_present);
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
    av_log_missing_feature(ac->avccontext, "SBR", 0);
    skip_bits_long(gb, 8*cnt - 4); // -4 due to reading extension type
    return cnt;
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
 * Conduct IMDCT and windowing.
 */
static void imdct_and_windowing(AACContext * ac, SingleChannelElement * sce) {
    IndividualChannelStream * ics = &sce->ics;
    float * in = sce->coeffs;
    float * out = sce->ret;
    float * saved = sce->saved;
    const float * lwindow      = ics->use_kb_window[0] ? ff_aac_kbd_long_1024 : ff_aac_sine_long_1024;
    const float * swindow      = ics->use_kb_window[0] ? ff_aac_kbd_short_128 : ff_aac_sine_short_128;
    const float * lwindow_prev = ics->use_kb_window[1] ? ff_aac_kbd_long_1024 : ff_aac_sine_long_1024;
    const float * swindow_prev = ics->use_kb_window[1] ? ff_aac_kbd_short_128 : ff_aac_sine_short_128;
    float * buf = ac->buf_mdct;
    int i;

/**
 * Apply dependent channel coupling (applied before IMDCT).
 *
 * @param   index   index into coupling gain array
 */
static void apply_dependent_coupling(AACContext * ac, SingleChannelElement * sce, ChannelElement * cc, int index) {
    IndividualChannelStream * ics = &cc->ch[0].ics;
    const uint16_t * offsets = ics->swb_offset;
    float * dest = sce->coeffs;
    const float * src = cc->ch[0].coeffs;
    int g, i, group, k, idx = 0;
    if(ac->m4ac.object_type == AOT_AAC_LTP) {
        av_log(ac->avccontext, AV_LOG_ERROR,
               "Dependent coupling is not supported together with LTP\n");
        return;
    }
    for (g = 0; g < ics->num_window_groups; g++) {
        for (i = 0; i < ics->max_sfb; i++, idx++) {
            if (cc->ch[0].band_type[idx] != ZERO_BT) {
                for (group = 0; group < ics->group_len[g]; group++) {
                    for (k = offsets[i]; k < offsets[i+1]; k++) {
                        // XXX dsputil-ize
                        dest[group*128+k] += cc->coup.gain[index][idx] * src[group*128+k];
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
static void apply_independent_coupling(AACContext * ac, SingleChannelElement * sce, ChannelElement * cc, int index) {
    int i;
    for (i = 0; i < 1024; i++)
        sce->ret[i] += cc->coup.gain[index][0] * (cc->ch[0].ret[i] - ac->add_bias);
}

    if (!ac->is_saved) {
        ac->is_saved = 1;
        *data_size = 0;
        return 0;
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
