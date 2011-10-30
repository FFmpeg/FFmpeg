/*
 * MLP decoder
 * Copyright (c) 2007-2008 Ian Caulfield
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * MLP decoder
 */

#include <stdint.h>

#include "avcodec.h"
#include "dsputil.h"
#include "libavutil/intreadwrite.h"
#include "get_bits.h"
#include "libavutil/crc.h"
#include "parser.h"
#include "mlp_parser.h"
#include "mlp.h"

/** number of bits used for VLC lookup - longest Huffman code is 9 */
#define VLC_BITS            9


static const char* sample_message =
    "Please file a bug report following the instructions at "
    "http://libav.org/bugreports.html and include "
    "a sample of this file.";

typedef struct SubStream {
    /// Set if a valid restart header has been read. Otherwise the substream cannot be decoded.
    uint8_t     restart_seen;

    //@{
    /** restart header data */
    /// The type of noise to be used in the rematrix stage.
    uint16_t    noise_type;

    /// The index of the first channel coded in this substream.
    uint8_t     min_channel;
    /// The index of the last channel coded in this substream.
    uint8_t     max_channel;
    /// The number of channels input into the rematrix stage.
    uint8_t     max_matrix_channel;
    /// For each channel output by the matrix, the output channel to map it to
    uint8_t     ch_assign[MAX_CHANNELS];

    /// Channel coding parameters for channels in the substream
    ChannelParams channel_params[MAX_CHANNELS];

    /// The left shift applied to random noise in 0x31ea substreams.
    uint8_t     noise_shift;
    /// The current seed value for the pseudorandom noise generator(s).
    uint32_t    noisegen_seed;

    /// Set if the substream contains extra info to check the size of VLC blocks.
    uint8_t     data_check_present;

    /// Bitmask of which parameter sets are conveyed in a decoding parameter block.
    uint8_t     param_presence_flags;
#define PARAM_BLOCKSIZE     (1 << 7)
#define PARAM_MATRIX        (1 << 6)
#define PARAM_OUTSHIFT      (1 << 5)
#define PARAM_QUANTSTEP     (1 << 4)
#define PARAM_FIR           (1 << 3)
#define PARAM_IIR           (1 << 2)
#define PARAM_HUFFOFFSET    (1 << 1)
#define PARAM_PRESENCE      (1 << 0)
    //@}

    //@{
    /** matrix data */

    /// Number of matrices to be applied.
    uint8_t     num_primitive_matrices;

    /// matrix output channel
    uint8_t     matrix_out_ch[MAX_MATRICES];

    /// Whether the LSBs of the matrix output are encoded in the bitstream.
    uint8_t     lsb_bypass[MAX_MATRICES];
    /// Matrix coefficients, stored as 2.14 fixed point.
    int32_t     matrix_coeff[MAX_MATRICES][MAX_CHANNELS];
    /// Left shift to apply to noise values in 0x31eb substreams.
    uint8_t     matrix_noise_shift[MAX_MATRICES];
    //@}

    /// Left shift to apply to Huffman-decoded residuals.
    uint8_t     quant_step_size[MAX_CHANNELS];

    /// number of PCM samples in current audio block
    uint16_t    blocksize;
    /// Number of PCM samples decoded so far in this frame.
    uint16_t    blockpos;

    /// Left shift to apply to decoded PCM values to get final 24-bit output.
    int8_t      output_shift[MAX_CHANNELS];

    /// Running XOR of all output samples.
    int32_t     lossless_check_data;

} SubStream;

typedef struct MLPDecodeContext {
    AVCodecContext *avctx;
    AVFrame     frame;

    /// Current access unit being read has a major sync.
    int         is_major_sync_unit;

    /// Set if a valid major sync block has been read. Otherwise no decoding is possible.
    uint8_t     params_valid;

    /// Number of substreams contained within this stream.
    uint8_t     num_substreams;

    /// Index of the last substream to decode - further substreams are skipped.
    uint8_t     max_decoded_substream;

    /// number of PCM samples contained in each frame
    int         access_unit_size;
    /// next power of two above the number of samples in each frame
    int         access_unit_size_pow2;

    SubStream   substream[MAX_SUBSTREAMS];

    int         matrix_changed;
    int         filter_changed[MAX_CHANNELS][NUM_FILTERS];

    int8_t      noise_buffer[MAX_BLOCKSIZE_POW2];
    int8_t      bypassed_lsbs[MAX_BLOCKSIZE][MAX_CHANNELS];
    int32_t     sample_buffer[MAX_BLOCKSIZE][MAX_CHANNELS];

    DSPContext  dsp;
} MLPDecodeContext;

static VLC huff_vlc[3];

/** Initialize static data, constant between all invocations of the codec. */

static av_cold void init_static(void)
{
    if (!huff_vlc[0].bits) {
        INIT_VLC_STATIC(&huff_vlc[0], VLC_BITS, 18,
                    &ff_mlp_huffman_tables[0][0][1], 2, 1,
                    &ff_mlp_huffman_tables[0][0][0], 2, 1, 512);
        INIT_VLC_STATIC(&huff_vlc[1], VLC_BITS, 16,
                    &ff_mlp_huffman_tables[1][0][1], 2, 1,
                    &ff_mlp_huffman_tables[1][0][0], 2, 1, 512);
        INIT_VLC_STATIC(&huff_vlc[2], VLC_BITS, 15,
                    &ff_mlp_huffman_tables[2][0][1], 2, 1,
                    &ff_mlp_huffman_tables[2][0][0], 2, 1, 512);
    }

    ff_mlp_init_crc();
}

