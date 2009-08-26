/*
 * Wmapro compatible decoder
 * Copyright (c) 2007 Baptiste Coudurier, Benjamin Larsson, Ulion
 * Copyright (c) 2008 - 2009 Sascha Sommer, Benjamin Larsson
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
 * @file  libavcodec/wmaprodec.c
 * @brief wmapro decoder implementation
 * Wmapro is an MDCT based codec comparable to wma standard or AAC.
 * The decoding therefore consists of the following steps:
 * - bitstream decoding
 * - reconstruction of per-channel data
 * - rescaling and inverse quantization
 * - IMDCT
 * - windowing and overlapp-add
 *
 * The compressed wmapro bitstream is split into individual packets.
 * Every such packet contains one or more wma frames.
 * The compressed frames may have a variable length and frames may
 * cross packet boundaries.
 * Common to all wmapro frames is the number of samples that are stored in
 * a frame.
 * The number of samples and a few other decode flags are stored
 * as extradata that has to be passed to the decoder.
 *
 * The wmapro frames themselves are again split into a variable number of
 * subframes. Every subframe contains the data for 2^N time domain samples
 * where N varies between 7 and 12.
 *
 * Example wmapro bitstream (in samples):
 *
 * ||   packet 0           || packet 1 || packet 2      packets
 * ---------------------------------------------------
 * || frame 0      || frame 1       || frame 2    ||    frames
 * ---------------------------------------------------
 * ||   |      |   ||   |   |   |   ||            ||    subframes of channel 0
 * ---------------------------------------------------
 * ||      |   |   ||   |   |   |   ||            ||    subframes of channel 1
 * ---------------------------------------------------
 *
 * The frame layouts for the individual channels of a wma frame does not need
 * to be the same.
 *
 * However, if the offsets and lengths of several subframes of a frame are the
 * same, the subframes of the channels can be grouped.
 * Every group may then use special coding techniques like M/S stereo coding
 * to improve the compression ratio. These channel transformations do not
 * need to be applied to a whole subframe. Instead, they can also work on
 * individual scale factor bands (see below).
 * The coefficients that carry the audio signal in the frequency domain
 * are transmitted as huffman-coded vectors with 4, 2 and 1 elements.
 * In addition to that, the encoder can switch to a runlevel coding scheme
 * by transmitting subframe_length / 128 zero coefficients.
 *
 * Before the audio signal can be converted to the time domain, the
 * coefficients have to be rescaled and inverse quantized.
 * A subframe is therefore split into several scale factor bands that get
 * scaled individually.
 * Scale factors are submitted for every frame but they might be shared
 * between the subframes of a channel. Scale factors are initially DPCM-coded.
 * Once scale factors are shared, the differences are transmitted as runlevel
 * codes.
 * Every subframe length and offset combination in the frame layout shares a
 * common quantization factor that can be adjusted for every channel by a
 * modifier.
 * After the inverse quantization, the coefficients get processed by an IMDCT.
 * The resulting values are then windowed with a sine window and the first half
 * of the values are added to the second half of the output from the previous
 * subframe in order to reconstruct the output samples.
 */

/**
 *@brief Uninitialize the decoder and free all resources.
 *@param avctx codec context
 *@return 0 on success, < 0 otherwise
 */
static av_cold int decode_end(AVCodecContext *avctx)
{
    WMA3DecodeContext *s = avctx->priv_data;
    int i;

    for (i = 0; i < WMAPRO_BLOCK_SIZES; i++)
        ff_mdct_end(&s->mdct_ctx[i]);

    return 0;
}

/**
 *@brief Calculate a decorrelation matrix from the bitstream parameters.
 *@param s codec context
 *@param chgroup channel group for which the matrix needs to be calculated
 */
static void decode_decorrelation_matrix(WMA3DecodeContext *s,
                                        WMA3ChannelGroup *chgroup)
{
    int i;
    int offset = 0;
    int8_t rotation_offset[WMAPRO_MAX_CHANNELS * WMAPRO_MAX_CHANNELS];
    memset(chgroup->decorrelation_matrix, 0,
           sizeof(float) *s->num_channels * s->num_channels);

    for (i = 0; i < chgroup->num_channels * (chgroup->num_channels - 1) >> 1; i++)
        rotation_offset[i] = get_bits(&s->gb, 6);

    for (i = 0; i < chgroup->num_channels; i++)
        chgroup->decorrelation_matrix[chgroup->num_channels * i + i] =
                                                get_bits1(&s->gb) ? 1.0 : -1.0;

    for (i = 1; i < chgroup->num_channels; i++) {
        int x;
        for (x = 0; x < i; x++) {
            int y;
            for (y = 0; y < i + 1; y++) {
                float v1 = chgroup->decorrelation_matrix[x * chgroup->num_channels + y];
                float v2 = chgroup->decorrelation_matrix[i * chgroup->num_channels + y];
                int n = rotation_offset[offset + x];
                float sinv;
                float cosv;

                if (n < 32) {
                    sinv = sin64[n];
                    cosv = sin64[32-n];
                } else {
                    sinv = sin64[64-n];
                    cosv = -sin64[n-32];
                }

                chgroup->decorrelation_matrix[y + x * chgroup->num_channels] =
                                               (v1 * sinv) - (v2 * cosv);
                chgroup->decorrelation_matrix[y + i * chgroup->num_channels] =
                                               (v1 * cosv) + (v2 * sinv);
            }
        }
        offset += i;
    }
}

