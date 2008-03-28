/*
 * AC-3 Audio Decoder
 * This code is developed as part of Google Summer of Code 2006 Program.
 *
 * Copyright (c) 2006 Kartikey Mahendra BHATT (bhattkm at gmail dot com).
 * Copyright (c) 2007 Justin Ruggles
 *
 * Portions of this code are derived from liba52
 * http://liba52.sourceforge.net
 * Copyright (C) 2000-2003 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <stddef.h>
#include <math.h>
#include <string.h>

#include "avcodec.h"
#include "ac3_parser.h"
#include "bitstream.h"
#include "crc.h"
#include "dsputil.h"
#include "random.h"

/** Maximum possible frame size when the specification limit is ignored */
#define AC3_MAX_FRAME_SIZE 21695

/**
 * Table of bin locations for rematrixing bands
 * reference: Section 7.5.2 Rematrixing : Frequency Band Definitions
 */
static const uint8_t rematrix_band_tab[5] = { 13, 25, 37, 61, 253 };

/** table for grouping exponents */
static uint8_t exp_ungroup_tab[128][3];


/** tables for ungrouping mantissas */
static int b1_mantissas[32][3];
static int b2_mantissas[128][3];
static int b3_mantissas[8];
static int b4_mantissas[128][2];
static int b5_mantissas[16];

/**
 * Quantization table: levels for symmetric. bits for asymmetric.
 * reference: Table 7.18 Mapping of bap to Quantizer
 */
static const uint8_t quantization_tab[16] = {
    0, 3, 5, 7, 11, 15,
    5, 6, 7, 8, 9, 10, 11, 12, 14, 16
};

/** dynamic range table. converts codes to scale factors. */
static float dynamic_range_tab[256];

/** Adjustments in dB gain */
#define LEVEL_MINUS_3DB         0.7071067811865476
#define LEVEL_MINUS_4POINT5DB   0.5946035575013605
#define LEVEL_MINUS_6DB         0.5000000000000000
#define LEVEL_MINUS_9DB         0.3535533905932738
#define LEVEL_ZERO              0.0000000000000000
#define LEVEL_ONE               1.0000000000000000

static const float gain_levels[6] = {
    LEVEL_ZERO,
    LEVEL_ONE,
    LEVEL_MINUS_3DB,
    LEVEL_MINUS_4POINT5DB,
    LEVEL_MINUS_6DB,
    LEVEL_MINUS_9DB
};

/**
 * Table for center mix levels
 * reference: Section 5.4.2.4 cmixlev
 */
static const uint8_t center_levels[4] = { 2, 3, 4, 3 };

/**
 * Table for surround mix levels
 * reference: Section 5.4.2.5 surmixlev
 */
static const uint8_t surround_levels[4] = { 2, 4, 0, 4 };

/**
 * Table for default stereo downmixing coefficients
 * reference: Section 7.8.2 Downmixing Into Two Channels
 */
static const uint8_t ac3_default_coeffs[8][5][2] = {
    { { 1, 0 }, { 0, 1 },                               },
    { { 2, 2 },                                         },
    { { 1, 0 }, { 0, 1 },                               },
    { { 1, 0 }, { 3, 3 }, { 0, 1 },                     },
    { { 1, 0 }, { 0, 1 }, { 4, 4 },                     },
    { { 1, 0 }, { 3, 3 }, { 0, 1 }, { 5, 5 },           },
    { { 1, 0 }, { 0, 1 }, { 4, 0 }, { 0, 4 },           },
    { { 1, 0 }, { 3, 3 }, { 0, 1 }, { 4, 0 }, { 0, 4 }, },
};

/* override ac3.h to include coupling channel */
#undef AC3_MAX_CHANNELS
#define AC3_MAX_CHANNELS 7
#define CPL_CH 0

#define AC3_OUTPUT_LFEON  8

