/**
 * MLP encoder
 * Copyright (c) 2008 Ramiro Polla
 * Copyright (c) 2016-2019 Jai Luthra
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

#include "avcodec.h"
#include "internal.h"
#include "put_bits.h"
#include "audio_frame_queue.h"
#include "libavutil/crc.h"
#include "libavutil/avstring.h"
#include "libavutil/samplefmt.h"
#include "mlp.h"
#include "lpc.h"

#define MAJOR_HEADER_INTERVAL 16

#define MLP_MIN_LPC_ORDER      1
#define MLP_MAX_LPC_ORDER      8
#define MLP_MIN_LPC_SHIFT      8
#define MLP_MAX_LPC_SHIFT     15

typedef struct {
    uint8_t         min_channel;         ///< The index of the first channel coded in this substream.
    uint8_t         max_channel;         ///< The index of the last channel coded in this substream.
    uint8_t         max_matrix_channel;  ///< The number of channels input into the rematrix stage.

    uint8_t         noise_shift;         ///< The left shift applied to random noise in 0x31ea substreams.
    uint32_t        noisegen_seed;       ///< The current seed value for the pseudorandom noise generator(s).

    int             data_check_present;  ///< Set if the substream contains extra info to check the size of VLC blocks.

    int32_t         lossless_check_data; ///< XOR of all output samples

    uint8_t         max_huff_lsbs;       ///< largest huff_lsbs
    uint8_t         max_output_bits;     ///< largest output bit-depth
} RestartHeader;

typedef struct {
    uint8_t         count;                  ///< number of matrices to apply

    uint8_t         outch[MAX_MATRICES];    ///< output channel for each matrix
    int32_t         forco[MAX_MATRICES][MAX_CHANNELS+2];    ///< forward coefficients
    int32_t         coeff[MAX_MATRICES][MAX_CHANNELS+2];    ///< decoding coefficients
    uint8_t         fbits[MAX_CHANNELS];    ///< fraction bits

    int8_t          shift[MAX_CHANNELS];    ///< Left shift to apply to decoded PCM values to get final 24-bit output.
} MatrixParams;

enum ParamFlags {
    PARAMS_DEFAULT       = 0xff,
    PARAM_PRESENCE_FLAGS = 1 << 8,
    PARAM_BLOCKSIZE      = 1 << 7,
    PARAM_MATRIX         = 1 << 6,
    PARAM_OUTSHIFT       = 1 << 5,
    PARAM_QUANTSTEP      = 1 << 4,
    PARAM_FIR            = 1 << 3,
    PARAM_IIR            = 1 << 2,
    PARAM_HUFFOFFSET     = 1 << 1,
    PARAM_PRESENT        = 1 << 0,
};

typedef struct {
    uint16_t        blocksize;                  ///< number of PCM samples in current audio block
    uint8_t         quant_step_size[MAX_CHANNELS];  ///< left shift to apply to Huffman-decoded residuals

    MatrixParams    matrix_params;

    uint8_t         param_presence_flags;       ///< Bitmask of which parameter sets are conveyed in a decoding parameter block.
} DecodingParams;

typedef struct BestOffset {
    int32_t offset;
    int bitcount;
    int lsb_bits;
    int32_t min;
    int32_t max;
} BestOffset;

#define HUFF_OFFSET_MIN    (-16384)
#define HUFF_OFFSET_MAX    ( 16383)

/** Number of possible codebooks (counting "no codebooks") */
#define NUM_CODEBOOKS       4

typedef struct {
    AVCodecContext *avctx;

    int             num_substreams;         ///< Number of substreams contained within this stream.

    int             num_channels;   /**< Number of channels in major_scratch_buffer.
                                     *   Normal channels + noise channels. */

    int             coded_sample_fmt [2];   ///< sample format encoded for MLP
    int             coded_sample_rate[2];   ///< sample rate encoded for MLP
    int             coded_peak_bitrate;     ///< peak bitrate for this major sync header

    int             flags;                  ///< major sync info flags

    /* channel_meaning */
    int             substream_info;
    int             fs;
    int             wordlength;
    int             channel_occupancy;
    int             summary_info;

    int32_t        *inout_buffer;           ///< Pointer to data currently being read from lavc or written to bitstream.
    int32_t        *major_inout_buffer;     ///< Buffer with all in/out data for one entire major frame interval.
    int32_t        *write_buffer;           ///< Pointer to data currently being written to bitstream.
    int32_t        *sample_buffer;          ///< Pointer to current access unit samples.
    int32_t        *major_scratch_buffer;   ///< Scratch buffer big enough to fit all data for one entire major frame interval.
    int32_t        *last_frame;             ///< Pointer to last frame with data to encode.

    int32_t        *lpc_sample_buffer;

    unsigned int    major_number_of_frames;
    unsigned int    next_major_number_of_frames;

    unsigned int    major_frame_size;       ///< Number of samples in current major frame being encoded.
    unsigned int    next_major_frame_size;  ///< Counter of number of samples for next major frame.

    int32_t        *lossless_check_data;    ///< Array with lossless_check_data for each access unit.

    unsigned int   *max_output_bits;        ///< largest output bit-depth
    unsigned int   *frame_size;             ///< Array with number of samples/channel in each access unit.
    unsigned int    frame_index;            ///< Index of current frame being encoded.

    unsigned int    one_sample_buffer_size; ///< Number of samples*channel for one access unit.

    unsigned int    max_restart_interval;   ///< Max interval of access units in between two major frames.
    unsigned int    min_restart_interval;   ///< Min interval of access units in between two major frames.
    unsigned int    restart_intervals;      ///< Number of possible major frame sizes.

    uint16_t        timestamp;              ///< Timestamp of current access unit.
    uint16_t        dts;                    ///< Decoding timestamp of current access unit.

    uint8_t         channel_arrangement;    ///< channel arrangement for MLP streams

    uint8_t         ch_modifier_thd0;       ///< channel modifier for TrueHD stream 0
    uint8_t         ch_modifier_thd1;       ///< channel modifier for TrueHD stream 1
    uint8_t         ch_modifier_thd2;       ///< channel modifier for TrueHD stream 2

    unsigned int    seq_size  [MAJOR_HEADER_INTERVAL];
    unsigned int    seq_offset[MAJOR_HEADER_INTERVAL];
    unsigned int    sequence_size;

    ChannelParams  *channel_params;

    BestOffset      best_offset[MAJOR_HEADER_INTERVAL+1][MAX_CHANNELS][NUM_CODEBOOKS];

    DecodingParams *decoding_params;
    RestartHeader   restart_header [MAX_SUBSTREAMS];

    ChannelParams   major_channel_params[MAJOR_HEADER_INTERVAL+1][MAX_CHANNELS];       ///< ChannelParams to be written to bitstream.
    DecodingParams  major_decoding_params[MAJOR_HEADER_INTERVAL+1][MAX_SUBSTREAMS];    ///< DecodingParams to be written to bitstream.
    int             major_params_changed[MAJOR_HEADER_INTERVAL+1][MAX_SUBSTREAMS];     ///< params_changed to be written to bitstream.

    unsigned int    major_cur_subblock_index;
    unsigned int    major_filter_state_subblock;
    unsigned int    major_number_of_subblocks;

    BestOffset    (*cur_best_offset)[NUM_CODEBOOKS];
    ChannelParams  *cur_channel_params;
    DecodingParams *cur_decoding_params;
    RestartHeader  *cur_restart_header;

    AudioFrameQueue afq;

    /* Analysis stage. */
    unsigned int    starting_frame_index;
    unsigned int    number_of_frames;
    unsigned int    number_of_samples;
    unsigned int    number_of_subblocks;
    unsigned int    seq_index;              ///< Sequence index for high compression levels.

    ChannelParams  *prev_channel_params;
    DecodingParams *prev_decoding_params;

    ChannelParams  *seq_channel_params;
    DecodingParams *seq_decoding_params;

    unsigned int    max_codebook_search;

    LPCContext      lpc_ctx;
} MLPEncodeContext;

static ChannelParams   restart_channel_params[MAX_CHANNELS];
static DecodingParams  restart_decoding_params[MAX_SUBSTREAMS];
static BestOffset      restart_best_offset[NUM_CODEBOOKS] = {{0}};

#define SYNC_MAJOR      0xf8726f
#define MAJOR_SYNC_INFO_SIGNATURE   0xB752

#define SYNC_MLP        0xbb
#define SYNC_TRUEHD     0xba

/* must be set for DVD-A */
#define FLAGS_DVDA      0x4000
/* FIFO delay must be constant */
#define FLAGS_CONST     0x8000

#define SUBSTREAM_INFO_MAX_2_CHAN   0x01
#define SUBSTREAM_INFO_HIGH_RATE    0x02
#define SUBSTREAM_INFO_ALWAYS_SET   0x04
#define SUBSTREAM_INFO_2_SUBSTREAMS 0x08

/****************************************************************************
 ************ Functions that copy, clear, or compare parameters *************
 ****************************************************************************/

/** Compares two FilterParams structures and returns 1 if anything has
 *  changed. Returns 0 if they are both equal.
 */
static int compare_filter_params(const ChannelParams *prev_cp, const ChannelParams *cp, int filter)
{
    const FilterParams *prev = &prev_cp->filter_params[filter];
    const FilterParams *fp = &cp->filter_params[filter];
    int i;

    if (prev->order != fp->order)
        return 1;

    if (!prev->order)
        return 0;

    if (prev->shift != fp->shift)
        return 1;

    for (i = 0; i < fp->order; i++)
        if (prev_cp->coeff[filter][i] != cp->coeff[filter][i])
            return 1;

    return 0;
}

/** Compare two primitive matrices and returns 1 if anything has changed.
 *  Returns 0 if they are both equal.
 */
static int compare_matrix_params(MLPEncodeContext *ctx, const MatrixParams *prev, const MatrixParams *mp)
{
    RestartHeader *rh = ctx->cur_restart_header;
    unsigned int channel, mat;

    if (prev->count != mp->count)
        return 1;

    if (!prev->count)
        return 0;

    for (channel = rh->min_channel; channel <= rh->max_channel; channel++)
        if (prev->fbits[channel] != mp->fbits[channel])
            return 1;

    for (mat = 0; mat < mp->count; mat++) {
        if (prev->outch[mat] != mp->outch[mat])
            return 1;

        for (channel = 0; channel < ctx->num_channels; channel++)
            if (prev->coeff[mat][channel] != mp->coeff[mat][channel])
                return 1;
    }

    return 0;
}

/** Compares two DecodingParams and ChannelParams structures to decide if a
 *  new decoding params header has to be written.
 */