static inline int32_t calculate_sign_huff(MLPDecodeContext *m,
                                          unsigned int substr, unsigned int ch)
{
    SubStream *s = &m->substream[substr];
    ChannelParams *cp = &s->channel_params[ch];
    int lsb_bits = cp->huff_lsbs - s->quant_step_size[ch];
    int sign_shift = lsb_bits + (cp->codebook ? 2 - cp->codebook : -1);
    int32_t sign_huff_offset = cp->huff_offset;

    if (cp->codebook > 0)
        sign_huff_offset -= 7 << lsb_bits;

    if (sign_shift >= 0)
        sign_huff_offset -= 1 << sign_shift;

    return sign_huff_offset;
}

/** Read a sample, consisting of either, both or neither of entropy-coded MSBs
 *  and plain LSBs. */

static inline int read_huff_channels(MLPDecodeContext *m, GetBitContext *gbp,
                                     unsigned int substr, unsigned int pos)
{
    SubStream *s = &m->substream[substr];
    unsigned int mat, channel;

    for (mat = 0; mat < s->num_primitive_matrices; mat++)
        if (s->lsb_bypass[mat])
            m->bypassed_lsbs[pos + s->blockpos][mat] = get_bits1(gbp);

    for (channel = s->min_channel; channel <= s->max_channel; channel++) {
        ChannelParams *cp = &s->channel_params[channel];
        int codebook = cp->codebook;
        int quant_step_size = s->quant_step_size[channel];
        int lsb_bits = cp->huff_lsbs - quant_step_size;
        int result = 0;

        if (codebook > 0)
            result = get_vlc2(gbp, huff_vlc[codebook-1].table,
                            VLC_BITS, (9 + VLC_BITS - 1) / VLC_BITS);

        if (result < 0)
            return AVERROR_INVALIDDATA;

        if (lsb_bits > 0)
            result = (result << lsb_bits) + get_bits(gbp, lsb_bits);

        result  += cp->sign_huff_offset;
        result <<= quant_step_size;

        m->sample_buffer[pos + s->blockpos][channel] = result;
    }

    return 0;
}

static av_cold int mlp_decode_init(AVCodecContext *avctx)
{
    MLPDecodeContext *m = avctx->priv_data;
    int substr;

    init_static();
    m->avctx = avctx;
    for (substr = 0; substr < MAX_SUBSTREAMS; substr++)
        m->substream[substr].lossless_check_data = 0xffffffff;
    dsputil_init(&m->dsp, avctx);

    avcodec_get_frame_defaults(&m->frame);
    avctx->coded_frame = &m->frame;

    return 0;
}

/** Read a major sync info header - contains high level information about
 *  the stream - sample rate, channel arrangement etc. Most of this
 *  information is not actually necessary for decoding, only for playback.
 */

static int read_major_sync(MLPDecodeContext *m, GetBitContext *gb)
{
    MLPHeaderInfo mh;
    int substr, ret;

    if ((ret = ff_mlp_read_major_sync(m->avctx, &mh, gb)) != 0)
        return ret;

    if (mh.group1_bits == 0) {
        av_log(m->avctx, AV_LOG_ERROR, "invalid/unknown bits per sample\n");
        return AVERROR_INVALIDDATA;
    }
    if (mh.group2_bits > mh.group1_bits) {
        av_log(m->avctx, AV_LOG_ERROR,
               "Channel group 2 cannot have more bits per sample than group 1.\n");
        return AVERROR_INVALIDDATA;
    }

    if (mh.group2_samplerate && mh.group2_samplerate != mh.group1_samplerate) {
        av_log(m->avctx, AV_LOG_ERROR,
               "Channel groups with differing sample rates are not currently supported.\n");
        return AVERROR_INVALIDDATA;
    }

    if (mh.group1_samplerate == 0) {
        av_log(m->avctx, AV_LOG_ERROR, "invalid/unknown sampling rate\n");
        return AVERROR_INVALIDDATA;
    }
    if (mh.group1_samplerate > MAX_SAMPLERATE) {
        av_log(m->avctx, AV_LOG_ERROR,
               "Sampling rate %d is greater than the supported maximum (%d).\n",
               mh.group1_samplerate, MAX_SAMPLERATE);
        return AVERROR_INVALIDDATA;
    }
    if (mh.access_unit_size > MAX_BLOCKSIZE) {
        av_log(m->avctx, AV_LOG_ERROR,
               "Block size %d is greater than the supported maximum (%d).\n",
               mh.access_unit_size, MAX_BLOCKSIZE);
        return AVERROR_INVALIDDATA;
    }
    if (mh.access_unit_size_pow2 > MAX_BLOCKSIZE_POW2) {
        av_log(m->avctx, AV_LOG_ERROR,
               "Block size pow2 %d is greater than the supported maximum (%d).\n",
               mh.access_unit_size_pow2, MAX_BLOCKSIZE_POW2);
        return AVERROR_INVALIDDATA;
    }

    if (mh.num_substreams == 0)
        return AVERROR_INVALIDDATA;
    if (m->avctx->codec_id == CODEC_ID_MLP && mh.num_substreams > 2) {
        av_log(m->avctx, AV_LOG_ERROR, "MLP only supports up to 2 substreams.\n");
        return AVERROR_INVALIDDATA;
    }
    if (mh.num_substreams > MAX_SUBSTREAMS) {
        av_log(m->avctx, AV_LOG_ERROR,
               "Number of substreams %d is larger than the maximum supported "
               "by the decoder. %s\n", mh.num_substreams, sample_message);
        return AVERROR_INVALIDDATA;
    }

    m->access_unit_size      = mh.access_unit_size;
    m->access_unit_size_pow2 = mh.access_unit_size_pow2;

    m->num_substreams        = mh.num_substreams;
    m->max_decoded_substream = m->num_substreams - 1;

    m->avctx->sample_rate    = mh.group1_samplerate;
    m->avctx->frame_size     = mh.access_unit_size;

    m->avctx->bits_per_raw_sample = mh.group1_bits;
    if (mh.group1_bits > 16)
        m->avctx->sample_fmt = AV_SAMPLE_FMT_S32;
    else
        m->avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    m->params_valid = 1;
    for (substr = 0; substr < MAX_SUBSTREAMS; substr++)
        m->substream[substr].restart_seen = 0;

    return 0;
}