typedef struct {
    int channel_mode;                       ///< channel mode (acmod)
    int block_switch[AC3_MAX_CHANNELS];     ///< block switch flags
    int dither_flag[AC3_MAX_CHANNELS];      ///< dither flags
    int dither_all;                         ///< true if all channels are dithered
    int cpl_in_use;                         ///< coupling in use
    int channel_in_cpl[AC3_MAX_CHANNELS];   ///< channel in coupling
    int phase_flags_in_use;                 ///< phase flags in use
    int phase_flags[18];                    ///< phase flags
    int cpl_band_struct[18];                ///< coupling band structure
    int num_rematrixing_bands;              ///< number of rematrixing bands
    int rematrixing_flags[4];               ///< rematrixing flags
    int exp_strategy[AC3_MAX_CHANNELS];     ///< exponent strategies
    int snr_offset[AC3_MAX_CHANNELS];       ///< signal-to-noise ratio offsets
    int fast_gain[AC3_MAX_CHANNELS];        ///< fast gain values (signal-to-mask ratio)
    int dba_mode[AC3_MAX_CHANNELS];         ///< delta bit allocation mode
    int dba_nsegs[AC3_MAX_CHANNELS];        ///< number of delta segments
    uint8_t dba_offsets[AC3_MAX_CHANNELS][8]; ///< delta segment offsets
    uint8_t dba_lengths[AC3_MAX_CHANNELS][8]; ///< delta segment lengths
    uint8_t dba_values[AC3_MAX_CHANNELS][8];  ///< delta values for each segment

    int sample_rate;                        ///< sample frequency, in Hz
    int bit_rate;                           ///< stream bit rate, in bits-per-second
    int frame_size;                         ///< current frame size, in bytes

    int channels;                           ///< number of total channels
    int fbw_channels;                       ///< number of full-bandwidth channels
    int lfe_on;                             ///< lfe channel in use
    int lfe_ch;                             ///< index of LFE channel
    int output_mode;                        ///< output channel configuration
    int out_channels;                       ///< number of output channels

    int center_mix_level;                   ///< Center mix level index
    int surround_mix_level;                 ///< Surround mix level index
    float downmix_coeffs[AC3_MAX_CHANNELS][2];  ///< stereo downmix coefficients
    float downmix_coeff_adjust[2];          ///< adjustment needed for each output channel when downmixing
    float dynamic_range[2];                 ///< dynamic range
    int   cpl_coords[AC3_MAX_CHANNELS][18]; ///< coupling coordinates
    int   num_cpl_bands;                    ///< number of coupling bands
    int   num_cpl_subbands;                 ///< number of coupling sub bands
    int   start_freq[AC3_MAX_CHANNELS];     ///< start frequency bin
    int   end_freq[AC3_MAX_CHANNELS];       ///< end frequency bin
    AC3BitAllocParameters bit_alloc_params; ///< bit allocation parameters

    int8_t  dexps[AC3_MAX_CHANNELS][256];   ///< decoded exponents
    uint8_t bap[AC3_MAX_CHANNELS][256];     ///< bit allocation pointers
    int16_t psd[AC3_MAX_CHANNELS][256];     ///< scaled exponents
    int16_t band_psd[AC3_MAX_CHANNELS][50]; ///< interpolated exponents
    int16_t mask[AC3_MAX_CHANNELS][50];     ///< masking curve values

    int fixed_coeffs[AC3_MAX_CHANNELS][256];    ///> fixed-point transform coefficients
    DECLARE_ALIGNED_16(float, transform_coeffs[AC3_MAX_CHANNELS][256]);  ///< transform coefficients
    int downmixed;                              ///< indicates if coeffs are currently downmixed

    /* For IMDCT. */
    MDCTContext imdct_512;                  ///< for 512 sample IMDCT
    MDCTContext imdct_256;                  ///< for 256 sample IMDCT
    DSPContext  dsp;                        ///< for optimization
    float       add_bias;                   ///< offset for float_to_int16 conversion
    float       mul_bias;                   ///< scaling for float_to_int16 conversion

    DECLARE_ALIGNED_16(float, output[AC3_MAX_CHANNELS][256]);       ///< output after imdct transform and windowing
    DECLARE_ALIGNED_16(short, int_output[AC3_MAX_CHANNELS-1][256]); ///< final 16-bit integer output
    DECLARE_ALIGNED_16(float, delay[AC3_MAX_CHANNELS][256]);        ///< delay - added to the next block
    DECLARE_ALIGNED_16(float, tmp_imdct[256]);                      ///< temporary storage for imdct transform
    DECLARE_ALIGNED_16(float, tmp_output[512]);                     ///< temporary storage for output before windowing
    DECLARE_ALIGNED_16(float, window[256]);                         ///< window coefficients

    /* Miscellaneous. */
    GetBitContext gbc;                      ///< bitstream reader
    AVRandomState dith_state;               ///< for dither generation
    AVCodecContext *avctx;                  ///< parent context
    uint8_t *input_buffer;                  ///< temp buffer to prevent overread
} AC3DecodeContext;

/**
 * Symmetrical Dequantization
 * reference: Section 7.3.3 Expansion of Mantissas for Symmetrical Quantization
 *            Tables 7.19 to 7.23
 */
static inline int
symmetric_dequant(int code, int levels)
{
    return ((code - (levels >> 1)) << 24) / levels;
}

/*
 * Initialize tables at runtime.
 */
static av_cold void ac3_tables_init(void)
{
    int i;

    /* generate grouped mantissa tables
       reference: Section 7.3.5 Ungrouping of Mantissas */
    for(i=0; i<32; i++) {
        /* bap=1 mantissas */
        b1_mantissas[i][0] = symmetric_dequant( i / 9     , 3);
        b1_mantissas[i][1] = symmetric_dequant((i % 9) / 3, 3);
        b1_mantissas[i][2] = symmetric_dequant((i % 9) % 3, 3);
    }
    for(i=0; i<128; i++) {
        /* bap=2 mantissas */
        b2_mantissas[i][0] = symmetric_dequant( i / 25     , 5);
        b2_mantissas[i][1] = symmetric_dequant((i % 25) / 5, 5);
        b2_mantissas[i][2] = symmetric_dequant((i % 25) % 5, 5);

        /* bap=4 mantissas */
        b4_mantissas[i][0] = symmetric_dequant(i / 11, 11);
        b4_mantissas[i][1] = symmetric_dequant(i % 11, 11);
    }
    /* generate ungrouped mantissa tables
       reference: Tables 7.21 and 7.23 */
    for(i=0; i<7; i++) {
        /* bap=3 mantissas */
        b3_mantissas[i] = symmetric_dequant(i, 7);
    }
    for(i=0; i<15; i++) {
        /* bap=5 mantissas */
        b5_mantissas[i] = symmetric_dequant(i, 15);
    }

    /* generate dynamic range table
       reference: Section 7.7.1 Dynamic Range Control */
    for(i=0; i<256; i++) {
        int v = (i >> 5) - ((i >> 7) << 3) - 5;
        dynamic_range_tab[i] = powf(2.0f, v) * ((i & 0x1F) | 0x20);
    }

    /* generate exponent tables
       reference: Section 7.1.3 Exponent Decoding */
    for(i=0; i<128; i++) {
        exp_ungroup_tab[i][0] =  i / 25;
        exp_ungroup_tab[i][1] = (i % 25) / 5;
        exp_ungroup_tab[i][2] = (i % 25) % 5;
    }
}


/**
 * AVCodec initialization
 */
static av_cold int ac3_decode_init(AVCodecContext *avctx)
{
    AC3DecodeContext *s = avctx->priv_data;
    s->avctx = avctx;

    ac3_common_init();
    ac3_tables_init();
    ff_mdct_init(&s->imdct_256, 8, 1);
    ff_mdct_init(&s->imdct_512, 9, 1);
    ff_kbd_window_init(s->window, 5.0, 256);
    dsputil_init(&s->dsp, avctx);
    av_init_random(0, &s->dith_state);

    /* set bias values for float to int16 conversion */
    if(s->dsp.float_to_int16 == ff_float_to_int16_c) {
        s->add_bias = 385.0f;
        s->mul_bias = 1.0f;
    } else {
        s->add_bias = 0.0f;
        s->mul_bias = 32767.0f;
    }

    /* allow downmixing to stereo or mono */
    if (avctx->channels > 0 && avctx->request_channels > 0 &&
            avctx->request_channels < avctx->channels &&
            avctx->request_channels <= 2) {
        avctx->channels = avctx->request_channels;
    }
    s->downmixed = 1;

    /* allocate context input buffer */
    if (avctx->error_resilience >= FF_ER_CAREFUL) {
        s->input_buffer = av_mallocz(AC3_MAX_FRAME_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);
        if (!s->input_buffer)
            return AVERROR_NOMEM;
    }

    return 0;
}