static int compare_decoding_params(MLPEncodeContext *ctx)
{
    DecodingParams *prev = ctx->prev_decoding_params;
    DecodingParams *dp = ctx->cur_decoding_params;
    MatrixParams *prev_mp = &prev->matrix_params;
    MatrixParams *mp = &dp->matrix_params;
    RestartHeader  *rh = ctx->cur_restart_header;
    unsigned int ch;
    int retval = 0;

    if (prev->param_presence_flags != dp->param_presence_flags)
        retval |= PARAM_PRESENCE_FLAGS;

    if (prev->blocksize != dp->blocksize)
        retval |= PARAM_BLOCKSIZE;

    if (compare_matrix_params(ctx, prev_mp, mp))
        retval |= PARAM_MATRIX;

    for (ch = 0; ch <= rh->max_matrix_channel; ch++)
        if (prev_mp->shift[ch] != mp->shift[ch]) {
            retval |= PARAM_OUTSHIFT;
            break;
        }

    for (ch = 0; ch <= rh->max_channel; ch++)
        if (prev->quant_step_size[ch] != dp->quant_step_size[ch]) {
            retval |= PARAM_QUANTSTEP;
            break;
        }

    for (ch = rh->min_channel; ch <= rh->max_channel; ch++) {
        ChannelParams *prev_cp = &ctx->prev_channel_params[ch];
        ChannelParams *cp = &ctx->cur_channel_params[ch];

        if (!(retval & PARAM_FIR) &&
            compare_filter_params(prev_cp, cp, FIR))
            retval |= PARAM_FIR;

        if (!(retval & PARAM_IIR) &&
            compare_filter_params(prev_cp, cp, IIR))
            retval |= PARAM_IIR;

        if (prev_cp->huff_offset != cp->huff_offset)
            retval |= PARAM_HUFFOFFSET;

        if (prev_cp->codebook    != cp->codebook  ||
            prev_cp->huff_lsbs   != cp->huff_lsbs  )
            retval |= 0x1;
    }

    return retval;
}

static void copy_filter_params(ChannelParams *dst_cp, ChannelParams *src_cp, int filter)
{
    FilterParams *dst = &dst_cp->filter_params[filter];
    FilterParams *src = &src_cp->filter_params[filter];
    unsigned int order;

    dst->order = src->order;

    if (dst->order) {
        dst->shift = src->shift;

        dst->coeff_shift = src->coeff_shift;
        dst->coeff_bits = src->coeff_bits;
    }

    for (order = 0; order < dst->order; order++)
        dst_cp->coeff[filter][order] = src_cp->coeff[filter][order];
}

static void copy_matrix_params(MatrixParams *dst, MatrixParams *src)
{
    dst->count = src->count;

    if (dst->count) {
        unsigned int channel, count;

        for (channel = 0; channel < MAX_CHANNELS; channel++) {

            dst->fbits[channel] = src->fbits[channel];
            dst->shift[channel] = src->shift[channel];

            for (count = 0; count < MAX_MATRICES; count++)
                dst->coeff[count][channel] = src->coeff[count][channel];
        }

        for (count = 0; count < MAX_MATRICES; count++)
            dst->outch[count] = src->outch[count];
    }
}

static void copy_restart_frame_params(MLPEncodeContext *ctx,
                                      unsigned int substr)
{
    unsigned int index;

    for (index = 0; index < ctx->number_of_subblocks; index++) {
        DecodingParams *dp = ctx->seq_decoding_params + index*(ctx->num_substreams) + substr;
        unsigned int channel;

        copy_matrix_params(&dp->matrix_params, &ctx->cur_decoding_params->matrix_params);

        for (channel = 0; channel < ctx->avctx->channels; channel++) {
            ChannelParams *cp = ctx->seq_channel_params + index*(ctx->avctx->channels) + channel;
            unsigned int filter;

            dp->quant_step_size[channel] = ctx->cur_decoding_params->quant_step_size[channel];
            dp->matrix_params.shift[channel] = ctx->cur_decoding_params->matrix_params.shift[channel];

            if (index)
                for (filter = 0; filter < NUM_FILTERS; filter++)
                    copy_filter_params(cp, &ctx->cur_channel_params[channel], filter);
        }
    }
}

/** Clears a DecodingParams struct the way it should be after a restart header. */
static void clear_decoding_params(MLPEncodeContext *ctx, DecodingParams decoding_params[MAX_SUBSTREAMS])
{
    unsigned int substr;

    for (substr = 0; substr < ctx->num_substreams; substr++) {
        DecodingParams *dp = &decoding_params[substr];

        dp->param_presence_flags   = 0xff;
        dp->blocksize              = 8;

        memset(&dp->matrix_params , 0, sizeof(MatrixParams       ));
        memset(dp->quant_step_size, 0, sizeof(dp->quant_step_size));
    }
}

/** Clears a ChannelParams struct the way it should be after a restart header. */
static void clear_channel_params(MLPEncodeContext *ctx, ChannelParams channel_params[MAX_CHANNELS])
{
    unsigned int channel;

    for (channel = 0; channel < ctx->avctx->channels; channel++) {
        ChannelParams *cp = &channel_params[channel];

        memset(&cp->filter_params, 0, sizeof(cp->filter_params));

        /* Default audio coding is 24-bit raw PCM. */
        cp->huff_offset      =  0;
        cp->codebook         =  0;
        cp->huff_lsbs        = 24;
    }
}

/** Sets default vales in our encoder for a DecodingParams struct. */
static void default_decoding_params(MLPEncodeContext *ctx,
     DecodingParams decoding_params[MAX_SUBSTREAMS])
{
    unsigned int substr;

    clear_decoding_params(ctx, decoding_params);

    for (substr = 0; substr < ctx->num_substreams; substr++) {
        DecodingParams *dp = &decoding_params[substr];
        uint8_t param_presence_flags = 0;

        param_presence_flags |= PARAM_BLOCKSIZE;
        param_presence_flags |= PARAM_MATRIX;
        param_presence_flags |= PARAM_OUTSHIFT;
        param_presence_flags |= PARAM_QUANTSTEP;
        param_presence_flags |= PARAM_FIR;
/*      param_presence_flags |= PARAM_IIR; */
        param_presence_flags |= PARAM_HUFFOFFSET;
        param_presence_flags |= PARAM_PRESENT;

        dp->param_presence_flags = param_presence_flags;
    }
}

/****************************************************************************/

/** Calculates the smallest number of bits it takes to encode a given signed
 *  value in two's complement.
 */
static int inline number_sbits(int number)
{
    if (number < -1)
        number++;

    return av_log2(FFABS(number)) + 1 + !!number;
}

enum InputBitDepth {
    BITS_16,
    BITS_20,
    BITS_24,
};

static int mlp_peak_bitrate(int peak_bitrate, int sample_rate)
{
    return ((peak_bitrate << 4) - 8) / sample_rate;
}

static av_cold int mlp_encode_init(AVCodecContext *avctx)
{
    MLPEncodeContext *ctx = avctx->priv_data;
    unsigned int substr, index;
    unsigned int sum = 0;
    unsigned int size;
    int ret;

    ctx->avctx = avctx;

    switch (avctx->sample_rate) {
    case 44100 << 0:
        avctx->frame_size         = 40  << 0;
        ctx->coded_sample_rate[0] = 0x08 + 0;
        ctx->fs                   = 0x08 + 1;
        break;
    case 44100 << 1:
        avctx->frame_size         = 40  << 1;
        ctx->coded_sample_rate[0] = 0x08 + 1;
        ctx->fs                   = 0x0C + 1;
        break;
    case 44100 << 2:
        ctx->substream_info      |= SUBSTREAM_INFO_HIGH_RATE;
        avctx->frame_size         = 40  << 2;
        ctx->coded_sample_rate[0] = 0x08 + 2;
        ctx->fs                   = 0x10 + 1;
        break;
    case 48000 << 0:
        avctx->frame_size         = 40  << 0;
        ctx->coded_sample_rate[0] = 0x00 + 0;
        ctx->fs                   = 0x08 + 2;
        break;
    case 48000 << 1:
        avctx->frame_size         = 40  << 1;
        ctx->coded_sample_rate[0] = 0x00 + 1;
        ctx->fs                   = 0x0C + 2;
        break;
    case 48000 << 2:
        ctx->substream_info      |= SUBSTREAM_INFO_HIGH_RATE;
        avctx->frame_size         = 40  << 2;
        ctx->coded_sample_rate[0] = 0x00 + 2;
        ctx->fs                   = 0x10 + 2;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported sample rate %d. Supported "
                            "sample rates are 44100, 88200, 176400, 48000, "
                            "96000, and 192000.\n", avctx->sample_rate);
        return AVERROR(EINVAL);
    }
    ctx->coded_sample_rate[1] = -1 & 0xf;

    /* TODO Keep count of bitrate and calculate real value. */
    ctx->coded_peak_bitrate = mlp_peak_bitrate(9600000, avctx->sample_rate);

    /* TODO support more channels. */
    if (avctx->channels > 2) {
        av_log(avctx, AV_LOG_WARNING,
               "Only mono and stereo are supported at the moment.\n");
    }

    ctx->substream_info |= SUBSTREAM_INFO_ALWAYS_SET;
    if (avctx->channels <= 2) {
        ctx->substream_info |= SUBSTREAM_INFO_MAX_2_CHAN;
    }

    switch (avctx->sample_fmt) {
    case AV_SAMPLE_FMT_S16:
        ctx->coded_sample_fmt[0] = BITS_16;
        ctx->wordlength = 16;
        avctx->bits_per_raw_sample = 16;
        break;
    /* TODO 20 bits: */
    case AV_SAMPLE_FMT_S32:
        ctx->coded_sample_fmt[0] = BITS_24;
        ctx->wordlength = 24;
        avctx->bits_per_raw_sample = 24;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Sample format not supported. "
               "Only 16- and 24-bit samples are supported.\n");
        return AVERROR(EINVAL);
    }
    ctx->coded_sample_fmt[1] = -1 & 0xf;

    ctx->dts = -avctx->frame_size;

    ctx->num_channels = avctx->channels + 2; /* +2 noise channels */
    ctx->one_sample_buffer_size = avctx->frame_size
                                * ctx->num_channels;
    /* TODO Let user pass major header interval as parameter. */
    ctx->max_restart_interval = MAJOR_HEADER_INTERVAL;

    ctx->max_codebook_search = 3;
    ctx->min_restart_interval = MAJOR_HEADER_INTERVAL;
    ctx->restart_intervals = ctx->max_restart_interval / ctx->min_restart_interval;

    /* TODO Let user pass parameters for LPC filter. */

    size = avctx->frame_size * ctx->max_restart_interval;

    ctx->lpc_sample_buffer = av_malloc_array(size, sizeof(int32_t));
    if (!ctx->lpc_sample_buffer) {
        av_log(avctx, AV_LOG_ERROR,
               "Not enough memory for buffering samples.\n");
        return AVERROR(ENOMEM);
    }

    size = ctx->one_sample_buffer_size * ctx->max_restart_interval;

    ctx->major_scratch_buffer = av_malloc_array(size, sizeof(int32_t));
    if (!ctx->major_scratch_buffer) {
        av_log(avctx, AV_LOG_ERROR,
               "Not enough memory for buffering samples.\n");
        return AVERROR(ENOMEM);
    }

    ctx->major_inout_buffer = av_malloc_array(size, sizeof(int32_t));
    if (!ctx->major_inout_buffer) {
        av_log(avctx, AV_LOG_ERROR,
               "Not enough memory for buffering samples.\n");
        return AVERROR(ENOMEM);
    }

    ff_mlp_init_crc();

    ctx->num_substreams = 1; // TODO: change this after adding multi-channel support for TrueHD

    if (ctx->avctx->codec_id == AV_CODEC_ID_MLP) {
        /* MLP */
        switch(avctx->channel_layout) {
        case AV_CH_LAYOUT_MONO:
            ctx->channel_arrangement = 0; break;
        case AV_CH_LAYOUT_STEREO:
            ctx->channel_arrangement = 1; break;
        case AV_CH_LAYOUT_2_1:
            ctx->channel_arrangement = 2; break;
        case AV_CH_LAYOUT_QUAD:
            ctx->channel_arrangement = 3; break;
        case AV_CH_LAYOUT_2POINT1:
            ctx->channel_arrangement = 4; break;
        case AV_CH_LAYOUT_SURROUND:
            ctx->channel_arrangement = 7; break;
        case AV_CH_LAYOUT_4POINT0:
            ctx->channel_arrangement = 8; break;
        case AV_CH_LAYOUT_5POINT0_BACK:
            ctx->channel_arrangement = 9; break;
        case AV_CH_LAYOUT_3POINT1:
            ctx->channel_arrangement = 10; break;
        case AV_CH_LAYOUT_4POINT1:
            ctx->channel_arrangement = 11; break;
        case AV_CH_LAYOUT_5POINT1_BACK:
            ctx->channel_arrangement = 12; break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Unsupported channel arrangement\n");
            return AVERROR(EINVAL);
        }
        ctx->flags = FLAGS_DVDA;
        ctx->channel_occupancy = ff_mlp_ch_info[ctx->channel_arrangement].channel_occupancy;
        ctx->summary_info      = ff_mlp_ch_info[ctx->channel_arrangement].summary_info     ;
    } else {
        /* TrueHD */
        switch(avctx->channel_layout) {
        case AV_CH_LAYOUT_STEREO:
            ctx->ch_modifier_thd0    = 0;
            ctx->ch_modifier_thd1    = 0;
            ctx->ch_modifier_thd2    = 0;
            ctx->channel_arrangement = 1;
            break;
        case AV_CH_LAYOUT_5POINT0_BACK:
            ctx->ch_modifier_thd0    = 1;
            ctx->ch_modifier_thd1    = 1;
            ctx->ch_modifier_thd2    = 1;
            ctx->channel_arrangement = 11;
            break;
        case AV_CH_LAYOUT_5POINT1_BACK:
            ctx->ch_modifier_thd0    = 2;
            ctx->ch_modifier_thd1    = 1;
            ctx->ch_modifier_thd2    = 2;
            ctx->channel_arrangement = 15;
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Unsupported channel arrangement\n");
            return AVERROR(EINVAL);
        }
        ctx->flags = 0;
        ctx->channel_occupancy = 0;
        ctx->summary_info = 0;
    }

    size = sizeof(unsigned int) * ctx->max_restart_interval;

    ctx->frame_size = av_malloc(size);
    if (!ctx->frame_size)
        return AVERROR(ENOMEM);

    ctx->max_output_bits = av_malloc(size);
    if (!ctx->max_output_bits)
        return AVERROR(ENOMEM);

    size = sizeof(int32_t)
         * ctx->num_substreams * ctx->max_restart_interval;

    ctx->lossless_check_data = av_malloc(size);
    if (!ctx->lossless_check_data)
        return AVERROR(ENOMEM);

    for (index = 0; index < ctx->restart_intervals; index++) {
        ctx->seq_offset[index] = sum;
        ctx->seq_size  [index] = ((index + 1) * ctx->min_restart_interval) + 1;
        sum += ctx->seq_size[index];
    }
    ctx->sequence_size = sum;
    size = sizeof(ChannelParams)
         * ctx->restart_intervals * ctx->sequence_size * ctx->avctx->channels;
    ctx->channel_params = av_malloc(size);
    if (!ctx->channel_params) {
        av_log(avctx, AV_LOG_ERROR,
               "Not enough memory for analysis context.\n");
        return AVERROR(ENOMEM);
    }

    size = sizeof(DecodingParams)
         * ctx->restart_intervals * ctx->sequence_size * ctx->num_substreams;
    ctx->decoding_params = av_malloc(size);
    if (!ctx->decoding_params) {
        av_log(avctx, AV_LOG_ERROR,
               "Not enough memory for analysis context.\n");
        return AVERROR(ENOMEM);
    }

    for (substr = 0; substr < ctx->num_substreams; substr++) {
        RestartHeader  *rh = &ctx->restart_header [substr];

        /* TODO see if noisegen_seed is really worth it. */
        rh->noisegen_seed      = 0;

        rh->min_channel        = 0;
        rh->max_channel        = avctx->channels - 1;
        /* FIXME: this works for 1 and 2 channels, but check for more */
        rh->max_matrix_channel = rh->max_channel;
    }

    clear_channel_params(ctx, restart_channel_params);
    clear_decoding_params(ctx, restart_decoding_params);

    if ((ret = ff_lpc_init(&ctx->lpc_ctx, ctx->number_of_samples,
                    MLP_MAX_LPC_ORDER, FF_LPC_TYPE_LEVINSON)) < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Not enough memory for LPC context.\n");
        return ret;
    }

    ff_af_queue_init(avctx, &ctx->afq);

    return 0;
}

