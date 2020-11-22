/*
 * ATRAC3 compatible decoder
 * Copyright (c) 2006-2008 Maxim Poliakovski
 * Copyright (c) 2006-2008 Benjamin Larsson
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
 * ATRAC3 compatible decoder.
 * This decoder handles Sony's ATRAC3 data.
 *
 * Container formats used to store ATRAC3 data:
 * RealMedia (.rm), RIFF WAV (.wav, .at3), Sony OpenMG (.oma, .aa3).
 *
 * To use this decoder, a calling application must supply the extradata
 * bytes provided in the containers above.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "libavutil/attributes.h"
#include "libavutil/float_dsp.h"
#include "libavutil/libm.h"
#include "libavutil/thread.h"

#include "avcodec.h"
#include "bytestream.h"
#include "fft.h"
#include "get_bits.h"
#include "internal.h"

#include "atrac.h"
#include "atrac3data.h"

#define MIN_CHANNELS    1
#define MAX_CHANNELS    8
#define MAX_JS_PAIRS    8 / 2

#define JOINT_STEREO    0x12
#define SINGLE          0x2

#define SAMPLES_PER_FRAME 1024
#define MDCT_SIZE          512

#define ATRAC3_VLC_BITS 8

typedef struct GainBlock {
    AtracGainInfo g_block[4];
} GainBlock;

typedef struct TonalComponent {
    int pos;
    int num_coefs;
    float coef[8];
} TonalComponent;

typedef struct ChannelUnit {
    int            bands_coded;
    int            num_components;
    float          prev_frame[SAMPLES_PER_FRAME];
    int            gc_blk_switch;
    TonalComponent components[64];
    GainBlock      gain_block[2];

    DECLARE_ALIGNED(32, float, spectrum)[SAMPLES_PER_FRAME];
    DECLARE_ALIGNED(32, float, imdct_buf)[SAMPLES_PER_FRAME];

    float          delay_buf1[46]; ///<qmf delay buffers
    float          delay_buf2[46];
    float          delay_buf3[46];
} ChannelUnit;

typedef struct ATRAC3Context {
    GetBitContext gb;
    //@{
    /** stream data */
    int coding_mode;

    ChannelUnit *units;
    //@}
    //@{
    /** joint-stereo related variables */
    int matrix_coeff_index_prev[MAX_JS_PAIRS][4];
    int matrix_coeff_index_now[MAX_JS_PAIRS][4];
    int matrix_coeff_index_next[MAX_JS_PAIRS][4];
    int weighting_delay[MAX_JS_PAIRS][6];
    //@}
    //@{
    /** data buffers */
    uint8_t *decoded_bytes_buffer;
    float temp_buf[1070];
    //@}
    //@{
    /** extradata */
    int scrambled_stream;
    //@}

    AtracGCContext    gainc_ctx;
    FFTContext        mdct_ctx;
    void (*vector_fmul)(float *dst, const float *src0, const float *src1,
                        int len);
} ATRAC3Context;

static DECLARE_ALIGNED(32, float, mdct_window)[MDCT_SIZE];
static VLC_TYPE atrac3_vlc_table[7 * 1 << ATRAC3_VLC_BITS][2];
static VLC   spectral_coeff_tab[7];

/**
 * Regular 512 points IMDCT without overlapping, with the exception of the
 * swapping of odd bands caused by the reverse spectra of the QMF.
 *
 * @param odd_band  1 if the band is an odd band
 */
static void imlt(ATRAC3Context *q, float *input, float *output, int odd_band)
{
    int i;

    if (odd_band) {
        /**
         * Reverse the odd bands before IMDCT, this is an effect of the QMF
         * transform or it gives better compression to do it this way.
         * FIXME: It should be possible to handle this in imdct_calc
         * for that to happen a modification of the prerotation step of
         * all SIMD code and C code is needed.
         * Or fix the functions before so they generate a pre reversed spectrum.
         */
        for (i = 0; i < 128; i++)
            FFSWAP(float, input[i], input[255 - i]);
    }

    q->mdct_ctx.imdct_calc(&q->mdct_ctx, output, input);

    /* Perform windowing on the output. */
    q->vector_fmul(output, output, mdct_window, MDCT_SIZE);
}

/*
 * indata descrambling, only used for data coming from the rm container
 */