/**
 * Parse the 'sync info' and 'bit stream info' from the AC-3 bitstream.
 * GetBitContext within AC3DecodeContext must point to
 * start of the synchronized ac3 bitstream.
 */
static int ac3_parse_header(AC3DecodeContext *s)
{
    AC3HeaderInfo hdr;
    GetBitContext *gbc = &s->gbc;
    int err, i;

    err = ff_ac3_parse_header(gbc->buffer, &hdr);
    if(err)
        return err;

    if(hdr.bitstream_id > 10)
        return AC3_PARSE_ERROR_BSID;

    /* get decoding parameters from header info */
    s->bit_alloc_params.sr_code     = hdr.sr_code;
    s->channel_mode                 = hdr.channel_mode;
    s->lfe_on                       = hdr.lfe_on;
    s->bit_alloc_params.sr_shift    = hdr.sr_shift;
    s->sample_rate                  = hdr.sample_rate;
    s->bit_rate                     = hdr.bit_rate;
    s->channels                     = hdr.channels;
    s->fbw_channels                 = s->channels - s->lfe_on;
    s->lfe_ch                       = s->fbw_channels + 1;
    s->frame_size                   = hdr.frame_size;

    /* set default output to all source channels */
    s->out_channels = s->channels;
    s->output_mode = s->channel_mode;
    if(s->lfe_on)
        s->output_mode |= AC3_OUTPUT_LFEON;

    /* set default mix levels */
    s->center_mix_level   = 3;  // -4.5dB
    s->surround_mix_level = 4;  // -6.0dB

    /* skip over portion of header which has already been read */
    skip_bits(gbc, 16); // skip the sync_word
    skip_bits(gbc, 16); // skip crc1
    skip_bits(gbc, 8);  // skip fscod and frmsizecod
    skip_bits(gbc, 11); // skip bsid, bsmod, and acmod
    if(s->channel_mode == AC3_CHMODE_STEREO) {
        skip_bits(gbc, 2); // skip dsurmod
    } else {
        if((s->channel_mode & 1) && s->channel_mode != AC3_CHMODE_MONO)
            s->center_mix_level = center_levels[get_bits(gbc, 2)];
        if(s->channel_mode & 4)
            s->surround_mix_level = surround_levels[get_bits(gbc, 2)];
    }
    skip_bits1(gbc); // skip lfeon

    /* read the rest of the bsi. read twice for dual mono mode. */
    i = !(s->channel_mode);
    do {
        skip_bits(gbc, 5); // skip dialog normalization
        if (get_bits1(gbc))
            skip_bits(gbc, 8); //skip compression
        if (get_bits1(gbc))
            skip_bits(gbc, 8); //skip language code
        if (get_bits1(gbc))
            skip_bits(gbc, 7); //skip audio production information
    } while (i--);

    skip_bits(gbc, 2); //skip copyright bit and original bitstream bit

    /* skip the timecodes (or extra bitstream information for Alternate Syntax)
       TODO: read & use the xbsi1 downmix levels */
    if (get_bits1(gbc))
        skip_bits(gbc, 14); //skip timecode1 / xbsi1
    if (get_bits1(gbc))
        skip_bits(gbc, 14); //skip timecode2 / xbsi2

    /* skip additional bitstream info */
    if (get_bits1(gbc)) {
        i = get_bits(gbc, 6);
        do {
            skip_bits(gbc, 8);
        } while(i--);
    }

    return 0;
}

/**
 * Set stereo downmixing coefficients based on frame header info.
 * reference: Section 7.8.2 Downmixing Into Two Channels
 */
static void set_downmix_coeffs(AC3DecodeContext *s)
{
    int i;
    float cmix = gain_levels[s->center_mix_level];
    float smix = gain_levels[s->surround_mix_level];

    for(i=0; i<s->fbw_channels; i++) {
        s->downmix_coeffs[i][0] = gain_levels[ac3_default_coeffs[s->channel_mode][i][0]];
        s->downmix_coeffs[i][1] = gain_levels[ac3_default_coeffs[s->channel_mode][i][1]];
    }
    if(s->channel_mode > 1 && s->channel_mode & 1) {
        s->downmix_coeffs[1][0] = s->downmix_coeffs[1][1] = cmix;
    }
    if(s->channel_mode == AC3_CHMODE_2F1R || s->channel_mode == AC3_CHMODE_3F1R) {
        int nf = s->channel_mode - 2;
        s->downmix_coeffs[nf][0] = s->downmix_coeffs[nf][1] = smix * LEVEL_MINUS_3DB;
    }
    if(s->channel_mode == AC3_CHMODE_2F2R || s->channel_mode == AC3_CHMODE_3F2R) {
        int nf = s->channel_mode - 4;
        s->downmix_coeffs[nf][0] = s->downmix_coeffs[nf+1][1] = smix;
    }

    /* calculate adjustment needed for each channel to avoid clipping */
    s->downmix_coeff_adjust[0] = s->downmix_coeff_adjust[1] = 0.0f;
    for(i=0; i<s->fbw_channels; i++) {
        s->downmix_coeff_adjust[0] += s->downmix_coeffs[i][0];
        s->downmix_coeff_adjust[1] += s->downmix_coeffs[i][1];
    }
    s->downmix_coeff_adjust[0] = 1.0f / s->downmix_coeff_adjust[0];
    s->downmix_coeff_adjust[1] = 1.0f / s->downmix_coeff_adjust[1];
}

/**
 * Decode the grouped exponents according to exponent strategy.
 * reference: Section 7.1.3 Exponent Decoding
 */
static void decode_exponents(GetBitContext *gbc, int exp_strategy, int ngrps,
                             uint8_t absexp, int8_t *dexps)
{
    int i, j, grp, group_size;
    int dexp[256];
    int expacc, prevexp;

    /* unpack groups */
    group_size = exp_strategy + (exp_strategy == EXP_D45);
    for(grp=0,i=0; grp<ngrps; grp++) {
        expacc = get_bits(gbc, 7);
        dexp[i++] = exp_ungroup_tab[expacc][0];
        dexp[i++] = exp_ungroup_tab[expacc][1];
        dexp[i++] = exp_ungroup_tab[expacc][2];
    }

    /* convert to absolute exps and expand groups */
    prevexp = absexp;
    for(i=0; i<ngrps*3; i++) {
        prevexp = av_clip(prevexp + dexp[i]-2, 0, 24);
        for(j=0; j<group_size; j++) {
            dexps[(i*group_size)+j] = prevexp;
        }
    }
}