/****************************************************************************
 ****************** Functions that write to the bitstream *******************
 ****************************************************************************/

/** Writes a major sync header to the bitstream. */
static void write_major_sync(MLPEncodeContext *ctx, uint8_t *buf, int buf_size)
{
    PutBitContext pb;

    init_put_bits(&pb, buf, buf_size);

    put_bits(&pb, 24, SYNC_MAJOR               );

    if (ctx->avctx->codec_id == AV_CODEC_ID_MLP) {
        put_bits(&pb,  8, SYNC_MLP                 );
        put_bits(&pb,  4, ctx->coded_sample_fmt [0]);
        put_bits(&pb,  4, ctx->coded_sample_fmt [1]);
        put_bits(&pb,  4, ctx->coded_sample_rate[0]);
        put_bits(&pb,  4, ctx->coded_sample_rate[1]);
        put_bits(&pb,  4, 0                        ); /* ignored */
        put_bits(&pb,  4, 0                        ); /* multi_channel_type */
        put_bits(&pb,  3, 0                        ); /* ignored */
        put_bits(&pb,  5, ctx->channel_arrangement );
    } else if (ctx->avctx->codec_id == AV_CODEC_ID_TRUEHD) {
        put_bits(&pb,  8, SYNC_TRUEHD              );
        put_bits(&pb,  4, ctx->coded_sample_rate[0]);
        put_bits(&pb,  4, 0                        ); /* ignored */
        put_bits(&pb,  2, ctx->ch_modifier_thd0    );
        put_bits(&pb,  2, ctx->ch_modifier_thd1    );
        put_bits(&pb,  5, ctx->channel_arrangement );
        put_bits(&pb,  2, ctx->ch_modifier_thd2    );
        put_bits(&pb, 13, ctx->channel_arrangement );
    }

    put_bits(&pb, 16, MAJOR_SYNC_INFO_SIGNATURE);
    put_bits(&pb, 16, ctx->flags               );
    put_bits(&pb, 16, 0                        ); /* ignored */
    put_bits(&pb,  1, 1                        ); /* is_vbr */
    put_bits(&pb, 15, ctx->coded_peak_bitrate  );
    put_bits(&pb,  4, 1                        ); /* num_substreams */
    put_bits(&pb,  4, 0x1                      ); /* ignored */

    /* channel_meaning */
    put_bits(&pb,  8, ctx->substream_info      );
    put_bits(&pb,  5, ctx->fs                  );
    put_bits(&pb,  5, ctx->wordlength          );
    put_bits(&pb,  6, ctx->channel_occupancy   );
    put_bits(&pb,  3, 0                        ); /* ignored */
    put_bits(&pb, 10, 0                        ); /* speaker_layout */
    put_bits(&pb,  3, 0                        ); /* copy_protection */
    put_bits(&pb, 16, 0x8080                   ); /* ignored */
    put_bits(&pb,  7, 0                        ); /* ignored */
    put_bits(&pb,  4, 0                        ); /* source_format */
    put_bits(&pb,  5, ctx->summary_info        );

    flush_put_bits(&pb);

    AV_WL16(buf+26, ff_mlp_checksum16(buf, 26));
}

/** Writes a restart header to the bitstream. Damaged streams can start being
 *  decoded losslessly again after such a header and the subsequent decoding
 *  params header.
 */
static void write_restart_header(MLPEncodeContext *ctx, PutBitContext *pb)
{
    RestartHeader *rh = ctx->cur_restart_header;
    uint8_t lossless_check = xor_32_to_8(rh->lossless_check_data);
    unsigned int start_count = put_bits_count(pb);
    PutBitContext tmpb;
    uint8_t checksum;
    unsigned int ch;

    put_bits(pb, 14, 0x31ea                ); /* TODO 0x31eb */
    put_bits(pb, 16, ctx->timestamp        );
    put_bits(pb,  4, rh->min_channel       );
    put_bits(pb,  4, rh->max_channel       );
    put_bits(pb,  4, rh->max_matrix_channel);
    put_bits(pb,  4, rh->noise_shift       );
    put_bits(pb, 23, rh->noisegen_seed     );
    put_bits(pb,  4, 0                     ); /* TODO max_shift */
    put_bits(pb,  5, rh->max_huff_lsbs     );
    put_bits(pb,  5, rh->max_output_bits   );
    put_bits(pb,  5, rh->max_output_bits   );
    put_bits(pb,  1, rh->data_check_present);
    put_bits(pb,  8, lossless_check        );
    put_bits(pb, 16, 0                     ); /* ignored */

    for (ch = 0; ch <= rh->max_matrix_channel; ch++)
        put_bits(pb, 6, ch);

    /* Data must be flushed for the checksum to be correct. */
    tmpb = *pb;
    flush_put_bits(&tmpb);

    checksum = ff_mlp_restart_checksum(pb->buf, put_bits_count(pb) - start_count);

    put_bits(pb,  8, checksum);
}

/** Writes matrix params for all primitive matrices to the bitstream. */
static void write_matrix_params(MLPEncodeContext *ctx, PutBitContext *pb)
{
    DecodingParams *dp = ctx->cur_decoding_params;
    MatrixParams *mp = &dp->matrix_params;
    unsigned int mat;

    put_bits(pb, 4, mp->count);

    for (mat = 0; mat < mp->count; mat++) {
        unsigned int channel;

        put_bits(pb, 4, mp->outch[mat]); /* matrix_out_ch */
        put_bits(pb, 4, mp->fbits[mat]);
        put_bits(pb, 1, 0             ); /* lsb_bypass */

        for (channel = 0; channel < ctx->num_channels; channel++) {
            int32_t coeff = mp->coeff[mat][channel];

            if (coeff) {
                put_bits(pb, 1, 1);

                coeff >>= 14 - mp->fbits[mat];

                put_sbits(pb, mp->fbits[mat] + 2, coeff);
            } else {
                put_bits(pb, 1, 0);
            }
        }
    }
}

/** Writes filter parameters for one filter to the bitstream. */
static void write_filter_params(MLPEncodeContext *ctx, PutBitContext *pb,
                                unsigned int channel, unsigned int filter)
{
    FilterParams *fp = &ctx->cur_channel_params[channel].filter_params[filter];

    put_bits(pb, 4, fp->order);

    if (fp->order > 0) {
        int i;
        int32_t *fcoeff = ctx->cur_channel_params[channel].coeff[filter];

        put_bits(pb, 4, fp->shift      );
        put_bits(pb, 5, fp->coeff_bits );
        put_bits(pb, 3, fp->coeff_shift);

        for (i = 0; i < fp->order; i++) {
            put_sbits(pb, fp->coeff_bits, fcoeff[i] >> fp->coeff_shift);
        }

        /* TODO state data for IIR filter. */
        put_bits(pb, 1, 0);
    }
}