/** Read a restart header from a block in a substream. This contains parameters
 *  required to decode the audio that do not change very often. Generally
 *  (always) present only in blocks following a major sync. */

static int read_restart_header(MLPDecodeContext *m, GetBitContext *gbp,
                               const uint8_t *buf, unsigned int substr)
{
    SubStream *s = &m->substream[substr];
    unsigned int ch;
    int sync_word, tmp;
    uint8_t checksum;
    uint8_t lossless_check;
    int start_count = get_bits_count(gbp);
    const int max_matrix_channel = m->avctx->codec_id == CODEC_ID_MLP
                                 ? MAX_MATRIX_CHANNEL_MLP
                                 : MAX_MATRIX_CHANNEL_TRUEHD;

    sync_word = get_bits(gbp, 13);

    if (sync_word != 0x31ea >> 1) {
        av_log(m->avctx, AV_LOG_ERROR,
               "restart header sync incorrect (got 0x%04x)\n", sync_word);
        return AVERROR_INVALIDDATA;
    }

    s->noise_type = get_bits1(gbp);

    if (m->avctx->codec_id == CODEC_ID_MLP && s->noise_type) {
        av_log(m->avctx, AV_LOG_ERROR, "MLP must have 0x31ea sync word.\n");
        return AVERROR_INVALIDDATA;
    }

    skip_bits(gbp, 16); /* Output timestamp */

    s->min_channel        = get_bits(gbp, 4);
    s->max_channel        = get_bits(gbp, 4);
    s->max_matrix_channel = get_bits(gbp, 4);

    if (s->max_matrix_channel > max_matrix_channel) {
        av_log(m->avctx, AV_LOG_ERROR,
               "Max matrix channel cannot be greater than %d.\n",
               max_matrix_channel);
        return AVERROR_INVALIDDATA;
    }

    if (s->max_channel != s->max_matrix_channel) {
        av_log(m->avctx, AV_LOG_ERROR,
               "Max channel must be equal max matrix channel.\n");
        return AVERROR_INVALIDDATA;
    }

    /* This should happen for TrueHD streams with >6 channels and MLP's noise
     * type. It is not yet known if this is allowed. */
    if (s->max_channel > MAX_MATRIX_CHANNEL_MLP && !s->noise_type) {
        av_log(m->avctx, AV_LOG_ERROR,
               "Number of channels %d is larger than the maximum supported "
               "by the decoder. %s\n", s->max_channel+2, sample_message);
        return AVERROR_INVALIDDATA;
    }

    if (s->min_channel > s->max_channel) {
        av_log(m->avctx, AV_LOG_ERROR,
               "Substream min channel cannot be greater than max channel.\n");
        return AVERROR_INVALIDDATA;
    }

    if (m->avctx->request_channels > 0
        && s->max_channel + 1 >= m->avctx->request_channels
        && substr < m->max_decoded_substream) {
        av_log(m->avctx, AV_LOG_DEBUG,
               "Extracting %d channel downmix from substream %d. "
               "Further substreams will be skipped.\n",
               s->max_channel + 1, substr);
        m->max_decoded_substream = substr;
    }

    s->noise_shift   = get_bits(gbp,  4);
    s->noisegen_seed = get_bits(gbp, 23);

    skip_bits(gbp, 19);

    s->data_check_present = get_bits1(gbp);
    lossless_check = get_bits(gbp, 8);
    if (substr == m->max_decoded_substream
        && s->lossless_check_data != 0xffffffff) {
        tmp = xor_32_to_8(s->lossless_check_data);
        if (tmp != lossless_check)
            av_log(m->avctx, AV_LOG_WARNING,
                   "Lossless check failed - expected %02x, calculated %02x.\n",
                   lossless_check, tmp);
    }

    skip_bits(gbp, 16);

    memset(s->ch_assign, 0, sizeof(s->ch_assign));

    for (ch = 0; ch <= s->max_matrix_channel; ch++) {
        int ch_assign = get_bits(gbp, 6);
        if (ch_assign > s->max_matrix_channel) {
            av_log(m->avctx, AV_LOG_ERROR,
                   "Assignment of matrix channel %d to invalid output channel %d. %s\n",
                   ch, ch_assign, sample_message);
            return AVERROR_INVALIDDATA;
        }
        s->ch_assign[ch_assign] = ch;
    }

    checksum = ff_mlp_restart_checksum(buf, get_bits_count(gbp) - start_count);

    if (checksum != get_bits(gbp, 8))
        av_log(m->avctx, AV_LOG_ERROR, "restart header checksum error\n");

    /* Set default decoding parameters. */
    s->param_presence_flags   = 0xff;
    s->num_primitive_matrices = 0;
    s->blocksize              = 8;
    s->lossless_check_data    = 0;

    memset(s->output_shift   , 0, sizeof(s->output_shift   ));
    memset(s->quant_step_size, 0, sizeof(s->quant_step_size));

    for (ch = s->min_channel; ch <= s->max_channel; ch++) {
        ChannelParams *cp = &s->channel_params[ch];
        cp->filter_params[FIR].order = 0;
        cp->filter_params[IIR].order = 0;
        cp->filter_params[FIR].shift = 0;
        cp->filter_params[IIR].shift = 0;

        /* Default audio coding is 24-bit raw PCM. */
        cp->huff_offset      = 0;
        cp->sign_huff_offset = (-1) << 23;
        cp->codebook         = 0;
        cp->huff_lsbs        = 24;
    }

    if (substr == m->max_decoded_substream)
        m->avctx->channels = s->max_matrix_channel + 1;

    return 0;
}

