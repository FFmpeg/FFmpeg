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

static av_cold int aac_decode_init(AVCodecContext * avccontext) {
    AACContext * ac = avccontext->priv_data;
    int i;

    ac->avccontext = avccontext;

    if (avccontext->extradata_size <= 0 ||
        decode_audio_specific_config(ac, avccontext->extradata, avccontext->extradata_size))
        return -1;

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

    int byte_align = get_bits1(gb);
    int count = get_bits(gb, 8);
    if (count == 255)
        count += get_bits(gb, 8);
    if (byte_align)
        align_get_bits(gb);
    skip_bits_long(gb, 8 * count);
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

 *
 * @param   mix_gain            channel gain (Not used by AAC bitstream.)
 * @param   global_gain         first scalefactor value as scalefactors are differentially coded
 * @param   band_type           array of the used band type
 * @param   band_type_run_end   array of the last scalefactor band of a band type run
 * @param   sf                  array of scalefactors or intensity stereo positions
 *
 * @return  Returns error status. 0 - OK, !0 - error
 */
static int decode_scalefactors(AACContext * ac, float sf[120], GetBitContext * gb,
        float mix_gain, unsigned int global_gain, IndividualChannelStream * ics,
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
                    sf[idx] *= mix_gain;
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
                    sf[idx] *= mix_gain;
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
                    sf[idx] *= mix_gain;
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
 * Parse Spectral Band Replication extension data; reference: table 4.55.
 *
 * @param   crc flag indicating the presence of CRC checksum
 * @param   cnt length of TYPE_FIL syntactic element in bytes
 * @return  Returns number of bytes consumed from the TYPE_FIL element.
 */
static int decode_sbr_extension(AACContext * ac, GetBitContext * gb, int crc, int cnt) {
    // TODO : sbr_extension implementation
    av_log(ac->avccontext, AV_LOG_DEBUG, "aac: SBR not yet supported.\n");
    skip_bits_long(gb, 8*cnt - 4); // -4 due to reading extension type
    return cnt;
}

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
                float gain = cc->coup.gain[index][idx] * sce->mixing_gain;
                for (group = 0; group < ics->group_len[g]; group++) {
                    for (k = offsets[i]; k < offsets[i+1]; k++) {
                        // XXX dsputil-ize
                        dest[group*128+k] += gain * src[group*128+k];
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
    float gain = cc->coup.gain[index][0] * sce->mixing_gain;
    for (i = 0; i < 1024; i++)
        sce->ret[i] += gain * (cc->ch[0].ret[i] - ac->add_bias);
}

static av_cold int aac_decode_close(AVCodecContext * avccontext) {
    AACContext * ac = avccontext->priv_data;
    int i, j;

    for (i = 0; i < MAX_ELEM_ID; i++) {
        for(j = 0; j < 4; j++)
            av_freep(&ac->che[j][i]);
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