/** Writes decoding parameters to the bitstream. These change very often,
 *  usually at almost every frame.
 */
static void write_decoding_params(MLPEncodeContext *ctx, PutBitContext *pb,
                                  int params_changed)
{
    DecodingParams *dp = ctx->cur_decoding_params;
    RestartHeader  *rh = ctx->cur_restart_header;
    MatrixParams *mp = &dp->matrix_params;
    unsigned int ch;

    if (dp->param_presence_flags != PARAMS_DEFAULT &&
        params_changed & PARAM_PRESENCE_FLAGS) {
        put_bits(pb, 1, 1);
        put_bits(pb, 8, dp->param_presence_flags);
    } else {
        put_bits(pb, 1, 0);
    }

    if (dp->param_presence_flags & PARAM_BLOCKSIZE) {
        if (params_changed       & PARAM_BLOCKSIZE) {
            put_bits(pb, 1, 1);
            put_bits(pb, 9, dp->blocksize);
        } else {
            put_bits(pb, 1, 0);
        }
    }

    if (dp->param_presence_flags & PARAM_MATRIX) {
        if (params_changed       & PARAM_MATRIX) {
            put_bits(pb, 1, 1);
            write_matrix_params(ctx, pb);
        } else {
            put_bits(pb, 1, 0);
        }
    }

    if (dp->param_presence_flags & PARAM_OUTSHIFT) {
        if (params_changed       & PARAM_OUTSHIFT) {
            put_bits(pb, 1, 1);
            for (ch = 0; ch <= rh->max_matrix_channel; ch++)
                put_sbits(pb, 4, mp->shift[ch]);
        } else {
            put_bits(pb, 1, 0);
        }
    }

    if (dp->param_presence_flags & PARAM_QUANTSTEP) {
        if (params_changed       & PARAM_QUANTSTEP) {
            put_bits(pb, 1, 1);
            for (ch = 0; ch <= rh->max_channel; ch++)
                put_bits(pb, 4, dp->quant_step_size[ch]);
        } else {
            put_bits(pb, 1, 0);
        }
    }

    for (ch = rh->min_channel; ch <= rh->max_channel; ch++) {
        ChannelParams *cp = &ctx->cur_channel_params[ch];

        if (dp->param_presence_flags & 0xF) {
            put_bits(pb, 1, 1);

            if (dp->param_presence_flags & PARAM_FIR) {
                if (params_changed       & PARAM_FIR) {
                    put_bits(pb, 1, 1);
                    write_filter_params(ctx, pb, ch, FIR);
                } else {
                    put_bits(pb, 1, 0);
                }
            }

            if (dp->param_presence_flags & PARAM_IIR) {
                if (params_changed       & PARAM_IIR) {
                    put_bits(pb, 1, 1);
                    write_filter_params(ctx, pb, ch, IIR);
                } else {
                    put_bits(pb, 1, 0);
                }
            }

            if (dp->param_presence_flags & PARAM_HUFFOFFSET) {
                if (params_changed       & PARAM_HUFFOFFSET) {
                    put_bits (pb,  1, 1);
                    put_sbits(pb, 15, cp->huff_offset);
                } else {
                    put_bits(pb, 1, 0);
                }
            }
            if (cp->codebook > 0 && cp->huff_lsbs > 24) {
                av_log(ctx->avctx, AV_LOG_ERROR, "Invalid Huff LSBs\n");
            }

            put_bits(pb, 2, cp->codebook );
            put_bits(pb, 5, cp->huff_lsbs);
        } else {
            put_bits(pb, 1, 0);
        }
    }
}

/** Writes the residuals to the bitstream. That is, the VLC codes from the
 *  codebooks (if any is used), and then the residual.
 */
static void write_block_data(MLPEncodeContext *ctx, PutBitContext *pb)
{
    DecodingParams *dp = ctx->cur_decoding_params;
    RestartHeader  *rh = ctx->cur_restart_header;
    int32_t *sample_buffer = ctx->write_buffer;
    int32_t sign_huff_offset[MAX_CHANNELS];
    int codebook_index      [MAX_CHANNELS];
    int lsb_bits            [MAX_CHANNELS];
    unsigned int i, ch;

    for (ch = rh->min_channel; ch <= rh->max_channel; ch++) {
        ChannelParams *cp = &ctx->cur_channel_params[ch];
        int sign_shift;

        lsb_bits        [ch] = cp->huff_lsbs - dp->quant_step_size[ch];
        codebook_index  [ch] = cp->codebook  - 1;
        sign_huff_offset[ch] = cp->huff_offset;

        sign_shift = lsb_bits[ch] + (cp->codebook ? 2 - cp->codebook : -1);

        if (cp->codebook > 0)
            sign_huff_offset[ch] -= 7 << lsb_bits[ch];

        /* Unsign if needed. */
        if (sign_shift >= 0)
            sign_huff_offset[ch] -= 1 << sign_shift;
    }

    for (i = 0; i < dp->blocksize; i++) {
        for (ch = rh->min_channel; ch <= rh->max_channel; ch++) {
            int32_t sample = *sample_buffer++ >> dp->quant_step_size[ch];
            sample -= sign_huff_offset[ch];

            if (codebook_index[ch] >= 0) {
                int vlc = sample >> lsb_bits[ch];
                put_bits(pb, ff_mlp_huffman_tables[codebook_index[ch]][vlc][1],
                             ff_mlp_huffman_tables[codebook_index[ch]][vlc][0]);
            }

            put_sbits(pb, lsb_bits[ch], sample);
        }
        sample_buffer += 2; /* noise channels */
    }

    ctx->write_buffer = sample_buffer;
}

/** Writes the substreams data to the bitstream. */
static uint8_t *write_substrs(MLPEncodeContext *ctx, uint8_t *buf, int buf_size,
                              int restart_frame,
                              uint16_t substream_data_len[MAX_SUBSTREAMS])
{
    int32_t *lossless_check_data = ctx->lossless_check_data;
    unsigned int substr;
    int end = 0;

    lossless_check_data += ctx->frame_index * ctx->num_substreams;

    for (substr = 0; substr < ctx->num_substreams; substr++) {
        unsigned int cur_subblock_index = ctx->major_cur_subblock_index;
        unsigned int num_subblocks = ctx->major_filter_state_subblock;
        unsigned int subblock;
        RestartHeader  *rh = &ctx->restart_header [substr];
        int substr_restart_frame = restart_frame;
        uint8_t parity, checksum;
        PutBitContext pb, tmpb;
        int params_changed;

        ctx->cur_restart_header = rh;

        init_put_bits(&pb, buf, buf_size);

        for (subblock = 0; subblock <= num_subblocks; subblock++) {
            unsigned int subblock_index;

            subblock_index = cur_subblock_index++;

            ctx->cur_decoding_params = &ctx->major_decoding_params[subblock_index][substr];
            ctx->cur_channel_params = ctx->major_channel_params[subblock_index];

            params_changed = ctx->major_params_changed[subblock_index][substr];

            if (substr_restart_frame || params_changed) {
                put_bits(&pb, 1, 1);

                if (substr_restart_frame) {
                    put_bits(&pb, 1, 1);

                    write_restart_header(ctx, &pb);
                    rh->lossless_check_data = 0;
                } else {
                    put_bits(&pb, 1, 0);
                }

                write_decoding_params(ctx, &pb, params_changed);
            } else {
                put_bits(&pb, 1, 0);
            }

            write_block_data(ctx, &pb);

            put_bits(&pb, 1, !substr_restart_frame);

            substr_restart_frame = 0;
        }

        put_bits(&pb, (-put_bits_count(&pb)) & 15, 0);

        rh->lossless_check_data ^= *lossless_check_data++;

        if (ctx->last_frame == ctx->inout_buffer) {
            /* TODO find a sample and implement shorten_by. */
            put_bits(&pb, 32, END_OF_STREAM);
        }

        /* Data must be flushed for the checksum and parity to be correct. */
        tmpb = pb;
        flush_put_bits(&tmpb);

        parity   = ff_mlp_calculate_parity(buf, put_bits_count(&pb) >> 3) ^ 0xa9;
        checksum = ff_mlp_checksum8       (buf, put_bits_count(&pb) >> 3);

        put_bits(&pb, 8, parity  );
        put_bits(&pb, 8, checksum);

        flush_put_bits(&pb);

        end += put_bits_count(&pb) >> 3;
        substream_data_len[substr] = end;

        buf += put_bits_count(&pb) >> 3;
    }

    ctx->major_cur_subblock_index += ctx->major_filter_state_subblock + 1;
    ctx->major_filter_state_subblock = 0;

    return buf;
}

/** Writes the access unit and substream headers to the bitstream. */
static void write_frame_headers(MLPEncodeContext *ctx, uint8_t *frame_header,
                                uint8_t *substream_headers, unsigned int length,
                                int restart_frame,
                                uint16_t substream_data_len[MAX_SUBSTREAMS])
{
    uint16_t access_unit_header = 0;
    uint16_t parity_nibble = 0;
    unsigned int substr;

    parity_nibble  = ctx->dts;
    parity_nibble ^= length;

    for (substr = 0; substr < ctx->num_substreams; substr++) {
        uint16_t substr_hdr = 0;

        substr_hdr |= (0 << 15); /* extraword */
        substr_hdr |= (!restart_frame << 14); /* !restart_frame */
        substr_hdr |= (1 << 13); /* checkdata */
        substr_hdr |= (0 << 12); /* ??? */
        substr_hdr |= (substream_data_len[substr] / 2) & 0x0FFF;

        AV_WB16(substream_headers, substr_hdr);

        parity_nibble ^= *substream_headers++;
        parity_nibble ^= *substream_headers++;
    }

    parity_nibble ^= parity_nibble >> 8;
    parity_nibble ^= parity_nibble >> 4;
    parity_nibble &= 0xF;

    access_unit_header |= (parity_nibble ^ 0xF) << 12;
    access_unit_header |= length & 0xFFF;

    AV_WB16(frame_header  , access_unit_header);
    AV_WB16(frame_header+2, ctx->dts          );
}

/** Writes an entire access unit to the bitstream. */
static unsigned int write_access_unit(MLPEncodeContext *ctx, uint8_t *buf,
                                      int buf_size, int restart_frame)
{
    uint16_t substream_data_len[MAX_SUBSTREAMS];
    uint8_t *buf1, *buf0 = buf;
    unsigned int substr;
    int total_length;

    if (buf_size < 4)
        return AVERROR(EINVAL);

    /* Frame header will be written at the end. */
    buf      += 4;
    buf_size -= 4;

    if (restart_frame) {
        if (buf_size < 28)
            return AVERROR(EINVAL);
        write_major_sync(ctx, buf, buf_size);
        buf      += 28;
        buf_size -= 28;
    }

    buf1 = buf;

    /* Substream headers will be written at the end. */
    for (substr = 0; substr < ctx->num_substreams; substr++) {
        buf      += 2;
        buf_size -= 2;
    }

    buf = write_substrs(ctx, buf, buf_size, restart_frame, substream_data_len);

    total_length = buf - buf0;

    write_frame_headers(ctx, buf0, buf1, total_length / 2, restart_frame, substream_data_len);

    return total_length;
}