/** Read parameters for one of the prediction filters. */

static int read_filter_params(MLPDecodeContext *m, GetBitContext *gbp,
                              unsigned int substr, unsigned int channel,
                              unsigned int filter)
{
    SubStream *s = &m->substream[substr];
    FilterParams *fp = &s->channel_params[channel].filter_params[filter];
    const int max_order = filter ? MAX_IIR_ORDER : MAX_FIR_ORDER;
    const char fchar = filter ? 'I' : 'F';
    int i, order;

    // Filter is 0 for FIR, 1 for IIR.
    assert(filter < 2);

    if (m->filter_changed[channel][filter]++ > 1) {
        av_log(m->avctx, AV_LOG_ERROR, "Filters may change only once per access unit.\n");
        return AVERROR_INVALIDDATA;
    }

    order = get_bits(gbp, 4);
    if (order > max_order) {
        av_log(m->avctx, AV_LOG_ERROR,
               "%cIR filter order %d is greater than maximum %d.\n",
               fchar, order, max_order);
        return AVERROR_INVALIDDATA;
    }
    fp->order = order;

    if (order > 0) {
        int32_t *fcoeff = s->channel_params[channel].coeff[filter];
        int coeff_bits, coeff_shift;

        fp->shift = get_bits(gbp, 4);

        coeff_bits  = get_bits(gbp, 5);
        coeff_shift = get_bits(gbp, 3);
        if (coeff_bits < 1 || coeff_bits > 16) {
            av_log(m->avctx, AV_LOG_ERROR,
                   "%cIR filter coeff_bits must be between 1 and 16.\n",
                   fchar);
            return AVERROR_INVALIDDATA;
        }
        if (coeff_bits + coeff_shift > 16) {
            av_log(m->avctx, AV_LOG_ERROR,
                   "Sum of coeff_bits and coeff_shift for %cIR filter must be 16 or less.\n",
                   fchar);
            return AVERROR_INVALIDDATA;
        }

        for (i = 0; i < order; i++)
            fcoeff[i] = get_sbits(gbp, coeff_bits) << coeff_shift;

        if (get_bits1(gbp)) {
            int state_bits, state_shift;

            if (filter == FIR) {
                av_log(m->avctx, AV_LOG_ERROR,
                       "FIR filter has state data specified.\n");
                return AVERROR_INVALIDDATA;
            }

            state_bits  = get_bits(gbp, 4);
            state_shift = get_bits(gbp, 4);

            /* TODO: Check validity of state data. */

            for (i = 0; i < order; i++)
                fp->state[i] = get_sbits(gbp, state_bits) << state_shift;
        }
    }

    return 0;
}

/** Read parameters for primitive matrices. */

static int read_matrix_params(MLPDecodeContext *m, unsigned int substr, GetBitContext *gbp)
{
    SubStream *s = &m->substream[substr];
    unsigned int mat, ch;
    const int max_primitive_matrices = m->avctx->codec_id == CODEC_ID_MLP
                                     ? MAX_MATRICES_MLP
                                     : MAX_MATRICES_TRUEHD;

    if (m->matrix_changed++ > 1) {
        av_log(m->avctx, AV_LOG_ERROR, "Matrices may change only once per access unit.\n");
        return AVERROR_INVALIDDATA;
    }

    s->num_primitive_matrices = get_bits(gbp, 4);

    if (s->num_primitive_matrices > max_primitive_matrices) {
        av_log(m->avctx, AV_LOG_ERROR,
               "Number of primitive matrices cannot be greater than %d.\n",
               max_primitive_matrices);
        return AVERROR_INVALIDDATA;
    }

    for (mat = 0; mat < s->num_primitive_matrices; mat++) {
        int frac_bits, max_chan;
        s->matrix_out_ch[mat] = get_bits(gbp, 4);
        frac_bits             = get_bits(gbp, 4);
        s->lsb_bypass   [mat] = get_bits1(gbp);

        if (s->matrix_out_ch[mat] > s->max_matrix_channel) {
            av_log(m->avctx, AV_LOG_ERROR,
                    "Invalid channel %d specified as output from matrix.\n",
                    s->matrix_out_ch[mat]);
            return AVERROR_INVALIDDATA;
        }
        if (frac_bits > 14) {
            av_log(m->avctx, AV_LOG_ERROR,
                    "Too many fractional bits specified.\n");
            return AVERROR_INVALIDDATA;
        }

        max_chan = s->max_matrix_channel;
        if (!s->noise_type)
            max_chan+=2;

        for (ch = 0; ch <= max_chan; ch++) {
            int coeff_val = 0;
            if (get_bits1(gbp))
                coeff_val = get_sbits(gbp, frac_bits + 2);

            s->matrix_coeff[mat][ch] = coeff_val << (14 - frac_bits);
        }

        if (s->noise_type)
            s->matrix_noise_shift[mat] = get_bits(gbp, 4);
        else
            s->matrix_noise_shift[mat] = 0;
    }

    return 0;
}