static int decode_bytes(const uint8_t *input, uint8_t *out, int bytes)
{
    int i, off;
    uint32_t c;
    const uint32_t *buf;
    uint32_t *output = (uint32_t *)out;

    off = (intptr_t)input & 3;
    buf = (const uint32_t *)(input - off);
    if (off)
        c = av_be2ne32((0x537F6103U >> (off * 8)) | (0x537F6103U << (32 - (off * 8))));
    else
        c = av_be2ne32(0x537F6103U);
    bytes += 3 + off;
    for (i = 0; i < bytes / 4; i++)
        output[i] = c ^ buf[i];

    if (off)
        avpriv_request_sample(NULL, "Offset of %d", off);

    return off;
}

static av_cold void init_imdct_window(void)
{
    int i, j;

    /* generate the mdct window, for details see
     * http://wiki.multimedia.cx/index.php?title=RealAudio_atrc#Windows */
    for (i = 0, j = 255; i < 128; i++, j--) {
        float wi = sin(((i + 0.5) / 256.0 - 0.5) * M_PI) + 1.0;
        float wj = sin(((j + 0.5) / 256.0 - 0.5) * M_PI) + 1.0;
        float w  = 0.5 * (wi * wi + wj * wj);
        mdct_window[i] = mdct_window[511 - i] = wi / w;
        mdct_window[j] = mdct_window[511 - j] = wj / w;
    }
}

static av_cold int atrac3_decode_close(AVCodecContext *avctx)
{
    ATRAC3Context *q = avctx->priv_data;

    av_freep(&q->units);
    av_freep(&q->decoded_bytes_buffer);

    ff_mdct_end(&q->mdct_ctx);

    return 0;
}

/**
 * Mantissa decoding
 *
 * @param selector     which table the output values are coded with
 * @param coding_flag  constant length coding or variable length coding
 * @param mantissas    mantissa output table
 * @param num_codes    number of values to get
 */
static void read_quant_spectral_coeffs(GetBitContext *gb, int selector,
                                       int coding_flag, int *mantissas,
                                       int num_codes)
{
    int i, code, huff_symb;

    if (selector == 1)
        num_codes /= 2;

    if (coding_flag != 0) {
        /* constant length coding (CLC) */
        int num_bits = clc_length_tab[selector];

        if (selector > 1) {
            for (i = 0; i < num_codes; i++) {
                if (num_bits)
                    code = get_sbits(gb, num_bits);
                else
                    code = 0;
                mantissas[i] = code;
            }
        } else {
            for (i = 0; i < num_codes; i++) {
                if (num_bits)
                    code = get_bits(gb, num_bits); // num_bits is always 4 in this case
                else
                    code = 0;
                mantissas[i * 2    ] = mantissa_clc_tab[code >> 2];
                mantissas[i * 2 + 1] = mantissa_clc_tab[code &  3];
            }
        }
    } else {
        /* variable length coding (VLC) */
        if (selector != 1) {
            for (i = 0; i < num_codes; i++) {
                mantissas[i] = get_vlc2(gb, spectral_coeff_tab[selector-1].table,
                                        ATRAC3_VLC_BITS, 1);
            }
        } else {
            for (i = 0; i < num_codes; i++) {
                huff_symb = get_vlc2(gb, spectral_coeff_tab[selector - 1].table,
                                     ATRAC3_VLC_BITS, 1);
                mantissas[i * 2    ] = mantissa_vlc_tab[huff_symb * 2    ];
                mantissas[i * 2 + 1] = mantissa_vlc_tab[huff_symb * 2 + 1];
            }
        }
    }
}

/**
 * Restore the quantized band spectrum coefficients
 *
 * @return subband count, fix for broken specification/files
 */
static int decode_spectrum(GetBitContext *gb, float *output)
{
    int num_subbands, coding_mode, i, j, first, last, subband_size;
    int subband_vlc_index[32], sf_index[32];
    int mantissas[128];
    float scale_factor;

    num_subbands = get_bits(gb, 5);  // number of coded subbands
    coding_mode  = get_bits1(gb);    // coding Mode: 0 - VLC/ 1-CLC

    /* get the VLC selector table for the subbands, 0 means not coded */
    for (i = 0; i <= num_subbands; i++)
        subband_vlc_index[i] = get_bits(gb, 3);

    /* read the scale factor indexes from the stream */
    for (i = 0; i <= num_subbands; i++) {
        if (subband_vlc_index[i] != 0)
            sf_index[i] = get_bits(gb, 6);
    }

    for (i = 0; i <= num_subbands; i++) {
        first = subband_tab[i    ];
        last  = subband_tab[i + 1];

        subband_size = last - first;

        if (subband_vlc_index[i] != 0) {
            /* decode spectral coefficients for this subband */
            /* TODO: This can be done faster is several blocks share the
             * same VLC selector (subband_vlc_index) */
            read_quant_spectral_coeffs(gb, subband_vlc_index[i], coding_mode,
                                       mantissas, subband_size);

            /* decode the scale factor for this subband */
            scale_factor = ff_atrac_sf_table[sf_index[i]] *
                           inv_max_quant[subband_vlc_index[i]];

            /* inverse quantize the coefficients */
            for (j = 0; first < last; first++, j++)
                output[first] = mantissas[j] * scale_factor;
        } else {
            /* this subband was not coded, so zero the entire subband */
            memset(output + first, 0, subband_size * sizeof(*output));
        }
    }

    /* clear the subbands that were not coded */
    first = subband_tab[i];
    memset(output + first, 0, (SAMPLES_PER_FRAME - first) * sizeof(*output));
    return num_subbands;
}