/****************************************************************************
 ****************** Functions that input data to context ********************
 ****************************************************************************/

/** Inputs data from the samples passed by lavc into the context, shifts them
 *  appropriately depending on the bit-depth, and calculates the
 *  lossless_check_data that will be written to the restart header.
 */
static void input_data_internal(MLPEncodeContext *ctx, const uint8_t *samples,
                                int is24)
{
    int32_t *lossless_check_data = ctx->lossless_check_data;
    const int32_t *samples_32 = (const int32_t *) samples;
    const int16_t *samples_16 = (const int16_t *) samples;
    unsigned int substr;

    lossless_check_data += ctx->frame_index * ctx->num_substreams;

    for (substr = 0; substr < ctx->num_substreams; substr++) {
        RestartHeader  *rh = &ctx->restart_header [substr];
        int32_t *sample_buffer = ctx->inout_buffer;
        int32_t temp_lossless_check_data = 0;
        uint32_t greatest = 0;
        unsigned int channel;
        int i;

        for (i = 0; i < ctx->frame_size[ctx->frame_index]; i++) {
            for (channel = 0; channel <= rh->max_channel; channel++) {
                uint32_t abs_sample;
                int32_t sample;

                sample = is24 ? *samples_32++ >> 8 : *samples_16++ * 256;

                /* TODO Find out if number_sbits can be used for negative values. */
                abs_sample = FFABS(sample);
                if (greatest < abs_sample)
                    greatest = abs_sample;

                temp_lossless_check_data ^= (sample & 0x00ffffff) << channel;
                *sample_buffer++ = sample;
            }

            sample_buffer += 2; /* noise channels */
        }

        ctx->max_output_bits[ctx->frame_index] = number_sbits(greatest);

        *lossless_check_data++ = temp_lossless_check_data;
    }
}

/** Wrapper function for inputting data in two different bit-depths. */
static void input_data(MLPEncodeContext *ctx, void *samples)
{
    if (ctx->avctx->sample_fmt == AV_SAMPLE_FMT_S32)
        input_data_internal(ctx, samples, 1);
    else
        input_data_internal(ctx, samples, 0);
}

static void input_to_sample_buffer(MLPEncodeContext *ctx)
{
    int32_t *sample_buffer = ctx->sample_buffer;
    unsigned int index;

    for (index = 0; index < ctx->number_of_frames; index++) {
        unsigned int cur_index = (ctx->starting_frame_index + index) % ctx->max_restart_interval;
        int32_t *input_buffer = ctx->inout_buffer + cur_index * ctx->one_sample_buffer_size;
        unsigned int i, channel;

        for (i = 0; i < ctx->frame_size[cur_index]; i++) {
            for (channel = 0; channel < ctx->avctx->channels; channel++)
                *sample_buffer++ = *input_buffer++;
            sample_buffer += 2; /* noise_channels */
            input_buffer += 2; /* noise_channels */
        }
    }
}

/****************************************************************************
 ********* Functions that analyze the data and set the parameters ***********
 ****************************************************************************/

/** Counts the number of trailing zeroes in a value */
static int number_trailing_zeroes(int32_t sample)
{
    int bits;

    for (bits = 0; bits < 24 && !(sample & (1<<bits)); bits++);

    /* All samples are 0. TODO Return previous quant_step_size to avoid
     * writing a new header. */
    if (bits == 24)
        return 0;

    return bits;
}

/** Determines how many bits are zero at the end of all samples so they can be
 *  shifted out.
 */
static void determine_quant_step_size(MLPEncodeContext *ctx)
{
    DecodingParams *dp = ctx->cur_decoding_params;
    RestartHeader  *rh = ctx->cur_restart_header;
    MatrixParams *mp = &dp->matrix_params;
    int32_t *sample_buffer = ctx->sample_buffer;
    int32_t sample_mask[MAX_CHANNELS];
    unsigned int channel;
    int i;

    memset(sample_mask, 0x00, sizeof(sample_mask));

    for (i = 0; i < ctx->number_of_samples; i++) {
        for (channel = 0; channel <= rh->max_channel; channel++)
            sample_mask[channel] |= *sample_buffer++;

        sample_buffer += 2; /* noise channels */
    }

    for (channel = 0; channel <= rh->max_channel; channel++)
        dp->quant_step_size[channel] = number_trailing_zeroes(sample_mask[channel]) - mp->shift[channel];
}

/** Determines the smallest number of bits needed to encode the filter
 *  coefficients, and if it's possible to right-shift their values without
 *  losing any precision.
 */
static void code_filter_coeffs(MLPEncodeContext *ctx, FilterParams *fp, int32_t *fcoeff)
{
    int min = INT_MAX, max = INT_MIN;
    int bits, shift;
    int coeff_mask = 0;
    int order;

    for (order = 0; order < fp->order; order++) {
        int coeff = fcoeff[order];

        if (coeff < min)
            min = coeff;
        if (coeff > max)
            max = coeff;

        coeff_mask |= coeff;
    }

    bits = FFMAX(number_sbits(min), number_sbits(max));

    for (shift = 0; shift < 7 && bits + shift < 16 && !(coeff_mask & (1<<shift)); shift++);

    fp->coeff_bits  = bits;
    fp->coeff_shift = shift;
}

/** Determines the best filter parameters for the given data and writes the
 *  necessary information to the context.
 *  TODO Add IIR filter predictor!
 */
static void set_filter_params(MLPEncodeContext *ctx,
                              unsigned int channel, unsigned int filter,
                              int clear_filter)
{
    ChannelParams *cp = &ctx->cur_channel_params[channel];
    FilterParams *fp = &cp->filter_params[filter];

    if ((filter == IIR && ctx->substream_info & SUBSTREAM_INFO_HIGH_RATE) ||
        clear_filter) {
        fp->order = 0;
    } else if (filter == IIR) {
        fp->order = 0;
    } else if (filter == FIR) {
        const int max_order = (ctx->substream_info & SUBSTREAM_INFO_HIGH_RATE)
                              ? 4 : MLP_MAX_LPC_ORDER;
        int32_t *sample_buffer = ctx->sample_buffer + channel;
        int32_t coefs[MAX_LPC_ORDER][MAX_LPC_ORDER];
        int32_t *lpc_samples = ctx->lpc_sample_buffer;
        int32_t *fcoeff = ctx->cur_channel_params[channel].coeff[filter];
        int shift[MLP_MAX_LPC_ORDER];
        unsigned int i;
        int order;

        for (i = 0; i < ctx->number_of_samples; i++) {
            *lpc_samples++ = *sample_buffer;
            sample_buffer += ctx->num_channels;
        }

        order = ff_lpc_calc_coefs(&ctx->lpc_ctx, ctx->lpc_sample_buffer,
                                  ctx->number_of_samples, MLP_MIN_LPC_ORDER,
                                  max_order, 11, coefs, shift, FF_LPC_TYPE_LEVINSON, 0,
                                  ORDER_METHOD_EST, MLP_MIN_LPC_SHIFT,
                                  MLP_MAX_LPC_SHIFT, MLP_MIN_LPC_SHIFT);

        fp->order = order;
        fp->shift = shift[order-1];

        for (i = 0; i < order; i++)
            fcoeff[i] = coefs[order-1][i];

        code_filter_coeffs(ctx, fp, fcoeff);
    }
}

/** Tries to determine a good prediction filter, and applies it to the samples
 *  buffer if the filter is good enough. Sets the filter data to be cleared if
 *  no good filter was found.
 */
static void determine_filters(MLPEncodeContext *ctx)
{
    RestartHeader *rh = ctx->cur_restart_header;
    int channel, filter;

    for (channel = rh->min_channel; channel <= rh->max_channel; channel++) {
        for (filter = 0; filter < NUM_FILTERS; filter++)
            set_filter_params(ctx, channel, filter, 0);
    }
}

enum MLPChMode {
    MLP_CHMODE_LEFT_RIGHT,
    MLP_CHMODE_LEFT_SIDE,
    MLP_CHMODE_RIGHT_SIDE,
    MLP_CHMODE_MID_SIDE,
};

static enum MLPChMode estimate_stereo_mode(MLPEncodeContext *ctx)
{
    uint64_t score[4], sum[4] = { 0, 0, 0, 0, };
    int32_t *right_ch = ctx->sample_buffer + 1;
    int32_t *left_ch  = ctx->sample_buffer;
    int i;
    enum MLPChMode best = 0;

    for(i = 2; i < ctx->number_of_samples; i++) {
        int32_t left  = left_ch [i * ctx->num_channels] - 2 * left_ch [(i - 1) * ctx->num_channels] + left_ch [(i - 2) * ctx->num_channels];
        int32_t right = right_ch[i * ctx->num_channels] - 2 * right_ch[(i - 1) * ctx->num_channels] + right_ch[(i - 2) * ctx->num_channels];

        sum[0] += FFABS( left        );
        sum[1] += FFABS(        right);
        sum[2] += FFABS((left + right) >> 1);
        sum[3] += FFABS( left - right);
    }

    score[MLP_CHMODE_LEFT_RIGHT] = sum[0] + sum[1];
    score[MLP_CHMODE_LEFT_SIDE]  = sum[0] + sum[3];
    score[MLP_CHMODE_RIGHT_SIDE] = sum[1] + sum[3];
    score[MLP_CHMODE_MID_SIDE]   = sum[2] + sum[3];

    for(i = 1; i < 3; i++)
        if(score[i] < score[best])
            best = i;

    return best;
}

/** Determines how many fractional bits are needed to encode matrix
 *  coefficients. Also shifts the coefficients to fit within 2.14 bits.
 */
static void code_matrix_coeffs(MLPEncodeContext *ctx, unsigned int mat)
{
    DecodingParams *dp = ctx->cur_decoding_params;
    MatrixParams *mp = &dp->matrix_params;
    int32_t coeff_mask = 0;
    unsigned int channel;
    unsigned int bits;

    for (channel = 0; channel < ctx->num_channels; channel++) {
        int32_t coeff = mp->coeff[mat][channel];
        coeff_mask |= coeff;
    }

    for (bits = 0; bits < 14 && !(coeff_mask & (1<<bits)); bits++);

    mp->fbits   [mat] = 14 - bits;
}