/** Read channel parameters. */

static int read_channel_params(MLPDecodeContext *m, unsigned int substr,
                               GetBitContext *gbp, unsigned int ch)
{
    SubStream *s = &m->substream[substr];
    ChannelParams *cp = &s->channel_params[ch];
    FilterParams *fir = &cp->filter_params[FIR];
    FilterParams *iir = &cp->filter_params[IIR];
    int ret;

    if (s->param_presence_flags & PARAM_FIR)
        if (get_bits1(gbp))
            if ((ret = read_filter_params(m, gbp, substr, ch, FIR)) < 0)
                return ret;

    if (s->param_presence_flags & PARAM_IIR)
        if (get_bits1(gbp))
            if ((ret = read_filter_params(m, gbp, substr, ch, IIR)) < 0)
                return ret;

    if (fir->order + iir->order > 8) {
        av_log(m->avctx, AV_LOG_ERROR, "Total filter orders too high.\n");
        return AVERROR_INVALIDDATA;
    }

    if (fir->order && iir->order &&
        fir->shift != iir->shift) {
        av_log(m->avctx, AV_LOG_ERROR,
                "FIR and IIR filters must use the same precision.\n");
        return AVERROR_INVALIDDATA;
    }
    /* The FIR and IIR filters must have the same precision.
     * To simplify the filtering code, only the precision of the
     * FIR filter is considered. If only the IIR filter is employed,
     * the FIR filter precision is set to that of the IIR filter, so
     * that the filtering code can use it. */
    if (!fir->order && iir->order)
        fir->shift = iir->shift;

    if (s->param_presence_flags & PARAM_HUFFOFFSET)
        if (get_bits1(gbp))
            cp->huff_offset = get_sbits(gbp, 15);

    cp->codebook  = get_bits(gbp, 2);
    cp->huff_lsbs = get_bits(gbp, 5);

    if (cp->huff_lsbs > 24) {
        av_log(m->avctx, AV_LOG_ERROR, "Invalid huff_lsbs.\n");
        return AVERROR_INVALIDDATA;
    }

    cp->sign_huff_offset = calculate_sign_huff(m, substr, ch);

    return 0;
}

/** Read decoding parameters that change more often than those in the restart
 *  header. */

static int read_decoding_params(MLPDecodeContext *m, GetBitContext *gbp,
                                unsigned int substr)
{
    SubStream *s = &m->substream[substr];
    unsigned int ch;
    int ret;

    if (s->param_presence_flags & PARAM_PRESENCE)
        if (get_bits1(gbp))
            s->param_presence_flags = get_bits(gbp, 8);

    if (s->param_presence_flags & PARAM_BLOCKSIZE)
        if (get_bits1(gbp)) {
            s->blocksize = get_bits(gbp, 9);
            if (s->blocksize < 8 || s->blocksize > m->access_unit_size) {
                av_log(m->avctx, AV_LOG_ERROR, "Invalid blocksize.");
                s->blocksize = 0;
                return AVERROR_INVALIDDATA;
            }
        }

    if (s->param_presence_flags & PARAM_MATRIX)
        if (get_bits1(gbp))
            if ((ret = read_matrix_params(m, substr, gbp)) < 0)
                return ret;

    if (s->param_presence_flags & PARAM_OUTSHIFT)
        if (get_bits1(gbp))
            for (ch = 0; ch <= s->max_matrix_channel; ch++)
                s->output_shift[ch] = get_sbits(gbp, 4);

    if (s->param_presence_flags & PARAM_QUANTSTEP)
        if (get_bits1(gbp))
            for (ch = 0; ch <= s->max_channel; ch++) {
                ChannelParams *cp = &s->channel_params[ch];

                s->quant_step_size[ch] = get_bits(gbp, 4);

                cp->sign_huff_offset = calculate_sign_huff(m, substr, ch);
            }

    for (ch = s->min_channel; ch <= s->max_channel; ch++)
        if (get_bits1(gbp))
            if ((ret = read_channel_params(m, substr, gbp, ch)) < 0)
                return ret;

    return 0;
}

#define MSB_MASK(bits)  (-1u << bits)

/** Generate PCM samples using the prediction filters and residual values
 *  read from the data stream, and update the filter state. */

static void filter_channel(MLPDecodeContext *m, unsigned int substr,
                           unsigned int channel)
{
    SubStream *s = &m->substream[substr];
    const int32_t *fircoeff = s->channel_params[channel].coeff[FIR];
    int32_t state_buffer[NUM_FILTERS][MAX_BLOCKSIZE + MAX_FIR_ORDER];
    int32_t *firbuf = state_buffer[FIR] + MAX_BLOCKSIZE;
    int32_t *iirbuf = state_buffer[IIR] + MAX_BLOCKSIZE;
    FilterParams *fir = &s->channel_params[channel].filter_params[FIR];
    FilterParams *iir = &s->channel_params[channel].filter_params[IIR];
    unsigned int filter_shift = fir->shift;
    int32_t mask = MSB_MASK(s->quant_step_size[channel]);

    memcpy(firbuf, fir->state, MAX_FIR_ORDER * sizeof(int32_t));
    memcpy(iirbuf, iir->state, MAX_IIR_ORDER * sizeof(int32_t));

    m->dsp.mlp_filter_channel(firbuf, fircoeff,
                              fir->order, iir->order,
                              filter_shift, mask, s->blocksize,
                              &m->sample_buffer[s->blockpos][channel]);

    memcpy(fir->state, firbuf - s->blocksize, MAX_FIR_ORDER * sizeof(int32_t));
    memcpy(iir->state, iirbuf - s->blocksize, MAX_IIR_ORDER * sizeof(int32_t));
}

