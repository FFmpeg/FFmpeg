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
#include "mpeg4audio.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <string.h>

#ifndef CONFIG_HARDCODED_TABLES
    static float ff_aac_ivquant_tab[IVQUANT_SIZE];
#endif /* CONFIG_HARDCODED_TABLES */

static VLC vlc_scalefactors;
static VLC vlc_spectral[11];


    num_front       = get_bits(gb, 4);
    num_side        = get_bits(gb, 4);
    num_back        = get_bits(gb, 4);
    num_lfe         = get_bits(gb, 2);
    num_assoc_data  = get_bits(gb, 3);
    num_cc          = get_bits(gb, 4);

    newpcs->mono_mixdown_tag   = get_bits1(gb) ? get_bits(gb, 4) : -1;
    newpcs->stereo_mixdown_tag = get_bits1(gb) ? get_bits(gb, 4) : -1;

    if (get_bits1(gb)) {
        newpcs->mixdown_coeff_index = get_bits(gb, 2);
        newpcs->pseudo_surround     = get_bits1(gb);
    }

    program_config_element_parse_tags(newpcs->che_type[ID_CPE], newpcs->che_type[ID_SCE], AAC_CHANNEL_FRONT, gb, num_front);
    program_config_element_parse_tags(newpcs->che_type[ID_CPE], newpcs->che_type[ID_SCE], AAC_CHANNEL_SIDE,  gb, num_side );
    program_config_element_parse_tags(newpcs->che_type[ID_CPE], newpcs->che_type[ID_SCE], AAC_CHANNEL_BACK,  gb, num_back );
    program_config_element_parse_tags(NULL,                     newpcs->che_type[ID_LFE], AAC_CHANNEL_LFE,   gb, num_lfe  );

    skip_bits_long(gb, 4 * num_assoc_data);

    program_config_element_parse_tags(newpcs->che_type[ID_CCE], newpcs->che_type[ID_CCE], AAC_CHANNEL_CC,    gb, num_cc   );

    align_get_bits(gb);

    /* comment field, first byte is length */
    skip_bits_long(gb, 8 * get_bits(gb, 8));

static av_cold int aac_decode_init(AVCodecContext * avccontext) {
    AACContext * ac = avccontext->priv_data;
    int i;

    ac->avccontext = avccontext;

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

static av_cold int aac_decode_close(AVCodecContext * avccontext) {
    AACContext * ac = avccontext->priv_data;
    int i, j;

    for (i = 0; i < MAX_TAGID; i++) {
        for(j = 0; j < 4; j++)
            av_freep(&ac->che[j][i]);
    }

    ff_mdct_end(&ac->mdct);
    ff_mdct_end(&ac->mdct_small);
    av_freep(&ac->interleaved_output);
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
};