/**
 * Generate transform coefficients for each coupled channel in the coupling
 * range using the coupling coefficients and coupling coordinates.
 * reference: Section 7.4.3 Coupling Coordinate Format
 */
static void uncouple_channels(AC3DecodeContext *s)
{
    int i, j, ch, bnd, subbnd;

    subbnd = -1;
    i = s->start_freq[CPL_CH];
    for(bnd=0; bnd<s->num_cpl_bands; bnd++) {
        do {
            subbnd++;
            for(j=0; j<12; j++) {
                for(ch=1; ch<=s->fbw_channels; ch++) {
                    if(s->channel_in_cpl[ch]) {
                        s->fixed_coeffs[ch][i] = ((int64_t)s->fixed_coeffs[CPL_CH][i] * (int64_t)s->cpl_coords[ch][bnd]) >> 23;
                        if (ch == 2 && s->phase_flags[bnd])
                            s->fixed_coeffs[ch][i] = -s->fixed_coeffs[ch][i];
                    }
                }
                i++;
            }
        } while(s->cpl_band_struct[subbnd]);
    }
}

/**
 * Grouped mantissas for 3-level 5-level and 11-level quantization
 */
typedef struct {
    int b1_mant[3];
    int b2_mant[3];
    int b4_mant[2];
    int b1ptr;
    int b2ptr;
    int b4ptr;
} mant_groups;

/**
 * Get the transform coefficients for a particular channel
 * reference: Section 7.3 Quantization and Decoding of Mantissas
 */
static int get_transform_coeffs_ch(AC3DecodeContext *s, int ch_index, mant_groups *m)
{
    GetBitContext *gbc = &s->gbc;
    int i, gcode, tbap, start, end;
    uint8_t *exps;
    uint8_t *bap;
    int *coeffs;

    exps = s->dexps[ch_index];
    bap = s->bap[ch_index];
    coeffs = s->fixed_coeffs[ch_index];
    start = s->start_freq[ch_index];
    end = s->end_freq[ch_index];

    for (i = start; i < end; i++) {
        tbap = bap[i];
        switch (tbap) {
            case 0:
                coeffs[i] = (av_random(&s->dith_state) & 0x7FFFFF) - 4194304;
                break;

            case 1:
                if(m->b1ptr > 2) {
                    gcode = get_bits(gbc, 5);
                    m->b1_mant[0] = b1_mantissas[gcode][0];
                    m->b1_mant[1] = b1_mantissas[gcode][1];
                    m->b1_mant[2] = b1_mantissas[gcode][2];
                    m->b1ptr = 0;
                }
                coeffs[i] = m->b1_mant[m->b1ptr++];
                break;

            case 2:
                if(m->b2ptr > 2) {
                    gcode = get_bits(gbc, 7);
                    m->b2_mant[0] = b2_mantissas[gcode][0];
                    m->b2_mant[1] = b2_mantissas[gcode][1];
                    m->b2_mant[2] = b2_mantissas[gcode][2];
                    m->b2ptr = 0;
                }
                coeffs[i] = m->b2_mant[m->b2ptr++];
                break;

            case 3:
                coeffs[i] = b3_mantissas[get_bits(gbc, 3)];
                break;

            case 4:
                if(m->b4ptr > 1) {
                    gcode = get_bits(gbc, 7);
                    m->b4_mant[0] = b4_mantissas[gcode][0];
                    m->b4_mant[1] = b4_mantissas[gcode][1];
                    m->b4ptr = 0;
                }
                coeffs[i] = m->b4_mant[m->b4ptr++];
                break;

            case 5:
                coeffs[i] = b5_mantissas[get_bits(gbc, 4)];
                break;

            default: {
                /* asymmetric dequantization */
                int qlevel = quantization_tab[tbap];
                coeffs[i] = get_sbits(gbc, qlevel) << (24 - qlevel);
                break;
            }
        }
        coeffs[i] >>= exps[i];
    }

    return 0;
}

/**
 * Remove random dithering from coefficients with zero-bit mantissas
 * reference: Section 7.3.4 Dither for Zero Bit Mantissas (bap=0)
 */
static void remove_dithering(AC3DecodeContext *s) {
    int ch, i;
    int end=0;
    int *coeffs;
    uint8_t *bap;

    for(ch=1; ch<=s->fbw_channels; ch++) {
        if(!s->dither_flag[ch]) {
            coeffs = s->fixed_coeffs[ch];
            bap = s->bap[ch];
            if(s->channel_in_cpl[ch])
                end = s->start_freq[CPL_CH];
            else
                end = s->end_freq[ch];
            for(i=0; i<end; i++) {
                if(!bap[i])
                    coeffs[i] = 0;
            }
            if(s->channel_in_cpl[ch]) {
                bap = s->bap[CPL_CH];
                for(; i<s->end_freq[CPL_CH]; i++) {
                    if(!bap[i])
                        coeffs[i] = 0;
                }
            }
        }
    }
}

/**
 * Get the transform coefficients.
 */
static int get_transform_coeffs(AC3DecodeContext *s)
{
    int ch, end;
    int got_cplchan = 0;
    mant_groups m;

    m.b1ptr = m.b2ptr = m.b4ptr = 3;

    for (ch = 1; ch <= s->channels; ch++) {
        /* transform coefficients for full-bandwidth channel */
        if (get_transform_coeffs_ch(s, ch, &m))
            return -1;
        /* tranform coefficients for coupling channel come right after the
           coefficients for the first coupled channel*/
        if (s->channel_in_cpl[ch])  {
            if (!got_cplchan) {
                if (get_transform_coeffs_ch(s, CPL_CH, &m)) {
                    av_log(s->avctx, AV_LOG_ERROR, "error in decoupling channels\n");
                    return -1;
                }
                uncouple_channels(s);
                got_cplchan = 1;
            }
            end = s->end_freq[CPL_CH];
        } else {
            end = s->end_freq[ch];
        }
        do
            s->transform_coeffs[ch][end] = 0;
        while(++end < 256);
    }

    /* if any channel doesn't use dithering, zero appropriate coefficients */
    if(!s->dither_all)
        remove_dithering(s);

    return 0;
}