/** Read a block of PCM residual data (or actual if no filtering active). */

static int read_block_data(MLPDecodeContext *m, GetBitContext *gbp,
                           unsigned int substr)
{
    SubStream *s = &m->substream[substr];
    unsigned int i, ch, expected_stream_pos = 0;
    int ret;

    if (s->data_check_present) {
        expected_stream_pos  = get_bits_count(gbp);
        expected_stream_pos += get_bits(gbp, 16);
        av_log(m->avctx, AV_LOG_WARNING, "This file contains some features "
               "we have not tested yet. %s\n", sample_message);
    }

    if (s->blockpos + s->blocksize > m->access_unit_size) {
        av_log(m->avctx, AV_LOG_ERROR, "too many audio samples in frame\n");
        return AVERROR_INVALIDDATA;
    }

    memset(&m->bypassed_lsbs[s->blockpos][0], 0,
           s->blocksize * sizeof(m->bypassed_lsbs[0]));

    for (i = 0; i < s->blocksize; i++)
        if ((ret = read_huff_channels(m, gbp, substr, i)) < 0)
            return ret;

    for (ch = s->min_channel; ch <= s->max_channel; ch++)
        filter_channel(m, substr, ch);

    s->blockpos += s->blocksize;

    if (s->data_check_present) {
        if (get_bits_count(gbp) != expected_stream_pos)
            av_log(m->avctx, AV_LOG_ERROR, "block data length mismatch\n");
        skip_bits(gbp, 8);
    }

    return 0;
}

/** Data table used for TrueHD noise generation function. */

static const int8_t noise_table[256] = {
     30,  51,  22,  54,   3,   7,  -4,  38,  14,  55,  46,  81,  22,  58,  -3,   2,
     52,  31,  -7,  51,  15,  44,  74,  30,  85, -17,  10,  33,  18,  80,  28,  62,
     10,  32,  23,  69,  72,  26,  35,  17,  73,  60,   8,  56,   2,   6,  -2,  -5,
     51,   4,  11,  50,  66,  76,  21,  44,  33,  47,   1,  26,  64,  48,  57,  40,
     38,  16, -10, -28,  92,  22, -18,  29, -10,   5, -13,  49,  19,  24,  70,  34,
     61,  48,  30,  14,  -6,  25,  58,  33,  42,  60,  67,  17,  54,  17,  22,  30,
     67,  44,  -9,  50, -11,  43,  40,  32,  59,  82,  13,  49, -14,  55,  60,  36,
     48,  49,  31,  47,  15,  12,   4,  65,   1,  23,  29,  39,  45,  -2,  84,  69,
      0,  72,  37,  57,  27,  41, -15, -16,  35,  31,  14,  61,  24,   0,  27,  24,
     16,  41,  55,  34,  53,   9,  56,  12,  25,  29,  53,   5,  20, -20,  -8,  20,
     13,  28,  -3,  78,  38,  16,  11,  62,  46,  29,  21,  24,  46,  65,  43, -23,
     89,  18,  74,  21,  38, -12,  19,  12, -19,   8,  15,  33,   4,  57,   9,  -8,
     36,  35,  26,  28,   7,  83,  63,  79,  75,  11,   3,  87,  37,  47,  34,  40,
     39,  19,  20,  42,  27,  34,  39,  77,  13,  42,  59,  64,  45,  -1,  32,  37,
     45,  -5,  53,  -6,   7,  36,  50,  23,   6,  32,   9, -21,  18,  71,  27,  52,
    -25,  31,  35,  42,  -1,  68,  63,  52,  26,  43,  66,  37,  41,  25,  40,  70,
};

/** Noise generation functions.
 *  I'm not sure what these are for - they seem to be some kind of pseudorandom
 *  sequence generators, used to generate noise data which is used when the
 *  channels are rematrixed. I'm not sure if they provide a practical benefit
 *  to compression, or just obfuscate the decoder. Are they for some kind of
 *  dithering? */

/** Generate two channels of noise, used in the matrix when
 *  restart sync word == 0x31ea. */

static void generate_2_noise_channels(MLPDecodeContext *m, unsigned int substr)
{
    SubStream *s = &m->substream[substr];
    unsigned int i;
    uint32_t seed = s->noisegen_seed;
    unsigned int maxchan = s->max_matrix_channel;

    for (i = 0; i < s->blockpos; i++) {
        uint16_t seed_shr7 = seed >> 7;
        m->sample_buffer[i][maxchan+1] = ((int8_t)(seed >> 15)) << s->noise_shift;
        m->sample_buffer[i][maxchan+2] = ((int8_t) seed_shr7)   << s->noise_shift;

        seed = (seed << 16) ^ seed_shr7 ^ (seed_shr7 << 5);
    }

    s->noisegen_seed = seed;
}

/** Generate a block of noise, used when restart sync word == 0x31eb. */