/** Determines best coefficients to use for the lossless matrix. */
static void lossless_matrix_coeffs(MLPEncodeContext *ctx)
{
    DecodingParams *dp = ctx->cur_decoding_params;
    MatrixParams *mp = &dp->matrix_params;
    unsigned int shift = 0;
    unsigned int channel;
    int mat;
    enum MLPChMode mode;

    /* No decorrelation for non-stereo. */
    if (ctx->num_channels - 2 != 2) {
        mp->count = 0;
        return;
    }

    mode = estimate_stereo_mode(ctx);

    switch(mode) {
        /* TODO: add matrix for MID_SIDE */
        case MLP_CHMODE_MID_SIDE:
        case MLP_CHMODE_LEFT_RIGHT:
            mp->count    = 0;
            break;
        case MLP_CHMODE_LEFT_SIDE:
            mp->count    = 1;
            mp->outch[0] = 1;
            mp->coeff[0][0] =  1 << 14; mp->coeff[0][1] = -(1 << 14);
            mp->coeff[0][2] =  0 << 14; mp->coeff[0][2] =   0 << 14;
            mp->forco[0][0] =  1 << 14; mp->forco[0][1] = -(1 << 14);
            mp->forco[0][2] =  0 << 14; mp->forco[0][2] =   0 << 14;
            break;
        case MLP_CHMODE_RIGHT_SIDE:
            mp->count    = 1;
            mp->outch[0] = 0;
            mp->coeff[0][0] =  1 << 14; mp->coeff[0][1] =   1 << 14;
            mp->coeff[0][2] =  0 << 14; mp->coeff[0][2] =   0 << 14;
            mp->forco[0][0] =  1 << 14; mp->forco[0][1] = -(1 << 14);
            mp->forco[0][2] =  0 << 14; mp->forco[0][2] =   0 << 14;
            break;
    }

    for (mat = 0; mat < mp->count; mat++)
        code_matrix_coeffs(ctx, mat);

    for (channel = 0; channel < ctx->num_channels; channel++)
        mp->shift[channel] = shift;
}

/** Min and max values that can be encoded with each codebook. The values for
 *  the third codebook take into account the fact that the sign shift for this
 *  codebook is outside the coded value, so it has one more bit of precision.
 *  It should actually be -7 -> 7, shifted down by 0.5.
 */
static const int codebook_extremes[3][2] = {
    {-9, 8}, {-8, 7}, {-15, 14},
};

/** Determines the amount of bits needed to encode the samples using no
 *  codebooks and a specified offset.
 */
static void no_codebook_bits_offset(MLPEncodeContext *ctx,
                                    unsigned int channel, int16_t offset,
                                    int32_t min, int32_t max,
                                    BestOffset *bo)
{
    DecodingParams *dp = ctx->cur_decoding_params;
    int32_t unsign = 0;
    int lsb_bits;

    min -= offset;
    max -= offset;

    lsb_bits = FFMAX(number_sbits(min), number_sbits(max)) - 1;

    lsb_bits += !!lsb_bits;

    if (lsb_bits > 0)
        unsign = 1 << (lsb_bits - 1);

    bo->offset   = offset;
    bo->lsb_bits = lsb_bits;
    bo->bitcount = lsb_bits * dp->blocksize;
    bo->min      = offset - unsign + 1;
    bo->max      = offset + unsign;
}

/** Determines the least amount of bits needed to encode the samples using no
 *  codebooks.
 */
static void no_codebook_bits(MLPEncodeContext *ctx,
                             unsigned int channel,
                             int32_t min, int32_t max,
                             BestOffset *bo)
{
    DecodingParams *dp = ctx->cur_decoding_params;
    int16_t offset;
    int32_t unsign = 0;
    uint32_t diff;
    int lsb_bits;

    /* Set offset inside huffoffset's boundaries by adjusting extremes
     * so that more bits are used, thus shifting the offset. */
    if (min < HUFF_OFFSET_MIN)
        max = FFMAX(max, 2 * HUFF_OFFSET_MIN - min + 1);
    if (max > HUFF_OFFSET_MAX)
        min = FFMIN(min, 2 * HUFF_OFFSET_MAX - max - 1);

    /* Determine offset and minimum number of bits. */
    diff = max - min;

    lsb_bits = number_sbits(diff) - 1;

    if (lsb_bits > 0)
        unsign = 1 << (lsb_bits - 1);

    /* If all samples are the same (lsb_bits == 0), offset must be
     * adjusted because of sign_shift. */
    offset = min + diff / 2 + !!lsb_bits;

    bo->offset   = offset;
    bo->lsb_bits = lsb_bits;
    bo->bitcount = lsb_bits * dp->blocksize;
    bo->min      = max - unsign + 1;
    bo->max      = min + unsign;
}

/** Determines the least amount of bits needed to encode the samples using a
 *  given codebook and a given offset.
 */
static inline void codebook_bits_offset(MLPEncodeContext *ctx,
                                        unsigned int channel, int codebook,
                                        int32_t sample_min, int32_t sample_max,
                                        int16_t offset, BestOffset *bo)
{
    int32_t codebook_min = codebook_extremes[codebook][0];
    int32_t codebook_max = codebook_extremes[codebook][1];
    int32_t *sample_buffer = ctx->sample_buffer + channel;
    DecodingParams *dp = ctx->cur_decoding_params;
    int codebook_offset  = 7 + (2 - codebook);
    int32_t unsign_offset = offset;
    int lsb_bits = 0, bitcount = 0;
    int offset_min = INT_MAX, offset_max = INT_MAX;
    int unsign, mask;
    int i;

    sample_min -= offset;
    sample_max -= offset;

    while (sample_min < codebook_min || sample_max > codebook_max) {
        lsb_bits++;
        sample_min >>= 1;
        sample_max >>= 1;
    }

    unsign = 1 << lsb_bits;
    mask   = unsign - 1;

    if (codebook == 2) {
        unsign_offset -= unsign;
        lsb_bits++;
    }

    for (i = 0; i < dp->blocksize; i++) {
        int32_t sample = *sample_buffer >> dp->quant_step_size[channel];
        int temp_min, temp_max;

        sample -= unsign_offset;

        temp_min = sample & mask;
        if (temp_min < offset_min)
            offset_min = temp_min;

        temp_max = unsign - temp_min - 1;
        if (temp_max < offset_max)
            offset_max = temp_max;

        sample >>= lsb_bits;

        bitcount += ff_mlp_huffman_tables[codebook][sample + codebook_offset][1];

        sample_buffer += ctx->num_channels;
    }

    bo->offset   = offset;
    bo->lsb_bits = lsb_bits;
    bo->bitcount = lsb_bits * dp->blocksize + bitcount;
    bo->min      = FFMAX(offset - offset_min, HUFF_OFFSET_MIN);
    bo->max      = FFMIN(offset + offset_max, HUFF_OFFSET_MAX);
}

/** Determines the least amount of bits needed to encode the samples using a
 *  given codebook. Searches for the best offset to minimize the bits.
 */
static inline void codebook_bits(MLPEncodeContext *ctx,
                                 unsigned int channel, int codebook,
                                 int offset, int32_t min, int32_t max,
                                 BestOffset *bo, int direction)
{
    int previous_count = INT_MAX;
    int offset_min, offset_max;
    int is_greater = 0;

    offset_min = FFMAX(min, HUFF_OFFSET_MIN);
    offset_max = FFMIN(max, HUFF_OFFSET_MAX);

    while (offset <= offset_max && offset >= offset_min) {
        BestOffset temp_bo;

        codebook_bits_offset(ctx, channel, codebook,
                             min, max, offset,
                             &temp_bo);

        if (temp_bo.bitcount < previous_count) {
            if (temp_bo.bitcount < bo->bitcount)
                *bo = temp_bo;

            is_greater = 0;
        } else if (++is_greater >= ctx->max_codebook_search)
            break;

        previous_count = temp_bo.bitcount;

        if (direction) {
            offset = temp_bo.max + 1;
        } else {
            offset = temp_bo.min - 1;
        }
    }
}

/** Determines the least amount of bits needed to encode the samples using
 *  any or no codebook.
 */
static void determine_bits(MLPEncodeContext *ctx)
{
    DecodingParams *dp = ctx->cur_decoding_params;
    RestartHeader  *rh = ctx->cur_restart_header;
    unsigned int channel;

    for (channel = 0; channel <= rh->max_channel; channel++) {
        ChannelParams *cp = &ctx->cur_channel_params[channel];
        int32_t *sample_buffer = ctx->sample_buffer + channel;
        int32_t min = INT32_MAX, max = INT32_MIN;
        int no_filters_used = !cp->filter_params[FIR].order;
        int average = 0;
        int offset = 0;
        int i;

        /* Determine extremes and average. */
        for (i = 0; i < dp->blocksize; i++) {
            int32_t sample = *sample_buffer >> dp->quant_step_size[channel];
            if (sample < min)
                min = sample;
            if (sample > max)
                max = sample;
            average += sample;
            sample_buffer += ctx->num_channels;
        }
        average /= dp->blocksize;

        /* If filtering is used, we always set the offset to zero, otherwise
         * we search for the offset that minimizes the bitcount. */
        if (no_filters_used) {
            no_codebook_bits(ctx, channel, min, max, &ctx->cur_best_offset[channel][0]);
            offset = av_clip(average, HUFF_OFFSET_MIN, HUFF_OFFSET_MAX);
        } else {
            no_codebook_bits_offset(ctx, channel, offset, min, max, &ctx->cur_best_offset[channel][0]);
        }

        for (i = 1; i < NUM_CODEBOOKS; i++) {
            BestOffset temp_bo = { 0, INT_MAX, 0, 0, 0, };
            int16_t offset_max;

            codebook_bits_offset(ctx, channel, i - 1,
                                 min, max, offset,
                                 &temp_bo);

            if (no_filters_used) {
                offset_max = temp_bo.max;

                codebook_bits(ctx, channel, i - 1, temp_bo.min - 1,
                            min, max, &temp_bo, 0);
                codebook_bits(ctx, channel, i - 1, offset_max + 1,
                            min, max, &temp_bo, 1);
            }

            ctx->cur_best_offset[channel][i] = temp_bo;
        }
    }
}

/****************************************************************************
 *************** Functions that process the data in some way ****************
 ****************************************************************************/

#define SAMPLE_MAX(bitdepth) ((1 << (bitdepth - 1)) - 1)
#define SAMPLE_MIN(bitdepth) (~SAMPLE_MAX(bitdepth))

#define MSB_MASK(bits)  (-(int)(1u << (bits)))

/** Applies the filter to the current samples, and saves the residual back
 *  into the samples buffer. If the filter is too bad and overflows the
 *  maximum amount of bits allowed (24), the samples buffer is left as is and
 *  the function returns -1.
 */