/**
 * Stereo rematrixing.
 * reference: Section 7.5.4 Rematrixing : Decoding Technique
 */
static void do_rematrixing(AC3DecodeContext *s)
{
    int bnd, i;
    int end, bndend;
    int tmp0, tmp1;

    end = FFMIN(s->end_freq[1], s->end_freq[2]);

    for(bnd=0; bnd<s->num_rematrixing_bands; bnd++) {
        if(s->rematrixing_flags[bnd]) {
            bndend = FFMIN(end, rematrix_band_tab[bnd+1]);
            for(i=rematrix_band_tab[bnd]; i<bndend; i++) {
                tmp0 = s->fixed_coeffs[1][i];
                tmp1 = s->fixed_coeffs[2][i];
                s->fixed_coeffs[1][i] = tmp0 + tmp1;
                s->fixed_coeffs[2][i] = tmp0 - tmp1;
            }
        }
    }
}

/**
 * Perform the 256-point IMDCT
 */
static void do_imdct_256(AC3DecodeContext *s, int chindex)
{
    int i, k;
    DECLARE_ALIGNED_16(float, x[128]);
    FFTComplex z[2][64];
    float *o_ptr = s->tmp_output;

    for(i=0; i<2; i++) {
        /* de-interleave coefficients */
        for(k=0; k<128; k++) {
            x[k] = s->transform_coeffs[chindex][2*k+i];
        }

        /* run standard IMDCT */
        s->imdct_256.fft.imdct_calc(&s->imdct_256, o_ptr, x, s->tmp_imdct);

        /* reverse the post-rotation & reordering from standard IMDCT */
        for(k=0; k<32; k++) {
            z[i][32+k].re = -o_ptr[128+2*k];
            z[i][32+k].im = -o_ptr[2*k];
            z[i][31-k].re =  o_ptr[2*k+1];
            z[i][31-k].im =  o_ptr[128+2*k+1];
        }
    }

    /* apply AC-3 post-rotation & reordering */
    for(k=0; k<64; k++) {
        o_ptr[    2*k  ] = -z[0][   k].im;
        o_ptr[    2*k+1] =  z[0][63-k].re;
        o_ptr[128+2*k  ] = -z[0][   k].re;
        o_ptr[128+2*k+1] =  z[0][63-k].im;
        o_ptr[256+2*k  ] = -z[1][   k].re;
        o_ptr[256+2*k+1] =  z[1][63-k].im;
        o_ptr[384+2*k  ] =  z[1][   k].im;
        o_ptr[384+2*k+1] = -z[1][63-k].re;
    }
}

/**
 * Inverse MDCT Transform.
 * Convert frequency domain coefficients to time-domain audio samples.
 * reference: Section 7.9.4 Transformation Equations
 */
static inline void do_imdct(AC3DecodeContext *s, int channels)
{
    int ch;

    for (ch=1; ch<=channels; ch++) {
        if (s->block_switch[ch]) {
            do_imdct_256(s, ch);
        } else {
            s->imdct_512.fft.imdct_calc(&s->imdct_512, s->tmp_output,
                                        s->transform_coeffs[ch], s->tmp_imdct);
        }
        /* For the first half of the block, apply the window, add the delay
           from the previous block, and send to output */
        s->dsp.vector_fmul_add_add(s->output[ch-1], s->tmp_output,
                                     s->window, s->delay[ch-1], 0, 256, 1);
        /* For the second half of the block, apply the window and store the
           samples to delay, to be combined with the next block */
        s->dsp.vector_fmul_reverse(s->delay[ch-1], s->tmp_output+256,
                                   s->window, 256);
    }
}

/**
 * Downmix the output to mono or stereo.
 */
static void ac3_downmix(AC3DecodeContext *s,
                        float samples[AC3_MAX_CHANNELS][256], int ch_offset)
{
    int i, j;
    float v0, v1;

    for(i=0; i<256; i++) {
        v0 = v1 = 0.0f;
        for(j=0; j<s->fbw_channels; j++) {
            v0 += samples[j+ch_offset][i] * s->downmix_coeffs[j][0];
            v1 += samples[j+ch_offset][i] * s->downmix_coeffs[j][1];
        }
        v0 *= s->downmix_coeff_adjust[0];
        v1 *= s->downmix_coeff_adjust[1];
        if(s->output_mode == AC3_CHMODE_MONO) {
            samples[ch_offset][i] = (v0 + v1) * LEVEL_MINUS_3DB;
        } else if(s->output_mode == AC3_CHMODE_STEREO) {
            samples[  ch_offset][i] = v0;
            samples[1+ch_offset][i] = v1;
        }
    }
}

/**
 * Upmix delay samples from stereo to original channel layout.
 */
static void ac3_upmix_delay(AC3DecodeContext *s)
{
    int channel_data_size = sizeof(s->delay[0]);
    switch(s->channel_mode) {
        case AC3_CHMODE_DUALMONO:
        case AC3_CHMODE_STEREO:
            /* upmix mono to stereo */
            memcpy(s->delay[1], s->delay[0], channel_data_size);
            break;
        case AC3_CHMODE_2F2R:
            memset(s->delay[3], 0, channel_data_size);
        case AC3_CHMODE_2F1R:
            memset(s->delay[2], 0, channel_data_size);
            break;
        case AC3_CHMODE_3F2R:
            memset(s->delay[4], 0, channel_data_size);
        case AC3_CHMODE_3F1R:
            memset(s->delay[3], 0, channel_data_size);
        case AC3_CHMODE_3F:
            memcpy(s->delay[2], s->delay[1], channel_data_size);
            memset(s->delay[1], 0, channel_data_size);
            break;
    }
}

/**
 * Parse an audio block from AC-3 bitstream.
 */