static void fill_noise_buffer(MLPDecodeContext *m, unsigned int substr)
{
    SubStream *s = &m->substream[substr];
    unsigned int i;
    uint32_t seed = s->noisegen_seed;

    for (i = 0; i < m->access_unit_size_pow2; i++) {
        uint8_t seed_shr15 = seed >> 15;
        m->noise_buffer[i] = noise_table[seed_shr15];
        seed = (seed << 8) ^ seed_shr15 ^ (seed_shr15 << 5);
    }

    s->noisegen_seed = seed;
}


/** Apply the channel matrices in turn to reconstruct the original audio
 *  samples. */

static void rematrix_channels(MLPDecodeContext *m, unsigned int substr)
{
    SubStream *s = &m->substream[substr];
    unsigned int mat, src_ch, i;
    unsigned int maxchan;

    maxchan = s->max_matrix_channel;
    if (!s->noise_type) {
        generate_2_noise_channels(m, substr);
        maxchan += 2;
    } else {
        fill_noise_buffer(m, substr);
    }

    for (mat = 0; mat < s->num_primitive_matrices; mat++) {
        int matrix_noise_shift = s->matrix_noise_shift[mat];
        unsigned int dest_ch = s->matrix_out_ch[mat];
        int32_t mask = MSB_MASK(s->quant_step_size[dest_ch]);
        int32_t *coeffs = s->matrix_coeff[mat];
        int index  = s->num_primitive_matrices - mat;
        int index2 = 2 * index + 1;

        /* TODO: DSPContext? */

        for (i = 0; i < s->blockpos; i++) {
            int32_t bypassed_lsb = m->bypassed_lsbs[i][mat];
            int32_t *samples = m->sample_buffer[i];
            int64_t accum = 0;

            for (src_ch = 0; src_ch <= maxchan; src_ch++)
                accum += (int64_t) samples[src_ch] * coeffs[src_ch];

            if (matrix_noise_shift) {
                index &= m->access_unit_size_pow2 - 1;
                accum += m->noise_buffer[index] << (matrix_noise_shift + 7);
                index += index2;
            }

            samples[dest_ch] = ((accum >> 14) & mask) + bypassed_lsb;
        }
    }
}

/** Write the audio data into the output buffer. */