static int apply_filter(MLPEncodeContext *ctx, unsigned int channel)
{
    FilterParams *fp[NUM_FILTERS] = { &ctx->cur_channel_params[channel].filter_params[FIR],
                                      &ctx->cur_channel_params[channel].filter_params[IIR], };
    int32_t *filter_state_buffer[NUM_FILTERS] = { NULL };
    int32_t mask = MSB_MASK(ctx->cur_decoding_params->quant_step_size[channel]);
    int32_t *sample_buffer = ctx->sample_buffer + channel;
    unsigned int number_of_samples = ctx->number_of_samples;
    unsigned int filter_shift = fp[FIR]->shift;
    int filter;
    int i, ret = 0;

    for (i = 0; i < NUM_FILTERS; i++) {
        unsigned int size = ctx->number_of_samples;
        filter_state_buffer[i] = av_malloc(size*sizeof(int32_t));
        if (!filter_state_buffer[i]) {
            av_log(ctx->avctx, AV_LOG_ERROR,
                   "Not enough memory for applying filters.\n");
            ret = AVERROR(ENOMEM);
            goto free_and_return;
        }
    }

    for (i = 0; i < 8; i++) {
        filter_state_buffer[FIR][i] = *sample_buffer;
        filter_state_buffer[IIR][i] = *sample_buffer;

        sample_buffer += ctx->num_channels;
    }

    for (i = 8; i < number_of_samples; i++) {
        int32_t sample = *sample_buffer;
        unsigned int order;
        int64_t accum = 0;
        int64_t residual;

        for (filter = 0; filter < NUM_FILTERS; filter++) {
            int32_t *fcoeff = ctx->cur_channel_params[channel].coeff[filter];
            for (order = 0; order < fp[filter]->order; order++)
                accum += (int64_t)filter_state_buffer[filter][i - 1 - order] *
                         fcoeff[order];
        }

        accum  >>= filter_shift;
        residual = sample - (accum & mask);

        if (residual < SAMPLE_MIN(24) || residual > SAMPLE_MAX(24)) {
            ret = AVERROR_INVALIDDATA;
            goto free_and_return;
        }

        filter_state_buffer[FIR][i] = sample;
        filter_state_buffer[IIR][i] = (int32_t) residual;

        sample_buffer += ctx->num_channels;
    }

    sample_buffer = ctx->sample_buffer + channel;
    for (i = 0; i < number_of_samples; i++) {
        *sample_buffer = filter_state_buffer[IIR][i];

        sample_buffer += ctx->num_channels;
    }

free_and_return:
    for (i = 0; i < NUM_FILTERS; i++) {
        av_freep(&filter_state_buffer[i]);
    }

    return ret;
}

static void apply_filters(MLPEncodeContext *ctx)
{
    RestartHeader *rh = ctx->cur_restart_header;
    int channel;

    for (channel = rh->min_channel; channel <= rh->max_channel; channel++) {
        if (apply_filter(ctx, channel) < 0) {
            /* Filter is horribly wrong.
             * Clear filter params and update state. */
            set_filter_params(ctx, channel, FIR, 1);
            set_filter_params(ctx, channel, IIR, 1);
            apply_filter(ctx, channel);
        }
    }
}

/** Generates two noise channels worth of data. */
static void generate_2_noise_channels(MLPEncodeContext *ctx)
{
    int32_t *sample_buffer = ctx->sample_buffer + ctx->num_channels - 2;
    RestartHeader *rh = ctx->cur_restart_header;
    unsigned int i;
    uint32_t seed = rh->noisegen_seed;

    for (i = 0; i < ctx->number_of_samples; i++) {
        uint16_t seed_shr7 = seed >> 7;
        *sample_buffer++ = ((int8_t)(seed >> 15)) * (1 << rh->noise_shift);
        *sample_buffer++ = ((int8_t) seed_shr7)   * (1 << rh->noise_shift);

        seed = (seed << 16) ^ seed_shr7 ^ (seed_shr7 << 5);

        sample_buffer += ctx->num_channels - 2;
    }

    rh->noisegen_seed = seed & ((1 << 24)-1);
}

/** Rematrixes all channels using chosen coefficients. */
static void rematrix_channels(MLPEncodeContext *ctx)
{
    DecodingParams *dp = ctx->cur_decoding_params;
    MatrixParams *mp = &dp->matrix_params;
    int32_t *sample_buffer = ctx->sample_buffer;
    unsigned int mat, i, maxchan;

    maxchan = ctx->num_channels;

    for (mat = 0; mat < mp->count; mat++) {
        unsigned int msb_mask_bits = (ctx->avctx->sample_fmt == AV_SAMPLE_FMT_S16 ? 8 : 0) - mp->shift[mat];
        int32_t mask = MSB_MASK(msb_mask_bits);
        unsigned int outch = mp->outch[mat];

        sample_buffer = ctx->sample_buffer;
        for (i = 0; i < ctx->number_of_samples; i++) {
            unsigned int src_ch;
            int64_t accum = 0;

            for (src_ch = 0; src_ch < maxchan; src_ch++) {
                int32_t sample = *(sample_buffer + src_ch);
                accum += (int64_t) sample * mp->forco[mat][src_ch];
            }
            sample_buffer[outch] = (accum >> 14) & mask;

            sample_buffer += ctx->num_channels;
        }
    }
}

/****************************************************************************
 **** Functions that deal with determining the best parameters and output ***
 ****************************************************************************/

typedef struct {
    char    path[MAJOR_HEADER_INTERVAL + 3];
    int     bitcount;
} PathCounter;

static const char *path_counter_codebook[] = { "0", "1", "2", "3", };

#define ZERO_PATH               '0'
#define CODEBOOK_CHANGE_BITS    21

static void clear_path_counter(PathCounter *path_counter)
{
    unsigned int i;

    for (i = 0; i < NUM_CODEBOOKS + 1; i++) {
        path_counter[i].path[0]  = ZERO_PATH;
        path_counter[i].path[1]  =      0x00;
        path_counter[i].bitcount =         0;
    }
}

static int compare_best_offset(BestOffset *prev, BestOffset *cur)
{
    if (prev->lsb_bits != cur->lsb_bits)
        return 1;

    return 0;
}

static int best_codebook_path_cost(MLPEncodeContext *ctx, unsigned int channel,
                                   PathCounter *src, int cur_codebook)
{
    BestOffset *cur_bo, *prev_bo = restart_best_offset;
    int bitcount = src->bitcount;
    char *path = src->path + 1;
    int prev_codebook;
    int i;

    for (i = 0; path[i]; i++)
        prev_bo = ctx->best_offset[i][channel];

    prev_codebook = path[i - 1] - ZERO_PATH;

    cur_bo = ctx->best_offset[i][channel];

    bitcount += cur_bo[cur_codebook].bitcount;

    if (prev_codebook != cur_codebook ||
        compare_best_offset(&prev_bo[prev_codebook], &cur_bo[cur_codebook]))
        bitcount += CODEBOOK_CHANGE_BITS;

    return bitcount;
}

static void set_best_codebook(MLPEncodeContext *ctx)
{
    DecodingParams *dp = ctx->cur_decoding_params;
    RestartHeader *rh = ctx->cur_restart_header;
    unsigned int channel;

    for (channel = rh->min_channel; channel <= rh->max_channel; channel++) {
        BestOffset *cur_bo, *prev_bo = restart_best_offset;
        PathCounter path_counter[NUM_CODEBOOKS + 1];
        unsigned int best_codebook;
        unsigned int index;
        char *best_path;

        clear_path_counter(path_counter);

        for (index = 0; index < ctx->number_of_subblocks; index++) {
            unsigned int best_bitcount = INT_MAX;
            unsigned int codebook;

            cur_bo = ctx->best_offset[index][channel];

            for (codebook = 0; codebook < NUM_CODEBOOKS; codebook++) {
                int prev_best_bitcount = INT_MAX;
                int last_best;

                for (last_best = 0; last_best < 2; last_best++) {
                    PathCounter *dst_path = &path_counter[codebook];
                    PathCounter *src_path;
                    int  temp_bitcount;

                    /* First test last path with same headers,
                     * then with last best. */
                    if (last_best) {
                        src_path = &path_counter[NUM_CODEBOOKS];
                    } else {
                        if (compare_best_offset(&prev_bo[codebook], &cur_bo[codebook]))
                            continue;
                        else
                            src_path = &path_counter[codebook];
                    }

                    temp_bitcount = best_codebook_path_cost(ctx, channel, src_path, codebook);

                    if (temp_bitcount < best_bitcount) {
                        best_bitcount = temp_bitcount;
                        best_codebook = codebook;
                    }

                    if (temp_bitcount < prev_best_bitcount) {
                        prev_best_bitcount = temp_bitcount;
                        if (src_path != dst_path)
                            memcpy(dst_path, src_path, sizeof(PathCounter));
                        av_strlcat(dst_path->path, path_counter_codebook[codebook], sizeof(dst_path->path));
                        dst_path->bitcount = temp_bitcount;
                    }
                }
            }

            prev_bo = cur_bo;

            memcpy(&path_counter[NUM_CODEBOOKS], &path_counter[best_codebook], sizeof(PathCounter));
        }

        best_path = path_counter[NUM_CODEBOOKS].path + 1;

        /* Update context. */
        for (index = 0; index < ctx->number_of_subblocks; index++) {
            ChannelParams *cp = ctx->seq_channel_params + index*(ctx->avctx->channels) + channel;

            best_codebook = *best_path++ - ZERO_PATH;
            cur_bo = &ctx->best_offset[index][channel][best_codebook];

            cp->huff_offset      = cur_bo->offset;
            cp->huff_lsbs        = cur_bo->lsb_bits + dp->quant_step_size[channel];
            cp->codebook         = best_codebook;
        }
    }
}

/** Analyzes all collected bitcounts and selects the best parameters for each
 *  individual access unit.
 *  TODO This is just a stub!
 */
static void set_major_params(MLPEncodeContext *ctx)
{
    RestartHeader *rh = ctx->cur_restart_header;
    unsigned int index;
    unsigned int substr;
    uint8_t max_huff_lsbs = 0;
    uint8_t max_output_bits = 0;

    for (substr = 0; substr < ctx->num_substreams; substr++) {
        DecodingParams *seq_dp = (DecodingParams *) ctx->decoding_params+
                                 (ctx->restart_intervals - 1)*(ctx->sequence_size)*(ctx->avctx->channels) +
                                 (ctx->seq_offset[ctx->restart_intervals - 1])*(ctx->avctx->channels);

        ChannelParams *seq_cp = (ChannelParams *) ctx->channel_params +
                                (ctx->restart_intervals - 1)*(ctx->sequence_size)*(ctx->avctx->channels) +
                                (ctx->seq_offset[ctx->restart_intervals - 1])*(ctx->avctx->channels);
        unsigned int channel;
        for (index = 0; index < ctx->seq_size[ctx->restart_intervals-1]; index++) {
            memcpy(&ctx->major_decoding_params[index][substr], seq_dp + index*(ctx->num_substreams) + substr, sizeof(DecodingParams));
            for (channel = 0; channel < ctx->avctx->channels; channel++) {
                uint8_t huff_lsbs = (seq_cp + index*(ctx->avctx->channels) + channel)->huff_lsbs;
                if (max_huff_lsbs < huff_lsbs)
                    max_huff_lsbs = huff_lsbs;
                memcpy(&ctx->major_channel_params[index][channel],
                       (seq_cp + index*(ctx->avctx->channels) + channel),
                       sizeof(ChannelParams));
            }
        }
    }

    rh->max_huff_lsbs = max_huff_lsbs;

    for (index = 0; index < ctx->number_of_frames; index++)
        if (max_output_bits < ctx->max_output_bits[index])
            max_output_bits = ctx->max_output_bits[index];
    rh->max_output_bits = max_output_bits;

    for (substr = 0; substr < ctx->num_substreams; substr++) {

        ctx->cur_restart_header = &ctx->restart_header[substr];

        ctx->prev_decoding_params = &restart_decoding_params[substr];
        ctx->prev_channel_params = restart_channel_params;

        for (index = 0; index < MAJOR_HEADER_INTERVAL + 1; index++) {
                ctx->cur_decoding_params = &ctx->major_decoding_params[index][substr];
                ctx->cur_channel_params = ctx->major_channel_params[index];

                ctx->major_params_changed[index][substr] = compare_decoding_params(ctx);

                ctx->prev_decoding_params = ctx->cur_decoding_params;
                ctx->prev_channel_params = ctx->cur_channel_params;
        }
    }

    ctx->major_number_of_subblocks = ctx->number_of_subblocks;
    ctx->major_filter_state_subblock = 1;
    ctx->major_cur_subblock_index = 0;
}