/**
 *@brief Extract the coefficients from the bitstream.
 *@param s codec context
 *@param c current channel number
 *@return 0 on success, < 0 in case of bitstream errors
 */
static int decode_coeffs(WMA3DecodeContext *s, int c)
{
    int vlctable;
    VLC* vlc;
    WMA3ChannelCtx* ci = &s->channel[c];
    int rl_mode = 0;
    int cur_coeff = 0;
    int num_zeros = 0;
    const uint16_t* run;
    const uint16_t* level;

    dprintf(s->avctx, "decode coefficients for channel %i\n", c);

    vlctable = get_bits1(&s->gb);
    vlc = &coef_vlc[vlctable];

    if (vlctable) {
        run = coef1_run;
        level = coef1_level;
    } else {
        run = coef0_run;
        level = coef0_level;
    }

    /** decode vector coefficients (consumes up to 167 bits per iteration for
      4 vector coded large values) */
    while (!rl_mode && cur_coeff + 3 < s->subframe_len) {
        int vals[4];
        int i;
        unsigned int idx;

        idx = get_vlc2(&s->gb, vec4_vlc.table, VLCBITS, VEC4MAXDEPTH);

        if ( idx == HUFF_VEC4_SIZE - 1 ) {
            for (i = 0; i < 4; i += 2) {
                idx = get_vlc2(&s->gb, vec2_vlc.table, VLCBITS, VEC2MAXDEPTH);
                if ( idx == HUFF_VEC2_SIZE - 1 ) {
                    vals[i] = get_vlc2(&s->gb, vec1_vlc.table, VLCBITS, VEC1MAXDEPTH);
                    if (vals[i] == HUFF_VEC1_SIZE - 1)
                        vals[i] += ff_wma_get_large_val(&s->gb);
                    vals[i+1] = get_vlc2(&s->gb, vec1_vlc.table, VLCBITS, VEC1MAXDEPTH);
                    if (vals[i+1] == HUFF_VEC1_SIZE - 1)
                        vals[i+1] += ff_wma_get_large_val(&s->gb);
                } else {
                    vals[i]   = symbol_to_vec2[idx] >> 4;
                    vals[i+1] = symbol_to_vec2[idx] & 0xF;
                }
            }
        } else {
             vals[0] =  symbol_to_vec4[idx] >> 12;
             vals[1] = (symbol_to_vec4[idx] >> 8) & 0xF;
             vals[2] = (symbol_to_vec4[idx] >> 4) & 0xF;
             vals[3] =  symbol_to_vec4[idx]       & 0xF;
        }

        /** decode sign */
        for (i = 0; i < 4; i++) {
            if (vals[i]) {
                int sign = get_bits1(&s->gb) - 1;
                ci->coeffs[cur_coeff] = (vals[i]^sign) - sign;
                num_zeros = 0;
            } else {
                /** switch to run level mode when subframe_len / 128 zeros
                   were found in a row */
                rl_mode |= (++num_zeros > s->subframe_len>>8);
            }
            ++cur_coeff;
        }
    }

    /** decode run level coded coefficients */
    if (rl_mode) {
        if(ff_wma_run_level_decode(s->avctx, &s->gb, vlc,
                             level, run, 1, ci->coeffs,
                             cur_coeff, s->subframe_len, s->subframe_len,
                             s->esc_len, 0))
            return AVERROR_INVALIDDATA;
    }

    return 0;
}

/**
 *@brief Reconstruct the individual channel data.
 *@param s codec context
 */
static void inverse_channel_transform(WMA3DecodeContext *s)
{
    int i;

    for (i = 0; i < s->num_chgroups; i++) {
        if (s->chgroup[i].transform) {
            float data[WMAPRO_MAX_CHANNELS];
            const int num_channels = s->chgroup[i].num_channels;
            float** ch_data = s->chgroup[i].channel_data;
            float** ch_end = ch_data + num_channels;
            const int8_t* tb = s->chgroup[i].transform_band;
            int16_t* sfb;

            /** multichannel decorrelation */
            for (sfb = s->cur_sfb_offsets;
                sfb < s->cur_sfb_offsets + s->num_bands;sfb++) {
                int y;
                if (*tb++ == 1) {
                    /** multiply values with the decorrelation_matrix */
                    for (y = sfb[0]; y < FFMIN(sfb[1], s->subframe_len); y++) {
                        const float* mat = s->chgroup[i].decorrelation_matrix;
                        const float* data_end = data + num_channels;
                        float* data_ptr = data;
                        float** ch;

                        for (ch = ch_data; ch < ch_end; ch++)
                           *data_ptr++ = (*ch)[y];

                        for (ch = ch_data; ch < ch_end; ch++) {
                            float sum = 0;
                            data_ptr = data;
                            while (data_ptr < data_end)
                                sum += *data_ptr++ * *mat++;

                            (*ch)[y] = sum;
                        }
                    }
                } else if (s->num_channels == 2) {
                    for (y = sfb[0]; y < FFMIN(sfb[1], s->subframe_len); y++) {
                        ch_data[0][y] *= 181.0 / 128;
                        ch_data[1][y] *= 181.0 / 128;
                    }
                }
            }
        }
    }
}