/**
 * Restore the quantized tonal components
 *
 * @param components tonal components
 * @param num_bands  number of coded bands
 */
static int decode_tonal_components(GetBitContext *gb,
                                   TonalComponent *components, int num_bands)
{
    int i, b, c, m;
    int nb_components, coding_mode_selector, coding_mode;
    int band_flags[4], mantissa[8];
    int component_count = 0;

    nb_components = get_bits(gb, 5);

    /* no tonal components */
    if (nb_components == 0)
        return 0;

    coding_mode_selector = get_bits(gb, 2);
    if (coding_mode_selector == 2)
        return AVERROR_INVALIDDATA;

    coding_mode = coding_mode_selector & 1;

    for (i = 0; i < nb_components; i++) {
        int coded_values_per_component, quant_step_index;

        for (b = 0; b <= num_bands; b++)
            band_flags[b] = get_bits1(gb);

        coded_values_per_component = get_bits(gb, 3);

        quant_step_index = get_bits(gb, 3);
        if (quant_step_index <= 1)
            return AVERROR_INVALIDDATA;

        if (coding_mode_selector == 3)
            coding_mode = get_bits1(gb);

        for (b = 0; b < (num_bands + 1) * 4; b++) {
            int coded_components;

            if (band_flags[b >> 2] == 0)
                continue;

            coded_components = get_bits(gb, 3);

            for (c = 0; c < coded_components; c++) {
                TonalComponent *cmp = &components[component_count];
                int sf_index, coded_values, max_coded_values;
                float scale_factor;

                sf_index = get_bits(gb, 6);
                if (component_count >= 64)
                    return AVERROR_INVALIDDATA;

                cmp->pos = b * 64 + get_bits(gb, 6);

                max_coded_values = SAMPLES_PER_FRAME - cmp->pos;
                coded_values     = coded_values_per_component + 1;
                coded_values     = FFMIN(max_coded_values, coded_values);

                scale_factor = ff_atrac_sf_table[sf_index] *
                               inv_max_quant[quant_step_index];

                read_quant_spectral_coeffs(gb, quant_step_index, coding_mode,
                                           mantissa, coded_values);

                cmp->num_coefs = coded_values;

                /* inverse quant */
                for (m = 0; m < coded_values; m++)
                    cmp->coef[m] = mantissa[m] * scale_factor;

                component_count++;
            }
        }
    }

    return component_count;
}

/**
 * Decode gain parameters for the coded bands
 *
 * @param block      the gainblock for the current band
 * @param num_bands  amount of coded bands
 */
static int decode_gain_control(GetBitContext *gb, GainBlock *block,
                               int num_bands)
{
    int b, j;
    int *level, *loc;

    AtracGainInfo *gain = block->g_block;

    for (b = 0; b <= num_bands; b++) {
        gain[b].num_points = get_bits(gb, 3);
        level              = gain[b].lev_code;
        loc                = gain[b].loc_code;

        for (j = 0; j < gain[b].num_points; j++) {
            level[j] = get_bits(gb, 4);
            loc[j]   = get_bits(gb, 5);
            if (j && loc[j] <= loc[j - 1])
                return AVERROR_INVALIDDATA;
        }
    }

    /* Clear the unused blocks. */
    for (; b < 4 ; b++)
        gain[b].num_points = 0;

    return 0;
}

/**
 * Combine the tonal band spectrum and regular band spectrum
 *
 * @param spectrum        output spectrum buffer
 * @param num_components  number of tonal components
 * @param components      tonal components for this band
 * @return                position of the last tonal coefficient
 */
static int add_tonal_components(float *spectrum, int num_components,
                                TonalComponent *components)
{
    int i, j, last_pos = -1;
    float *input, *output;

    for (i = 0; i < num_components; i++) {
        last_pos = FFMAX(components[i].pos + components[i].num_coefs, last_pos);
        input    = components[i].coef;
        output   = &spectrum[components[i].pos];

        for (j = 0; j < components[i].num_coefs; j++)
            output[j] += input[j];
    }

    return last_pos;
}