static void analyze_sample_buffer(MLPEncodeContext *ctx)
{
    ChannelParams *seq_cp = ctx->seq_channel_params;
    DecodingParams *seq_dp = ctx->seq_decoding_params;
    unsigned int index;
    unsigned int substr;

    for (substr = 0; substr < ctx->num_substreams; substr++) {

        ctx->cur_restart_header = &ctx->restart_header[substr];
        ctx->cur_decoding_params = seq_dp + 1*(ctx->num_substreams) + substr;
        ctx->cur_channel_params = seq_cp + 1*(ctx->avctx->channels);

        determine_quant_step_size(ctx);
        generate_2_noise_channels(ctx);
        lossless_matrix_coeffs   (ctx);
        rematrix_channels        (ctx);
        determine_filters        (ctx);
        apply_filters            (ctx);

        copy_restart_frame_params(ctx, substr);

        /* Copy frame_size from frames 0...max to decoding_params 1...max + 1
         * decoding_params[0] is for the filter state subblock.
         */
        for (index = 0; index < ctx->number_of_frames; index++) {
            DecodingParams *dp = seq_dp + (index + 1)*(ctx->num_substreams) + substr;
            dp->blocksize = ctx->frame_size[index];
        }
        /* The official encoder seems to always encode a filter state subblock
         * even if there are no filters. TODO check if it is possible to skip
         * the filter state subblock for no filters.
         */
        (seq_dp + substr)->blocksize  = 8;
        (seq_dp + 1*(ctx->num_substreams) + substr)->blocksize -= 8;

        for (index = 0; index < ctx->number_of_subblocks; index++) {
                ctx->cur_decoding_params = seq_dp + index*(ctx->num_substreams) + substr;
                ctx->cur_channel_params = seq_cp + index*(ctx->avctx->channels);
                ctx->cur_best_offset = ctx->best_offset[index];
                determine_bits(ctx);
                ctx->sample_buffer += ctx->cur_decoding_params->blocksize * ctx->num_channels;
        }

        set_best_codebook(ctx);
    }
}

static void process_major_frame(MLPEncodeContext *ctx)
{
    unsigned int substr;

    ctx->sample_buffer = ctx->major_inout_buffer;

    ctx->starting_frame_index = 0;
    ctx->number_of_frames = ctx->major_number_of_frames;
    ctx->number_of_samples = ctx->major_frame_size;

    for (substr = 0; substr < ctx->num_substreams; substr++) {
        ctx->cur_restart_header = &ctx->restart_header[substr];

        ctx->cur_decoding_params = &ctx->major_decoding_params[1][substr];
        ctx->cur_channel_params = ctx->major_channel_params[1];

        generate_2_noise_channels(ctx);
        rematrix_channels        (ctx);

        apply_filters(ctx);
    }
}

/****************************************************************************/

static int mlp_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                            const AVFrame *frame, int *got_packet)
{
    MLPEncodeContext *ctx = avctx->priv_data;
    unsigned int bytes_written = 0;
    int restart_frame, ret;
    uint8_t *data;

    if ((ret = ff_alloc_packet2(avctx, avpkt, 87500 * avctx->channels, 0)) < 0)
        return ret;

    /* add current frame to queue */
    if ((ret = ff_af_queue_add(&ctx->afq, frame)) < 0)
        return ret;

    data = frame->data[0];

    ctx->frame_index = avctx->frame_number % ctx->max_restart_interval;

    ctx->inout_buffer = ctx->major_inout_buffer
                      + ctx->frame_index * ctx->one_sample_buffer_size;

    if (ctx->last_frame == ctx->inout_buffer) {
        return 0;
    }

    ctx->sample_buffer = ctx->major_scratch_buffer
                       + ctx->frame_index * ctx->one_sample_buffer_size;

    ctx->write_buffer = ctx->inout_buffer;

    if (avctx->frame_number < ctx->max_restart_interval) {
        if (data) {
            goto input_and_return;
        } else {
            /* There are less frames than the requested major header interval.
             * Update the context to reflect this.
             */
            ctx->max_restart_interval = avctx->frame_number;
            ctx->frame_index = 0;

            ctx->sample_buffer = ctx->major_scratch_buffer;
            ctx->inout_buffer = ctx->major_inout_buffer;
        }
    }

    if (ctx->frame_size[ctx->frame_index] > MAX_BLOCKSIZE) {
        av_log(avctx, AV_LOG_ERROR, "Invalid frame size (%d > %d)\n",
               ctx->frame_size[ctx->frame_index], MAX_BLOCKSIZE);
        return AVERROR_INVALIDDATA;
    }

    restart_frame = !ctx->frame_index;

    if (restart_frame) {
        set_major_params(ctx);
        if (ctx->min_restart_interval != ctx->max_restart_interval)
            process_major_frame(ctx);
    }

    if (ctx->min_restart_interval == ctx->max_restart_interval)
        ctx->write_buffer = ctx->sample_buffer;

    bytes_written = write_access_unit(ctx, avpkt->data, avpkt->size, restart_frame);

    ctx->timestamp += ctx->frame_size[ctx->frame_index];
    ctx->dts       += ctx->frame_size[ctx->frame_index];

input_and_return:

    if (data) {
        ctx->frame_size[ctx->frame_index] = avctx->frame_size;
        ctx->next_major_frame_size += avctx->frame_size;
        ctx->next_major_number_of_frames++;
        input_data(ctx, data);
    } else if (!ctx->last_frame) {
        ctx->last_frame = ctx->inout_buffer;
    }

    restart_frame = (ctx->frame_index + 1) % ctx->min_restart_interval;

    if (!restart_frame) {
        int seq_index;

        for (seq_index = 0;
             seq_index < ctx->restart_intervals && (seq_index * ctx->min_restart_interval) <= ctx->avctx->frame_number;
             seq_index++) {
            unsigned int number_of_samples = 0;
            unsigned int index;

            ctx->sample_buffer = ctx->major_scratch_buffer;
            ctx->inout_buffer = ctx->major_inout_buffer;
            ctx->seq_index = seq_index;

            ctx->starting_frame_index = (ctx->avctx->frame_number - (ctx->avctx->frame_number % ctx->min_restart_interval)
                                      - (seq_index * ctx->min_restart_interval)) % ctx->max_restart_interval;
            ctx->number_of_frames = ctx->next_major_number_of_frames;
            ctx->number_of_subblocks = ctx->next_major_number_of_frames + 1;

            ctx->seq_channel_params = (ChannelParams *) ctx->channel_params +
                                      (ctx->frame_index / ctx->min_restart_interval)*(ctx->sequence_size)*(ctx->avctx->channels) +
                                      (ctx->seq_offset[seq_index])*(ctx->avctx->channels);

            ctx->seq_decoding_params = (DecodingParams *) ctx->decoding_params +
                                       (ctx->frame_index / ctx->min_restart_interval)*(ctx->sequence_size)*(ctx->num_substreams) +
                                       (ctx->seq_offset[seq_index])*(ctx->num_substreams);

            for (index = 0; index < ctx->number_of_frames; index++) {
                number_of_samples += ctx->frame_size[(ctx->starting_frame_index + index) % ctx->max_restart_interval];
            }
            ctx->number_of_samples = number_of_samples;

            for (index = 0; index < ctx->seq_size[seq_index]; index++) {
                clear_channel_params(ctx, ctx->seq_channel_params + index*(ctx->avctx->channels));
                default_decoding_params(ctx, ctx->seq_decoding_params + index*(ctx->num_substreams));
            }

            input_to_sample_buffer(ctx);

            analyze_sample_buffer(ctx);
        }

        if (ctx->frame_index == (ctx->max_restart_interval - 1)) {
            ctx->major_frame_size = ctx->next_major_frame_size;
            ctx->next_major_frame_size = 0;
            ctx->major_number_of_frames = ctx->next_major_number_of_frames;
            ctx->next_major_number_of_frames = 0;

            if (!ctx->major_frame_size)
                goto no_data_left;
        }
    }

no_data_left:

    ff_af_queue_remove(&ctx->afq, avctx->frame_size, &avpkt->pts,
                       &avpkt->duration);
    avpkt->size = bytes_written;
    *got_packet = 1;
    return 0;
}

static av_cold int mlp_encode_close(AVCodecContext *avctx)
{
    MLPEncodeContext *ctx = avctx->priv_data;

    ff_lpc_end(&ctx->lpc_ctx);

    av_freep(&ctx->lossless_check_data);
    av_freep(&ctx->major_scratch_buffer);
    av_freep(&ctx->major_inout_buffer);
    av_freep(&ctx->lpc_sample_buffer);
    av_freep(&ctx->decoding_params);
    av_freep(&ctx->channel_params);
    av_freep(&ctx->frame_size);
    av_freep(&ctx->max_output_bits);
    ff_af_queue_close(&ctx->afq);

    return 0;
}

#if CONFIG_MLP_ENCODER
AVCodec ff_mlp_encoder = {
    .name                   ="mlp",
    .long_name              = NULL_IF_CONFIG_SMALL("MLP (Meridian Lossless Packing)"),
    .type                   = AVMEDIA_TYPE_AUDIO,
    .id                     = AV_CODEC_ID_MLP,
    .priv_data_size         = sizeof(MLPEncodeContext),
    .init                   = mlp_encode_init,
    .encode2                = mlp_encode_frame,
    .close                  = mlp_encode_close,
    .capabilities           = AV_CODEC_CAP_SMALL_LAST_FRAME | AV_CODEC_CAP_EXPERIMENTAL,
    .sample_fmts            = (const enum AVSampleFormat[]) {AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE},
    .supported_samplerates  = (const int[]) {44100, 48000, 88200, 96000, 176400, 192000, 0},
    .channel_layouts        = ff_mlp_channel_layouts,
    .caps_internal          = FF_CODEC_CAP_INIT_CLEANUP,
};
#endif
#if CONFIG_TRUEHD_ENCODER
AVCodec ff_truehd_encoder = {
    .name                   ="truehd",
    .long_name              = NULL_IF_CONFIG_SMALL("TrueHD"),
    .type                   = AVMEDIA_TYPE_AUDIO,
    .id                     = AV_CODEC_ID_TRUEHD,
    .priv_data_size         = sizeof(MLPEncodeContext),
    .init                   = mlp_encode_init,
    .encode2                = mlp_encode_frame,
    .close                  = mlp_encode_close,
    .capabilities           = AV_CODEC_CAP_SMALL_LAST_FRAME | AV_CODEC_CAP_EXPERIMENTAL,
    .sample_fmts            = (const enum AVSampleFormat[]) {AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE},
    .supported_samplerates  = (const int[]) {44100, 48000, 88200, 96000, 176400, 192000, 0},
    .channel_layouts        = (const uint64_t[]) {AV_CH_LAYOUT_STEREO, AV_CH_LAYOUT_5POINT0_BACK, AV_CH_LAYOUT_5POINT1_BACK, 0},
    .caps_internal          = FF_CODEC_CAP_INIT_CLEANUP,
};
#endif