static int ac3_parse_audio_block(AC3DecodeContext *s, int blk)
{
    int fbw_channels = s->fbw_channels;
    int channel_mode = s->channel_mode;
    int i, bnd, seg, ch;
    int different_transforms;
    int downmix_output;
    GetBitContext *gbc = &s->gbc;
    uint8_t bit_alloc_stages[AC3_MAX_CHANNELS];

    memset(bit_alloc_stages, 0, AC3_MAX_CHANNELS);

    /* block switch flags */
    different_transforms = 0;
    for (ch = 1; ch <= fbw_channels; ch++) {
        s->block_switch[ch] = get_bits1(gbc);
        if(ch > 1 && s->block_switch[ch] != s->block_switch[1])
            different_transforms = 1;
    }

    /* dithering flags */
    s->dither_all = 1;
    for (ch = 1; ch <= fbw_channels; ch++) {
        s->dither_flag[ch] = get_bits1(gbc);
        if(!s->dither_flag[ch])
            s->dither_all = 0;
    }

    /* dynamic range */
    i = !(s->channel_mode);
    do {
        if(get_bits1(gbc)) {
            s->dynamic_range[i] = ((dynamic_range_tab[get_bits(gbc, 8)]-1.0) *
                                  s->avctx->drc_scale)+1.0;
        } else if(blk == 0) {
            s->dynamic_range[i] = 1.0f;
        }
    } while(i--);

    /* coupling strategy */
    if (get_bits1(gbc)) {
        memset(bit_alloc_stages, 3, AC3_MAX_CHANNELS);
        s->cpl_in_use = get_bits1(gbc);
        if (s->cpl_in_use) {
            /* coupling in use */
            int cpl_begin_freq, cpl_end_freq;

            /* determine which channels are coupled */
            for (ch = 1; ch <= fbw_channels; ch++)
                s->channel_in_cpl[ch] = get_bits1(gbc);

            /* phase flags in use */
            if (channel_mode == AC3_CHMODE_STEREO)
                s->phase_flags_in_use = get_bits1(gbc);

            /* coupling frequency range and band structure */
            cpl_begin_freq = get_bits(gbc, 4);
            cpl_end_freq = get_bits(gbc, 4);
            if (3 + cpl_end_freq - cpl_begin_freq < 0) {
                av_log(s->avctx, AV_LOG_ERROR, "3+cplendf = %d < cplbegf = %d\n", 3+cpl_end_freq, cpl_begin_freq);
                return -1;
            }
            s->num_cpl_bands = s->num_cpl_subbands = 3 + cpl_end_freq - cpl_begin_freq;
            s->start_freq[CPL_CH] = cpl_begin_freq * 12 + 37;
            s->end_freq[CPL_CH] = cpl_end_freq * 12 + 73;
            for (bnd = 0; bnd < s->num_cpl_subbands - 1; bnd++) {
                if (get_bits1(gbc)) {
                    s->cpl_band_struct[bnd] = 1;
                    s->num_cpl_bands--;
                }
            }
            s->cpl_band_struct[s->num_cpl_subbands-1] = 0;
        } else {
            /* coupling not in use */
            for (ch = 1; ch <= fbw_channels; ch++)
                s->channel_in_cpl[ch] = 0;
        }
    }

    /* coupling coordinates */
    if (s->cpl_in_use) {
        int cpl_coords_exist = 0;

        for (ch = 1; ch <= fbw_channels; ch++) {
            if (s->channel_in_cpl[ch]) {
                if (get_bits1(gbc)) {
                    int master_cpl_coord, cpl_coord_exp, cpl_coord_mant;
                    cpl_coords_exist = 1;
                    master_cpl_coord = 3 * get_bits(gbc, 2);
                    for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
                        cpl_coord_exp = get_bits(gbc, 4);
                        cpl_coord_mant = get_bits(gbc, 4);
                        if (cpl_coord_exp == 15)
                            s->cpl_coords[ch][bnd] = cpl_coord_mant << 22;
                        else
                            s->cpl_coords[ch][bnd] = (cpl_coord_mant + 16) << 21;
                        s->cpl_coords[ch][bnd] >>= (cpl_coord_exp + master_cpl_coord);
                    }
                }
            }
        }
        /* phase flags */
        if (channel_mode == AC3_CHMODE_STEREO && cpl_coords_exist) {
            for (bnd = 0; bnd < s->num_cpl_bands; bnd++) {
                s->phase_flags[bnd] = s->phase_flags_in_use? get_bits1(gbc) : 0;
            }
        }
    }

    /* stereo rematrixing strategy and band structure */
    if (channel_mode == AC3_CHMODE_STEREO) {
        if (get_bits1(gbc)) {
            s->num_rematrixing_bands = 4;
            if(s->cpl_in_use && s->start_freq[CPL_CH] <= 61)
                s->num_rematrixing_bands -= 1 + (s->start_freq[CPL_CH] == 37);
            for(bnd=0; bnd<s->num_rematrixing_bands; bnd++)
                s->rematrixing_flags[bnd] = get_bits1(gbc);
        }
    }

    /* exponent strategies for each channel */
    s->exp_strategy[CPL_CH] = EXP_REUSE;
    s->exp_strategy[s->lfe_ch] = EXP_REUSE;
    for (ch = !s->cpl_in_use; ch <= s->channels; ch++) {
        if(ch == s->lfe_ch)
            s->exp_strategy[ch] = get_bits(gbc, 1);
        else
            s->exp_strategy[ch] = get_bits(gbc, 2);
        if(s->exp_strategy[ch] != EXP_REUSE)
            bit_alloc_stages[ch] = 3;
    }

    /* channel bandwidth */
    for (ch = 1; ch <= fbw_channels; ch++) {
        s->start_freq[ch] = 0;
        if (s->exp_strategy[ch] != EXP_REUSE) {
            int prev = s->end_freq[ch];
            if (s->channel_in_cpl[ch])
                s->end_freq[ch] = s->start_freq[CPL_CH];
            else {
                int bandwidth_code = get_bits(gbc, 6);
                if (bandwidth_code > 60) {
                    av_log(s->avctx, AV_LOG_ERROR, "bandwidth code = %d > 60", bandwidth_code);
                    return -1;
                }
                s->end_freq[ch] = bandwidth_code * 3 + 73;
            }
            if(blk > 0 && s->end_freq[ch] != prev)
                memset(bit_alloc_stages, 3, AC3_MAX_CHANNELS);
        }
    }
    s->start_freq[s->lfe_ch] = 0;
    s->end_freq[s->lfe_ch] = 7;

    /* decode exponents for each channel */
    for (ch = !s->cpl_in_use; ch <= s->channels; ch++) {
        if (s->exp_strategy[ch] != EXP_REUSE) {
            int group_size, num_groups;
            group_size = 3 << (s->exp_strategy[ch] - 1);
            if(ch == CPL_CH)
                num_groups = (s->end_freq[ch] - s->start_freq[ch]) / group_size;
            else if(ch == s->lfe_ch)
                num_groups = 2;
            else
                num_groups = (s->end_freq[ch] + group_size - 4) / group_size;
            s->dexps[ch][0] = get_bits(gbc, 4) << !ch;
            decode_exponents(gbc, s->exp_strategy[ch], num_groups, s->dexps[ch][0],
                             &s->dexps[ch][s->start_freq[ch]+!!ch]);
            if(ch != CPL_CH && ch != s->lfe_ch)
                skip_bits(gbc, 2); /* skip gainrng */
        }
    }

    /* bit allocation information */
    if (get_bits1(gbc)) {
        s->bit_alloc_params.slow_decay = ff_ac3_slow_decay_tab[get_bits(gbc, 2)] >> s->bit_alloc_params.sr_shift;
        s->bit_alloc_params.fast_decay = ff_ac3_fast_decay_tab[get_bits(gbc, 2)] >> s->bit_alloc_params.sr_shift;
        s->bit_alloc_params.slow_gain  = ff_ac3_slow_gain_tab[get_bits(gbc, 2)];
        s->bit_alloc_params.db_per_bit = ff_ac3_db_per_bit_tab[get_bits(gbc, 2)];
        s->bit_alloc_params.floor  = ff_ac3_floor_tab[get_bits(gbc, 3)];
        for(ch=!s->cpl_in_use; ch<=s->channels; ch++) {
            bit_alloc_stages[ch] = FFMAX(bit_alloc_stages[ch], 2);
        }
    }

    /* signal-to-noise ratio offsets and fast gains (signal-to-mask ratios) */
    if (get_bits1(gbc)) {
        int csnr;
        csnr = (get_bits(gbc, 6) - 15) << 4;
        for (ch = !s->cpl_in_use; ch <= s->channels; ch++) { /* snr offset and fast gain */
            s->snr_offset[ch] = (csnr + get_bits(gbc, 4)) << 2;
            s->fast_gain[ch] = ff_ac3_fast_gain_tab[get_bits(gbc, 3)];
        }
        memset(bit_alloc_stages, 3, AC3_MAX_CHANNELS);
    }

    /* coupling leak information */
    if (s->cpl_in_use && get_bits1(gbc)) {
        s->bit_alloc_params.cpl_fast_leak = get_bits(gbc, 3);
        s->bit_alloc_params.cpl_slow_leak = get_bits(gbc, 3);
        bit_alloc_stages[CPL_CH] = FFMAX(bit_alloc_stages[CPL_CH], 2);
    }

    /* delta bit allocation information */
    if (get_bits1(gbc)) {
        /* delta bit allocation exists (strategy) */
        for (ch = !s->cpl_in_use; ch <= fbw_channels; ch++) {
            s->dba_mode[ch] = get_bits(gbc, 2);
            if (s->dba_mode[ch] == DBA_RESERVED) {
                av_log(s->avctx, AV_LOG_ERROR, "delta bit allocation strategy reserved\n");
                return -1;
            }
            bit_alloc_stages[ch] = FFMAX(bit_alloc_stages[ch], 2);
        }
        /* channel delta offset, len and bit allocation */
        for (ch = !s->cpl_in_use; ch <= fbw_channels; ch++) {
            if (s->dba_mode[ch] == DBA_NEW) {
                s->dba_nsegs[ch] = get_bits(gbc, 3);
                for (seg = 0; seg <= s->dba_nsegs[ch]; seg++) {
                    s->dba_offsets[ch][seg] = get_bits(gbc, 5);
                    s->dba_lengths[ch][seg] = get_bits(gbc, 4);
                    s->dba_values[ch][seg] = get_bits(gbc, 3);
                }
            }
        }
    } else if(blk == 0) {
        for(ch=0; ch<=s->channels; ch++) {
            s->dba_mode[ch] = DBA_NONE;
        }
    }

    /* Bit allocation */
    for(ch=!s->cpl_in_use; ch<=s->channels; ch++) {
        if(bit_alloc_stages[ch] > 2) {
            /* Exponent mapping into PSD and PSD integration */
            ff_ac3_bit_alloc_calc_psd(s->dexps[ch],
                                      s->start_freq[ch], s->end_freq[ch],
                                      s->psd[ch], s->band_psd[ch]);
        }
        if(bit_alloc_stages[ch] > 1) {
            /* Compute excitation function, Compute masking curve, and
               Apply delta bit allocation */
            ff_ac3_bit_alloc_calc_mask(&s->bit_alloc_params, s->band_psd[ch],
                                       s->start_freq[ch], s->end_freq[ch],
                                       s->fast_gain[ch], (ch == s->lfe_ch),
                                       s->dba_mode[ch], s->dba_nsegs[ch],
                                       s->dba_offsets[ch], s->dba_lengths[ch],
                                       s->dba_values[ch], s->mask[ch]);
        }
        if(bit_alloc_stages[ch] > 0) {
            /* Compute bit allocation */
            ff_ac3_bit_alloc_calc_bap(s->mask[ch], s->psd[ch],
                                      s->start_freq[ch], s->end_freq[ch],
                                      s->snr_offset[ch],
                                      s->bit_alloc_params.floor,
                                      s->bap[ch]);
        }
    }

    /* unused dummy data */
    if (get_bits1(gbc)) {
        int skipl = get_bits(gbc, 9);
        while(skipl--)
            skip_bits(gbc, 8);
    }

    /* unpack the transform coefficients
       this also uncouples channels if coupling is in use. */
    if (get_transform_coeffs(s)) {
        av_log(s->avctx, AV_LOG_ERROR, "Error in routine get_transform_coeffs\n");
        return -1;
    }

    /* recover coefficients if rematrixing is in use */
    if(s->channel_mode == AC3_CHMODE_STEREO)
        do_rematrixing(s);

    /* apply scaling to coefficients (headroom, dynrng) */
    for(ch=1; ch<=s->channels; ch++) {
        float gain = s->mul_bias / 4194304.0f;
        if(s->channel_mode == AC3_CHMODE_DUALMONO) {
            gain *= s->dynamic_range[ch-1];
        } else {
            gain *= s->dynamic_range[0];
        }
        for(i=0; i<256; i++) {
            s->transform_coeffs[ch][i] = s->fixed_coeffs[ch][i] * gain;
        }
    }

    /* downmix and MDCT. order depends on whether block switching is used for
       any channel in this block. this is because coefficients for the long
       and short transforms cannot be mixed. */
    downmix_output = s->channels != s->out_channels &&
                     !((s->output_mode & AC3_OUTPUT_LFEON) &&
                     s->fbw_channels == s->out_channels);
    if(different_transforms) {
        /* the delay samples have already been downmixed, so we upmix the delay
           samples in order to reconstruct all channels before downmixing. */
        if(s->downmixed) {
            s->downmixed = 0;
            ac3_upmix_delay(s);
        }

        do_imdct(s, s->channels);

        if(downmix_output) {
            ac3_downmix(s, s->output, 0);
        }
    } else {
        if(downmix_output) {
            ac3_downmix(s, s->transform_coeffs, 1);
        }

        if(!s->downmixed) {
            s->downmixed = 1;
            ac3_downmix(s, s->delay, 0);
        }

        do_imdct(s, s->out_channels);
    }

    /* convert float to 16-bit integer */
    for(ch=0; ch<s->out_channels; ch++) {
        for(i=0; i<256; i++) {
            s->output[ch][i] += s->add_bias;
        }
        s->dsp.float_to_int16(s->int_output[ch], s->output[ch], 256);
    }

    return 0;
}