#define INTERPOLATE(old, new, nsample) \
    ((old) + (nsample) * 0.125 * ((new) - (old)))

static void reverse_matrixing(float *su1, float *su2, int *prev_code,
                              int *curr_code)
{
    int i, nsample, band;
    float mc1_l, mc1_r, mc2_l, mc2_r;

    for (i = 0, band = 0; band < 4 * 256; band += 256, i++) {
        int s1 = prev_code[i];
        int s2 = curr_code[i];
        nsample = band;

        if (s1 != s2) {
            /* Selector value changed, interpolation needed. */
            mc1_l = matrix_coeffs[s1 * 2    ];
            mc1_r = matrix_coeffs[s1 * 2 + 1];
            mc2_l = matrix_coeffs[s2 * 2    ];
            mc2_r = matrix_coeffs[s2 * 2 + 1];

            /* Interpolation is done over the first eight samples. */
            for (; nsample < band + 8; nsample++) {
                float c1 = su1[nsample];
                float c2 = su2[nsample];
                c2 = c1 * INTERPOLATE(mc1_l, mc2_l, nsample - band) +
                     c2 * INTERPOLATE(mc1_r, mc2_r, nsample - band);
                su1[nsample] = c2;
                su2[nsample] = c1 * 2.0 - c2;
            }
        }

        /* Apply the matrix without interpolation. */
        switch (s2) {
        case 0:     /* M/S decoding */
            for (; nsample < band + 256; nsample++) {
                float c1 = su1[nsample];
                float c2 = su2[nsample];
                su1[nsample] =  c2       * 2.0;
                su2[nsample] = (c1 - c2) * 2.0;
            }
            break;
        case 1:
            for (; nsample < band + 256; nsample++) {
                float c1 = su1[nsample];
                float c2 = su2[nsample];
                su1[nsample] = (c1 + c2) *  2.0;
                su2[nsample] =  c2       * -2.0;
            }
            break;
        case 2:
        case 3:
            for (; nsample < band + 256; nsample++) {
                float c1 = su1[nsample];
                float c2 = su2[nsample];
                su1[nsample] = c1 + c2;
                su2[nsample] = c1 - c2;
            }
            break;
        default:
            av_assert1(0);
        }
    }
}

static void get_channel_weights(int index, int flag, float ch[2])
{
    if (index == 7) {
        ch[0] = 1.0;
        ch[1] = 1.0;
    } else {
        ch[0] = (index & 7) / 7.0;
        ch[1] = sqrt(2 - ch[0] * ch[0]);
        if (flag)
            FFSWAP(float, ch[0], ch[1]);
    }
}

static void channel_weighting(float *su1, float *su2, int *p3)
{
    int band, nsample;
    /* w[x][y] y=0 is left y=1 is right */
    float w[2][2];

    if (p3[1] != 7 || p3[3] != 7) {
        get_channel_weights(p3[1], p3[0], w[0]);
        get_channel_weights(p3[3], p3[2], w[1]);

        for (band = 256; band < 4 * 256; band += 256) {
            for (nsample = band; nsample < band + 8; nsample++) {
                su1[nsample] *= INTERPOLATE(w[0][0], w[0][1], nsample - band);
                su2[nsample] *= INTERPOLATE(w[1][0], w[1][1], nsample - band);
            }
            for(; nsample < band + 256; nsample++) {
                su1[nsample] *= w[1][0];
                su2[nsample] *= w[1][1];
            }
        }
    }
}

/**
 * Decode a Sound Unit
 *
 * @param snd           the channel unit to be used
 * @param output        the decoded samples before IQMF in float representation
 * @param channel_num   channel number
 * @param coding_mode   the coding mode (JOINT_STEREO or single channels)
 */