static int output_data(MLPDecodeContext *m, unsigned int substr,
                       void *data, int *got_frame_ptr)
{
    AVCodecContext *avctx = m->avctx;
    SubStream *s = &m->substream[substr];
    unsigned int i, out_ch = 0;
    int32_t *data_32;
    int16_t *data_16;
    int ret;
    int is32 = (m->avctx->sample_fmt == AV_SAMPLE_FMT_S32);

    if (m->avctx->channels != s->max_matrix_channel + 1) {
        av_log(m->avctx, AV_LOG_ERROR, "channel count mismatch\n");
        return AVERROR_INVALIDDATA;
    }

    /* get output buffer */
    m->frame.nb_samples = s->blockpos;
    if ((ret = avctx->get_buffer(avctx, &m->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    data_32 = (int32_t *)m->frame.data[0];
    data_16 = (int16_t *)m->frame.data[0];

    for (i = 0; i < s->blockpos; i++) {
        for (out_ch = 0; out_ch <= s->max_matrix_channel; out_ch++) {
            int mat_ch = s->ch_assign[out_ch];
            int32_t sample = m->sample_buffer[i][mat_ch]
                          << s->output_shift[mat_ch];
            s->lossless_check_data ^= (sample & 0xffffff) << mat_ch;
            if (is32) *data_32++ = sample << 8;
            else      *data_16++ = sample >> 8;
        }
    }

    *got_frame_ptr   = 1;
    *(AVFrame *)data = m->frame;

    return 0;
}

/** Read an access unit from the stream.
 *  @return negative on error, 0 if not enough data is present in the input stream,
 *  otherwise the number of bytes consumed. */

static int read_access_unit(AVCodecContext *avctx, void* data,
                            int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    MLPDecodeContext *m = avctx->priv_data;
    GetBitContext gb;
    unsigned int length, substr;
    unsigned int substream_start;
    unsigned int header_size = 4;
    unsigned int substr_header_size = 0;
    uint8_t substream_parity_present[MAX_SUBSTREAMS];
    uint16_t substream_data_len[MAX_SUBSTREAMS];
    uint8_t parity_bits;
    int ret;

    if (buf_size < 4)
        return 0;

    length = (AV_RB16(buf) & 0xfff) * 2;

    if (length < 4 || length > buf_size)
        return AVERROR_INVALIDDATA;

    init_get_bits(&gb, (buf + 4), (length - 4) * 8);

    m->is_major_sync_unit = 0;
    if (show_bits_long(&gb, 31) == (0xf8726fba >> 1)) {
        if (read_major_sync(m, &gb) < 0)
            goto error;
        m->is_major_sync_unit = 1;
        header_size += 28;
    }

    if (!m->params_valid) {
        av_log(m->avctx, AV_LOG_WARNING,
               "Stream parameters not seen; skipping frame.\n");
        *got_frame_ptr = 0;
        return length;
    }

    substream_start = 0;

    for (substr = 0; substr < m->num_substreams; substr++) {
        int extraword_present, checkdata_present, end, nonrestart_substr;

        extraword_present = get_bits1(&gb);
        nonrestart_substr = get_bits1(&gb);
        checkdata_present = get_bits1(&gb);
        skip_bits1(&gb);

        end = get_bits(&gb, 12) * 2;

        substr_header_size += 2;

        if (extraword_present) {
            if (m->avctx->codec_id == CODEC_ID_MLP) {
                av_log(m->avctx, AV_LOG_ERROR, "There must be no extraword for MLP.\n");
                goto error;
            }
            skip_bits(&gb, 16);
            substr_header_size += 2;
        }

        if (!(nonrestart_substr ^ m->is_major_sync_unit)) {
            av_log(m->avctx, AV_LOG_ERROR, "Invalid nonrestart_substr.\n");
            goto error;
        }

        if (end + header_size + substr_header_size > length) {
            av_log(m->avctx, AV_LOG_ERROR,
                   "Indicated length of substream %d data goes off end of "
                   "packet.\n", substr);
            end = length - header_size - substr_header_size;
        }

        if (end < substream_start) {
            av_log(avctx, AV_LOG_ERROR,
                   "Indicated end offset of substream %d data "
                   "is smaller than calculated start offset.\n",
                   substr);
            goto error;
        }

        if (substr > m->max_decoded_substream)
            continue;

        substream_parity_present[substr] = checkdata_present;
        substream_data_len[substr] = end - substream_start;
        substream_start = end;
    }

    parity_bits  = ff_mlp_calculate_parity(buf, 4);
    parity_bits ^= ff_mlp_calculate_parity(buf + header_size, substr_header_size);

    if ((((parity_bits >> 4) ^ parity_bits) & 0xF) != 0xF) {
        av_log(avctx, AV_LOG_ERROR, "Parity check failed.\n");
        goto error;
    }

    buf += header_size + substr_header_size;

    for (substr = 0; substr <= m->max_decoded_substream; substr++) {
        SubStream *s = &m->substream[substr];
        init_get_bits(&gb, buf, substream_data_len[substr] * 8);

        m->matrix_changed = 0;
        memset(m->filter_changed, 0, sizeof(m->filter_changed));

        s->blockpos = 0;
        do {
            if (get_bits1(&gb)) {
                if (get_bits1(&gb)) {
                    /* A restart header should be present. */
                    if (read_restart_header(m, &gb, buf, substr) < 0)
                        goto next_substr;
                    s->restart_seen = 1;
                }

                if (!s->restart_seen)
                    goto next_substr;
                if (read_decoding_params(m, &gb, substr) < 0)
                    goto next_substr;
            }

            if (!s->restart_seen)
                goto next_substr;

            if ((ret = read_block_data(m, &gb, substr)) < 0)
                return ret;

            if (get_bits_count(&gb) >= substream_data_len[substr] * 8)
                goto substream_length_mismatch;

        } while (!get_bits1(&gb));

        skip_bits(&gb, (-get_bits_count(&gb)) & 15);

        if (substream_data_len[substr] * 8 - get_bits_count(&gb) >= 32) {
            int shorten_by;

            if (get_bits(&gb, 16) != 0xD234)
                return AVERROR_INVALIDDATA;

            shorten_by = get_bits(&gb, 16);
            if      (m->avctx->codec_id == CODEC_ID_TRUEHD && shorten_by  & 0x2000)
                s->blockpos -= FFMIN(shorten_by & 0x1FFF, s->blockpos);
            else if (m->avctx->codec_id == CODEC_ID_MLP    && shorten_by != 0xD234)
                return AVERROR_INVALIDDATA;

            if (substr == m->max_decoded_substream)
                av_log(m->avctx, AV_LOG_INFO, "End of stream indicated.\n");
        }

        if (substream_parity_present[substr]) {
            uint8_t parity, checksum;

            if (substream_data_len[substr] * 8 - get_bits_count(&gb) != 16)
                goto substream_length_mismatch;

            parity   = ff_mlp_calculate_parity(buf, substream_data_len[substr] - 2);
            checksum = ff_mlp_checksum8       (buf, substream_data_len[substr] - 2);

            if ((get_bits(&gb, 8) ^ parity) != 0xa9    )
                av_log(m->avctx, AV_LOG_ERROR, "Substream %d parity check failed.\n", substr);
            if ( get_bits(&gb, 8)           != checksum)
                av_log(m->avctx, AV_LOG_ERROR, "Substream %d checksum failed.\n"    , substr);
        }

        if (substream_data_len[substr] * 8 != get_bits_count(&gb))
            goto substream_length_mismatch;

next_substr:
        if (!s->restart_seen)
            av_log(m->avctx, AV_LOG_ERROR,
                   "No restart header present in substream %d.\n", substr);

        buf += substream_data_len[substr];
    }

    rematrix_channels(m, m->max_decoded_substream);

    if ((ret = output_data(m, m->max_decoded_substream, data, got_frame_ptr)) < 0)
        return ret;

    return length;

substream_length_mismatch:
    av_log(m->avctx, AV_LOG_ERROR, "substream %d length mismatch\n", substr);
    return AVERROR_INVALIDDATA;

error:
    m->params_valid = 0;
    return AVERROR_INVALIDDATA;
}

AVCodec ff_mlp_decoder = {
    .name           = "mlp",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_MLP,
    .priv_data_size = sizeof(MLPDecodeContext),
    .init           = mlp_decode_init,
    .decode         = read_access_unit,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("MLP (Meridian Lossless Packing)"),
};

#if CONFIG_TRUEHD_DECODER
AVCodec ff_truehd_decoder = {
    .name           = "truehd",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_TRUEHD,
    .priv_data_size = sizeof(MLPDecodeContext),
    .init           = mlp_decode_init,
    .decode         = read_access_unit,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("TrueHD"),
};
#endif /* CONFIG_TRUEHD_DECODER */