/**
 * Decode a single AC-3 frame.
 */
static int ac3_decode_frame(AVCodecContext * avctx, void *data, int *data_size,
                            const uint8_t *buf, int buf_size)
{
    AC3DecodeContext *s = avctx->priv_data;
    int16_t *out_samples = (int16_t *)data;
    int i, blk, ch, err;

    /* initialize the GetBitContext with the start of valid AC-3 Frame */
    if (s->input_buffer) {
        /* copy input buffer to decoder context to avoid reading past the end
           of the buffer, which can be caused by a damaged input stream. */
        memcpy(s->input_buffer, buf, FFMIN(buf_size, AC3_MAX_FRAME_SIZE));
        init_get_bits(&s->gbc, s->input_buffer, buf_size * 8);
    } else {
        init_get_bits(&s->gbc, buf, buf_size * 8);
    }

    /* parse the syncinfo */
    err = ac3_parse_header(s);
    if(err) {
        switch(err) {
            case AC3_PARSE_ERROR_SYNC:
                av_log(avctx, AV_LOG_ERROR, "frame sync error\n");
                break;
            case AC3_PARSE_ERROR_BSID:
                av_log(avctx, AV_LOG_ERROR, "invalid bitstream id\n");
                break;
            case AC3_PARSE_ERROR_SAMPLE_RATE:
                av_log(avctx, AV_LOG_ERROR, "invalid sample rate\n");
                break;
            case AC3_PARSE_ERROR_FRAME_SIZE:
                av_log(avctx, AV_LOG_ERROR, "invalid frame size\n");
                break;
            case AC3_PARSE_ERROR_FRAME_TYPE:
                av_log(avctx, AV_LOG_ERROR, "invalid frame type\n");
                break;
            default:
                av_log(avctx, AV_LOG_ERROR, "invalid header\n");
                break;
        }
        return -1;
    }

    /* check that reported frame size fits in input buffer */
    if(s->frame_size > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "incomplete frame\n");
        return -1;
    }

    /* check for crc mismatch */
    if(avctx->error_resilience >= FF_ER_CAREFUL) {
        if(av_crc(av_crc_get_table(AV_CRC_16_ANSI), 0, &buf[2], s->frame_size-2)) {
            av_log(avctx, AV_LOG_ERROR, "frame CRC mismatch\n");
            return -1;
        }
        /* TODO: error concealment */
    }

    avctx->sample_rate = s->sample_rate;
    avctx->bit_rate = s->bit_rate;

    /* channel config */
    s->out_channels = s->channels;
    if (avctx->request_channels > 0 && avctx->request_channels <= 2 &&
            avctx->request_channels < s->channels) {
        s->out_channels = avctx->request_channels;
        s->output_mode  = avctx->request_channels == 1 ? AC3_CHMODE_MONO : AC3_CHMODE_STEREO;
    }
    avctx->channels = s->out_channels;

    /* set downmixing coefficients if needed */
    if(s->channels != s->out_channels && !((s->output_mode & AC3_OUTPUT_LFEON) &&
            s->fbw_channels == s->out_channels)) {
        set_downmix_coeffs(s);
    }

    /* parse the audio blocks */
    for (blk = 0; blk < NB_BLOCKS; blk++) {
        if (ac3_parse_audio_block(s, blk)) {
            av_log(avctx, AV_LOG_ERROR, "error parsing the audio block\n");
            *data_size = 0;
            return s->frame_size;
        }
        for (i = 0; i < 256; i++)
            for (ch = 0; ch < s->out_channels; ch++)
                *(out_samples++) = s->int_output[ch][i];
    }
    *data_size = NB_BLOCKS * 256 * avctx->channels * sizeof (int16_t);
    return s->frame_size;
}

/**
 * Uninitialize the AC-3 decoder.
 */
static av_cold int ac3_decode_end(AVCodecContext *avctx)
{
    AC3DecodeContext *s = avctx->priv_data;
    ff_mdct_end(&s->imdct_512);
    ff_mdct_end(&s->imdct_256);

    av_freep(&s->input_buffer);

    return 0;
}

AVCodec ac3_decoder = {
    .name = "ac3",
    .type = CODEC_TYPE_AUDIO,
    .id = CODEC_ID_AC3,
    .priv_data_size = sizeof (AC3DecodeContext),
    .init = ac3_decode_init,
    .close = ac3_decode_end,
    .decode = ac3_decode_frame,
};