static int decode_channel_sound_unit(ATRAC3Context *q, GetBitContext *gb,
                                     ChannelUnit *snd, float *output,
                                     int channel_num, int coding_mode)
{
    int band, ret, num_subbands, last_tonal, num_bands;
    GainBlock *gain1 = &snd->gain_block[    snd->gc_blk_switch];
    GainBlock *gain2 = &snd->gain_block[1 - snd->gc_blk_switch];

    if (coding_mode == JOINT_STEREO && (channel_num % 2) == 1) {
        if (get_bits(gb, 2) != 3) {
            av_log(NULL,AV_LOG_ERROR,"JS mono Sound Unit id != 3.\n");
            return AVERROR_INVALIDDATA;
        }
    } else {
        if (get_bits(gb, 6) != 0x28) {
            av_log(NULL,AV_LOG_ERROR,"Sound Unit id != 0x28.\n");
            return AVERROR_INVALIDDATA;
        }
    }

    /* number of coded QMF bands */
    snd->bands_coded = get_bits(gb, 2);

    ret = decode_gain_control(gb, gain2, snd->bands_coded);
    if (ret)
        return ret;

    snd->num_components = decode_tonal_components(gb, snd->components,
                                                  snd->bands_coded);
    if (snd->num_components < 0)
        return snd->num_components;

    num_subbands = decode_spectrum(gb, snd->spectrum);

    /* Merge the decoded spectrum and tonal components. */
    last_tonal = add_tonal_components(snd->spectrum, snd->num_components,
                                      snd->components);


    /* calculate number of used MLT/QMF bands according to the amount of coded
       spectral lines */
    num_bands = (subband_tab[num_subbands] - 1) >> 8;
    if (last_tonal >= 0)
        num_bands = FFMAX((last_tonal + 256) >> 8, num_bands);


    /* Reconstruct time domain samples. */
    for (band = 0; band < 4; band++) {
        /* Perform the IMDCT step without overlapping. */
        if (band <= num_bands)
            imlt(q, &snd->spectrum[band * 256], snd->imdct_buf, band & 1);
        else
            memset(snd->imdct_buf, 0, 512 * sizeof(*snd->imdct_buf));

        /* gain compensation and overlapping */
        ff_atrac_gain_compensation(&q->gainc_ctx, snd->imdct_buf,
                                   &snd->prev_frame[band * 256],
                                   &gain1->g_block[band], &gain2->g_block[band],
                                   256, &output[band * 256]);
    }

    /* Swap the gain control buffers for the next frame. */
    snd->gc_blk_switch ^= 1;

    return 0;
}

static int decode_frame(AVCodecContext *avctx, const uint8_t *databuf,
                        float **out_samples)
{
    ATRAC3Context *q = avctx->priv_data;
    int ret, i, ch;
    uint8_t *ptr1;

    if (q->coding_mode == JOINT_STEREO) {
        /* channel coupling mode */

        /* Decode sound unit pairs (channels are expected to be even).
         * Multichannel joint stereo interleaves pairs (6ch: 2ch + 2ch + 2ch) */
        const uint8_t *js_databuf;
        int js_pair, js_block_align;

        js_block_align = (avctx->block_align / avctx->channels) * 2; /* block pair */

        for (ch = 0; ch < avctx->channels; ch = ch + 2) {
            js_pair = ch/2;
            js_databuf = databuf + js_pair * js_block_align; /* align to current pair */

            /* Set the bitstream reader at the start of first channel sound unit. */
            init_get_bits(&q->gb,
                          js_databuf, js_block_align * 8);

            /* decode Sound Unit 1 */
            ret = decode_channel_sound_unit(q, &q->gb, &q->units[ch],
                                            out_samples[ch], ch, JOINT_STEREO);
            if (ret != 0)
                return ret;

            /* Framedata of the su2 in the joint-stereo mode is encoded in
             * reverse byte order so we need to swap it first. */
            if (js_databuf == q->decoded_bytes_buffer) {
                uint8_t *ptr2 = q->decoded_bytes_buffer + js_block_align - 1;
                ptr1          = q->decoded_bytes_buffer;
                for (i = 0; i < js_block_align / 2; i++, ptr1++, ptr2--)
                    FFSWAP(uint8_t, *ptr1, *ptr2);
            } else {
                const uint8_t *ptr2 = js_databuf + js_block_align - 1;
                for (i = 0; i < js_block_align; i++)
                    q->decoded_bytes_buffer[i] = *ptr2--;
            }

            /* Skip the sync codes (0xF8). */
            ptr1 = q->decoded_bytes_buffer;
            for (i = 4; *ptr1 == 0xF8; i++, ptr1++) {
                if (i >= js_block_align)
                    return AVERROR_INVALIDDATA;
            }


            /* set the bitstream reader at the start of the second Sound Unit */
            ret = init_get_bits8(&q->gb,
                           ptr1, q->decoded_bytes_buffer + js_block_align - ptr1);
            if (ret < 0)
                return ret;

            /* Fill the Weighting coeffs delay buffer */
            memmove(q->weighting_delay[js_pair], &q->weighting_delay[js_pair][2],
                    4 * sizeof(*q->weighting_delay[js_pair]));
            q->weighting_delay[js_pair][4] = get_bits1(&q->gb);
            q->weighting_delay[js_pair][5] = get_bits(&q->gb, 3);

            for (i = 0; i < 4; i++) {
                q->matrix_coeff_index_prev[js_pair][i] = q->matrix_coeff_index_now[js_pair][i];
                q->matrix_coeff_index_now[js_pair][i]  = q->matrix_coeff_index_next[js_pair][i];
                q->matrix_coeff_index_next[js_pair][i] = get_bits(&q->gb, 2);
            }

            /* Decode Sound Unit 2. */
            ret = decode_channel_sound_unit(q, &q->gb, &q->units[ch+1],
                                            out_samples[ch+1], ch+1, JOINT_STEREO);
            if (ret != 0)
                return ret;

            /* Reconstruct the channel coefficients. */
            reverse_matrixing(out_samples[ch], out_samples[ch+1],
                              q->matrix_coeff_index_prev[js_pair],
                              q->matrix_coeff_index_now[js_pair]);

            channel_weighting(out_samples[ch], out_samples[ch+1], q->weighting_delay[js_pair]);
        }
    } else {
        /* single channels */
        /* Decode the channel sound units. */
        for (i = 0; i < avctx->channels; i++) {
            /* Set the bitstream reader at the start of a channel sound unit. */
            init_get_bits(&q->gb,
                          databuf + i * avctx->block_align / avctx->channels,
                          avctx->block_align * 8 / avctx->channels);

            ret = decode_channel_sound_unit(q, &q->gb, &q->units[i],
                                            out_samples[i], i, q->coding_mode);
            if (ret != 0)
                return ret;
        }
    }

    /* Apply the iQMF synthesis filter. */
    for (i = 0; i < avctx->channels; i++) {
        float *p1 = out_samples[i];
        float *p2 = p1 + 256;
        float *p3 = p2 + 256;
        float *p4 = p3 + 256;
        ff_atrac_iqmf(p1, p2, 256, p1, q->units[i].delay_buf1, q->temp_buf);
        ff_atrac_iqmf(p4, p3, 256, p3, q->units[i].delay_buf2, q->temp_buf);
        ff_atrac_iqmf(p1, p3, 512, p1, q->units[i].delay_buf3, q->temp_buf);
    }

    return 0;
}

static int al_decode_frame(AVCodecContext *avctx, const uint8_t *databuf,
                           int size, float **out_samples)
{
    ATRAC3Context *q = avctx->priv_data;
    int ret, i;

    /* Set the bitstream reader at the start of a channel sound unit. */
    init_get_bits(&q->gb, databuf, size * 8);
    /* single channels */
    /* Decode the channel sound units. */
    for (i = 0; i < avctx->channels; i++) {
        ret = decode_channel_sound_unit(q, &q->gb, &q->units[i],
                                        out_samples[i], i, q->coding_mode);
        if (ret != 0)
            return ret;
        while (i < avctx->channels && get_bits_left(&q->gb) > 6 && show_bits(&q->gb, 6) != 0x28) {
            skip_bits(&q->gb, 1);
        }
    }

    /* Apply the iQMF synthesis filter. */
    for (i = 0; i < avctx->channels; i++) {
        float *p1 = out_samples[i];
        float *p2 = p1 + 256;
        float *p3 = p2 + 256;
        float *p4 = p3 + 256;
        ff_atrac_iqmf(p1, p2, 256, p1, q->units[i].delay_buf1, q->temp_buf);
        ff_atrac_iqmf(p4, p3, 256, p3, q->units[i].delay_buf2, q->temp_buf);
        ff_atrac_iqmf(p1, p3, 512, p1, q->units[i].delay_buf3, q->temp_buf);
    }

    return 0;
}

static int atrac3_decode_frame(AVCodecContext *avctx, void *data,
                               int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame     = data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    ATRAC3Context *q = avctx->priv_data;
    int ret;
    const uint8_t *databuf;

    if (buf_size < avctx->block_align) {
        av_log(avctx, AV_LOG_ERROR,
               "Frame too small (%d bytes). Truncated file?\n", buf_size);
        return AVERROR_INVALIDDATA;
    }

    /* get output buffer */
    frame->nb_samples = SAMPLES_PER_FRAME;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    /* Check if we need to descramble and what buffer to pass on. */
    if (q->scrambled_stream) {
        decode_bytes(buf, q->decoded_bytes_buffer, avctx->block_align);
        databuf = q->decoded_bytes_buffer;
    } else {
        databuf = buf;
    }

    ret = decode_frame(avctx, databuf, (float **)frame->extended_data);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "Frame decoding error!\n");
        return ret;
    }

    *got_frame_ptr = 1;

    return avctx->block_align;
}

static int atrac3al_decode_frame(AVCodecContext *avctx, void *data,
                                 int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame = data;
    int ret;

    frame->nb_samples = SAMPLES_PER_FRAME;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    ret = al_decode_frame(avctx, avpkt->data, avpkt->size,
                          (float **)frame->extended_data);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "Frame decoding error!\n");
        return ret;
    }

    *got_frame_ptr = 1;

    return avpkt->size;
}

static av_cold void atrac3_init_static_data(void)
{
    VLC_TYPE (*table)[2] = atrac3_vlc_table;
    const uint8_t (*hufftabs)[2] = atrac3_hufftabs;
    int i;

    init_imdct_window();
    ff_atrac_generate_tables();

    /* Initialize the VLC tables. */
    for (i = 0; i < 7; i++) {
        spectral_coeff_tab[i].table           = table;
        spectral_coeff_tab[i].table_allocated = 256;
        ff_init_vlc_from_lengths(&spectral_coeff_tab[i], ATRAC3_VLC_BITS, huff_tab_sizes[i],
                                 &hufftabs[0][1], 2,
                                 &hufftabs[0][0], 2, 1,
                                 -31, INIT_VLC_USE_NEW_STATIC, NULL);
        hufftabs += huff_tab_sizes[i];
        table += 256;
    }
}

static av_cold int atrac3_decode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    int i, js_pair, ret;
    int version, delay, samples_per_frame, frame_factor;
    const uint8_t *edata_ptr = avctx->extradata;
    ATRAC3Context *q = avctx->priv_data;
    AVFloatDSPContext *fdsp;

    if (avctx->channels < MIN_CHANNELS || avctx->channels > MAX_CHANNELS) {
        av_log(avctx, AV_LOG_ERROR, "Channel configuration error!\n");
        return AVERROR(EINVAL);
    }

    /* Take care of the codec-specific extradata. */
    if (avctx->codec_id == AV_CODEC_ID_ATRAC3AL) {
        version           = 4;
        samples_per_frame = SAMPLES_PER_FRAME * avctx->channels;
        delay             = 0x88E;
        q->coding_mode    = SINGLE;
    } else if (avctx->extradata_size == 14) {
        /* Parse the extradata, WAV format */
        av_log(avctx, AV_LOG_DEBUG, "[0-1] %d\n",
               bytestream_get_le16(&edata_ptr));  // Unknown value always 1
        edata_ptr += 4;                             // samples per channel
        q->coding_mode = bytestream_get_le16(&edata_ptr);
        av_log(avctx, AV_LOG_DEBUG,"[8-9] %d\n",
               bytestream_get_le16(&edata_ptr));  //Dupe of coding mode
        frame_factor = bytestream_get_le16(&edata_ptr);  // Unknown always 1
        av_log(avctx, AV_LOG_DEBUG,"[12-13] %d\n",
               bytestream_get_le16(&edata_ptr));  // Unknown always 0

        /* setup */
        samples_per_frame    = SAMPLES_PER_FRAME * avctx->channels;
        version              = 4;
        delay                = 0x88E;
        q->coding_mode       = q->coding_mode ? JOINT_STEREO : SINGLE;
        q->scrambled_stream  = 0;

        if (avctx->block_align !=  96 * avctx->channels * frame_factor &&
            avctx->block_align != 152 * avctx->channels * frame_factor &&
            avctx->block_align != 192 * avctx->channels * frame_factor) {
            av_log(avctx, AV_LOG_ERROR, "Unknown frame/channel/frame_factor "
                   "configuration %d/%d/%d\n", avctx->block_align,
                   avctx->channels, frame_factor);
            return AVERROR_INVALIDDATA;
        }
    } else if (avctx->extradata_size == 12 || avctx->extradata_size == 10) {
        /* Parse the extradata, RM format. */
        version                = bytestream_get_be32(&edata_ptr);
        samples_per_frame      = bytestream_get_be16(&edata_ptr);
        delay                  = bytestream_get_be16(&edata_ptr);
        q->coding_mode         = bytestream_get_be16(&edata_ptr);
        q->scrambled_stream    = 1;

    } else {
        av_log(avctx, AV_LOG_ERROR, "Unknown extradata size %d.\n",
               avctx->extradata_size);
        return AVERROR(EINVAL);
    }

    /* Check the extradata */

    if (version != 4) {
        av_log(avctx, AV_LOG_ERROR, "Version %d != 4.\n", version);
        return AVERROR_INVALIDDATA;
    }

    if (samples_per_frame != SAMPLES_PER_FRAME * avctx->channels) {
        av_log(avctx, AV_LOG_ERROR, "Unknown amount of samples per frame %d.\n",
               samples_per_frame);
        return AVERROR_INVALIDDATA;
    }

    if (delay != 0x88E) {
        av_log(avctx, AV_LOG_ERROR, "Unknown amount of delay %x != 0x88E.\n",
               delay);
        return AVERROR_INVALIDDATA;
    }

    if (q->coding_mode == SINGLE)
        av_log(avctx, AV_LOG_DEBUG, "Single channels detected.\n");
    else if (q->coding_mode == JOINT_STEREO) {
        if (avctx->channels % 2 == 1) { /* Joint stereo channels must be even */
            av_log(avctx, AV_LOG_ERROR, "Invalid joint stereo channel configuration.\n");
            return AVERROR_INVALIDDATA;
        }
        av_log(avctx, AV_LOG_DEBUG, "Joint stereo detected.\n");
    } else {
        av_log(avctx, AV_LOG_ERROR, "Unknown channel coding mode %x!\n",
               q->coding_mode);
        return AVERROR_INVALIDDATA;
    }

    if (avctx->block_align > 4096 || avctx->block_align <= 0)
        return AVERROR(EINVAL);

    q->decoded_bytes_buffer = av_mallocz(FFALIGN(avctx->block_align, 4) +
                                         AV_INPUT_BUFFER_PADDING_SIZE);
    if (!q->decoded_bytes_buffer)
        return AVERROR(ENOMEM);

    avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;

    /* initialize the MDCT transform */
    if ((ret = ff_mdct_init(&q->mdct_ctx, 9, 1, 1.0 / 32768)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing MDCT\n");
        return ret;
    }

    /* init the joint-stereo decoding data */
    for (js_pair = 0; js_pair < MAX_JS_PAIRS; js_pair++) {
        q->weighting_delay[js_pair][0] = 0;
        q->weighting_delay[js_pair][1] = 7;
        q->weighting_delay[js_pair][2] = 0;
        q->weighting_delay[js_pair][3] = 7;
        q->weighting_delay[js_pair][4] = 0;
        q->weighting_delay[js_pair][5] = 7;

        for (i = 0; i < 4; i++) {
            q->matrix_coeff_index_prev[js_pair][i] = 3;
            q->matrix_coeff_index_now[js_pair][i]  = 3;
            q->matrix_coeff_index_next[js_pair][i] = 3;
        }
    }

    ff_atrac_init_gain_compensation(&q->gainc_ctx, 4, 3);
    fdsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!fdsp)
        return AVERROR(ENOMEM);
    q->vector_fmul = fdsp->vector_fmul;
    av_free(fdsp);

    q->units = av_mallocz_array(avctx->channels, sizeof(*q->units));
    if (!q->units)
        return AVERROR(ENOMEM);

    ff_thread_once(&init_static_once, atrac3_init_static_data);

    return 0;
}

AVCodec ff_atrac3_decoder = {
    .name             = "atrac3",
    .long_name        = NULL_IF_CONFIG_SMALL("ATRAC3 (Adaptive TRansform Acoustic Coding 3)"),
    .type             = AVMEDIA_TYPE_AUDIO,
    .id               = AV_CODEC_ID_ATRAC3,
    .priv_data_size   = sizeof(ATRAC3Context),
    .init             = atrac3_decode_init,
    .close            = atrac3_decode_close,
    .decode           = atrac3_decode_frame,
    .capabilities     = AV_CODEC_CAP_SUBFRAMES | AV_CODEC_CAP_DR1,
    .sample_fmts      = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                        AV_SAMPLE_FMT_NONE },
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};

AVCodec ff_atrac3al_decoder = {
    .name             = "atrac3al",
    .long_name        = NULL_IF_CONFIG_SMALL("ATRAC3 AL (Adaptive TRansform Acoustic Coding 3 Advanced Lossless)"),
    .type             = AVMEDIA_TYPE_AUDIO,
    .id               = AV_CODEC_ID_ATRAC3AL,
    .priv_data_size   = sizeof(ATRAC3Context),
    .init             = atrac3_decode_init,
    .close            = atrac3_decode_close,
    .decode           = atrac3al_decode_frame,
    .capabilities     = AV_CODEC_CAP_SUBFRAMES | AV_CODEC_CAP_DR1,
    .sample_fmts      = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                        AV_SAMPLE_FMT_NONE },
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
