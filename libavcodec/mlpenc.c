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

#include "config_components.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "put_bits.h"
#include "audio_frame_queue.h"
#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/crc.h"
#include "libavutil/avstring.h"
#include "libavutil/intmath.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "libavutil/thread.h"
#include "mlp_parse.h"
#include "mlp.h"
#include "lpc.h"

#define MAX_NCHANNELS (MAX_CHANNELS + 2)

#define MIN_HEADER_INTERVAL    8
#define MAX_HEADER_INTERVAL  128

#define MLP_MIN_LPC_ORDER      1
#define MLP_MAX_LPC_ORDER      8
#define MLP_MIN_LPC_SHIFT      0
#define MLP_MAX_LPC_SHIFT     15

typedef struct RestartHeader {
    uint8_t         min_channel;         ///< The index of the first channel coded in this substream.
    uint8_t         max_channel;         ///< The index of the last channel coded in this substream.
    uint8_t         max_matrix_channel;  ///< The number of channels input into the rematrix stage.

    int8_t          max_shift;
    uint8_t         noise_shift;         ///< The left shift applied to random noise in 0x31ea substreams.
    uint32_t        noisegen_seed;       ///< The current seed value for the pseudorandom noise generator(s).

    uint8_t         data_check_present;  ///< Set if the substream contains extra info to check the size of VLC blocks.

    int32_t         lossless_check_data; ///< XOR of all output samples

    uint8_t         max_huff_lsbs;       ///< largest huff_lsbs
    uint8_t         max_output_bits;     ///< largest output bit-depth
} RestartHeader;

typedef struct MatrixParams {
    uint8_t         count;                  ///< number of matrices to apply

    uint8_t         outch[MAX_MATRICES];    ///< output channel for each matrix
    int32_t         forco[MAX_MATRICES][MAX_NCHANNELS];    ///< forward coefficients
    int32_t         coeff[MAX_MATRICES][MAX_NCHANNELS];    ///< decoding coefficients
    uint8_t         fbits[MAX_MATRICES];    ///< fraction bits

    int8_t          noise_shift[MAX_CHANNELS];
    uint8_t         lsb_bypass[MAX_MATRICES];
    int8_t          bypassed_lsbs[MAX_MATRICES][MAX_BLOCKSIZE];
} MatrixParams;

#define PARAMS_DEFAULT (0xff)
#define PARAM_PRESENCE_FLAGS (1 << 8)

typedef struct DecodingParams {
    uint16_t        blocksize;                  ///< number of PCM samples in current audio block
    uint8_t         quant_step_size[MAX_CHANNELS];  ///< left shift to apply to Huffman-decoded residuals
    int8_t          output_shift[MAX_CHANNELS]; ///< Left shift to apply to decoded PCM values to get final 24-bit output.
    uint8_t         max_order[MAX_CHANNELS];

    MatrixParams    matrix_params;

    uint8_t         param_presence_flags;       ///< Bitmask of which parameter sets are conveyed in a decoding parameter block.
    int32_t         sample_buffer[MAX_NCHANNELS][MAX_BLOCKSIZE];
} DecodingParams;

typedef struct BestOffset {
    int32_t offset;
    uint32_t bitcount;
    uint8_t lsb_bits;
    int32_t min;
    int32_t max;
} BestOffset;

#define HUFF_OFFSET_MIN    (-16384)
#define HUFF_OFFSET_MAX    ( 16383)

/** Number of possible codebooks (counting "no codebooks") */
#define NUM_CODEBOOKS       4

typedef struct MLPBlock {
    unsigned int    seq_size;
    ChannelParams   channel_params[MAX_CHANNELS];
    DecodingParams  decoding_params;
    int32_t         lossless_check_data;
    unsigned int    max_output_bits; ///< largest output bit-depth
    BestOffset      best_offset[MAX_CHANNELS][NUM_CODEBOOKS];
    ChannelParams   major_channel_params[MAX_CHANNELS]; ///< ChannelParams to be written to bitstream.
    DecodingParams  major_decoding_params;              ///< DecodingParams to be written to bitstream.
    int             major_params_changed;               ///< params_changed to be written to bitstream.
    int32_t         inout_buffer[MAX_NCHANNELS][MAX_BLOCKSIZE];
} MLPBlock;

typedef struct MLPSubstream {
    RestartHeader   restart_header;
    RestartHeader  *cur_restart_header;
    MLPBlock        b[MAX_HEADER_INTERVAL + 1];
    unsigned int    major_cur_subblock_index;
    unsigned int    major_filter_state_subblock;
    int32_t         coefs[MAX_CHANNELS][MAX_LPC_ORDER][MAX_LPC_ORDER];
} MLPSubstream;

typedef struct MLPEncodeContext {
    AVClass        *class;
    AVCodecContext *avctx;

    int             max_restart_interval;   ///< Max interval of access units in between two major frames.
    int             min_restart_interval;   ///< Min interval of access units in between two major frames.
    int             cur_restart_interval;
    int             lpc_coeff_precision;
    int             rematrix_precision;
    int             lpc_type;
    int             lpc_passes;
    int             prediction_order;
    int             max_codebook_search;

    int             num_substreams;         ///< Number of substreams contained within this stream.

    int             num_channels;   /**< Number of channels in major_scratch_buffer.
                                     *   Normal channels + noise channels. */

    int             coded_sample_fmt [2];   ///< sample format encoded for MLP
    int             coded_sample_rate[2];   ///< sample rate encoded for MLP
    int             coded_peak_bitrate;     ///< peak bitrate for this major sync header

    int             flags;                  ///< major sync info flags

    /* channel_meaning */
    int             substream_info;
    int             thd_substream_info;
    int             fs;
    int             wordlength;
    int             channel_occupancy;
    int             summary_info;

    int32_t         last_frames;            ///< Signal last frames.

    unsigned int    major_number_of_frames;
    unsigned int    next_major_number_of_frames;

    unsigned int    major_frame_size;       ///< Number of samples in current major frame being encoded.
    unsigned int    next_major_frame_size;  ///< Counter of number of samples for next major frame.

    unsigned int    frame_index;            ///< Index of current frame being encoded.

    unsigned int    restart_intervals;      ///< Number of possible major frame sizes.

    uint16_t        output_timing;          ///< Timestamp of current access unit.
    uint16_t        input_timing;           ///< Decoding timestamp of current access unit.

    uint8_t         noise_type;
    uint8_t         channel_arrangement;    ///< channel arrangement for MLP streams
    uint16_t        channel_arrangement8;   ///< 8 channel arrangement for THD streams

    uint8_t         multichannel_type6ch;   ///< channel modifier for TrueHD stream 0
    uint8_t         multichannel_type8ch;   ///< channel modifier for TrueHD stream 0
    uint8_t         ch2_presentation_mod;   ///< channel modifier for TrueHD stream 0
    uint8_t         ch6_presentation_mod;   ///< channel modifier for TrueHD stream 1
    uint8_t         ch8_presentation_mod;   ///< channel modifier for TrueHD stream 2

    MLPSubstream    s[2];
    int32_t         filter_state[NUM_FILTERS][MAX_HEADER_INTERVAL * MAX_BLOCKSIZE];
    int32_t         lpc_sample_buffer[MAX_HEADER_INTERVAL * MAX_BLOCKSIZE];

    AudioFrameQueue afq;

    /* Analysis stage. */
    unsigned int    number_of_frames;
    unsigned int    number_of_subblocks;

    int             shorten_by;

    LPCContext      lpc_ctx;
} MLPEncodeContext;

static ChannelParams   restart_channel_params[MAX_CHANNELS];
static DecodingParams  restart_decoding_params[MAX_SUBSTREAMS];
static const BestOffset restart_best_offset[NUM_CODEBOOKS] = {{0}};

#define SYNC_MAJOR      0xf8726f
#define MAJOR_SYNC_INFO_SIGNATURE   0xB752

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

    if (prev->order != fp->order)
        return 1;

    if (!fp->order)
        return 0;

    if (prev->shift != fp->shift)
        return 1;

    for (int i = 0; i < fp->order; i++)
        if (prev_cp->coeff[filter][i] != cp->coeff[filter][i])
            return 1;

    return 0;
}

/** Compare two primitive matrices and returns 1 if anything has changed.
 *  Returns 0 if they are both equal.
 */
static int compare_matrix_params(MLPEncodeContext *ctx, MLPSubstream *s,
                                 const MatrixParams *prev, const MatrixParams *mp)
{
    RestartHeader *rh = s->cur_restart_header;

    if (prev->count != mp->count)
        return 1;

    if (!mp->count)
        return 0;

    for (unsigned int mat = 0; mat < mp->count; mat++) {
        if (prev->outch[mat] != mp->outch[mat])
            return 1;

        if (prev->fbits[mat] != mp->fbits[mat])
            return 1;

        if (prev->noise_shift[mat] != mp->noise_shift[mat])
            return 1;

        if (prev->lsb_bypass[mat] != mp->lsb_bypass[mat])
            return 1;

        for (int ch = 0; ch <= rh->max_matrix_channel; ch++)
            if (prev->coeff[mat][ch] != mp->coeff[mat][ch])
                return 1;
    }

    return 0;
}

/** Compares two DecodingParams and ChannelParams structures to decide if a
 *  new decoding params header has to be written.
 */
static int compare_decoding_params(MLPEncodeContext *ctx,
                                   MLPSubstream *s,
                                   unsigned int index)
{
    const DecodingParams *prev = index ? &s->b[index-1].major_decoding_params : restart_decoding_params;
    DecodingParams *dp = &s->b[index].major_decoding_params;
    const MatrixParams *prev_mp = &prev->matrix_params;
    MatrixParams *mp = &dp->matrix_params;
    RestartHeader *rh = s->cur_restart_header;
    int retval = 0;

    if (prev->param_presence_flags != dp->param_presence_flags)
        retval |= PARAM_PRESENCE_FLAGS;

    if (prev->blocksize != dp->blocksize)
        retval |= PARAM_BLOCKSIZE;

    if (compare_matrix_params(ctx, s, prev_mp, mp))
        retval |= PARAM_MATRIX;

    for (int ch = 0; ch <= rh->max_matrix_channel; ch++)
        if (prev->output_shift[ch] != dp->output_shift[ch]) {
            retval |= PARAM_OUTSHIFT;
            break;
        }

    for (int ch = 0; ch <= rh->max_channel; ch++)
        if (prev->quant_step_size[ch] != dp->quant_step_size[ch]) {
            retval |= PARAM_QUANTSTEP;
            break;
        }

    for (int ch = rh->min_channel; ch <= rh->max_channel; ch++) {
        const ChannelParams *prev_cp = index ? &s->b[index-1].major_channel_params[ch] : &restart_channel_params[ch];
        ChannelParams *cp = &s->b[index].major_channel_params[ch];

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
            retval |= PARAM_PRESENCE;
    }

    return retval;
}

static void copy_filter_params(ChannelParams *dst_cp, ChannelParams *src_cp, int filter)
{
    FilterParams *dst = &dst_cp->filter_params[filter];
    FilterParams *src = &src_cp->filter_params[filter];

    dst->order = src->order;

    if (dst->order) {
        dst->shift = src->shift;

        dst->coeff_shift = src->coeff_shift;
        dst->coeff_bits = src->coeff_bits;
    }

    for (int order = 0; order < dst->order; order++)
        dst_cp->coeff[filter][order] = src_cp->coeff[filter][order];
}

static void copy_matrix_params(MatrixParams *dst, MatrixParams *src)
{
    dst->count = src->count;

    if (!dst->count)
        return;

    for (int count = 0; count < MAX_MATRICES; count++) {
        dst->outch[count] = src->outch[count];
        dst->fbits[count] = src->fbits[count];
        dst->noise_shift[count] = src->noise_shift[count];
        dst->lsb_bypass[count] = src->lsb_bypass[count];

        for (int channel = 0; channel < MAX_NCHANNELS; channel++)
            dst->coeff[count][channel] = src->coeff[count][channel];
    }
}

static void copy_restart_frame_params(MLPEncodeContext *ctx, MLPSubstream *s)
{
    RestartHeader *rh = s->cur_restart_header;

    for (unsigned int index = 0; index < ctx->number_of_subblocks; index++) {
        DecodingParams *dp = &s->b[index].decoding_params;

        copy_matrix_params(&dp->matrix_params, &s->b[1].decoding_params.matrix_params);

        for (int ch = 0; ch <= rh->max_matrix_channel; ch++)
            dp->output_shift[ch] = s->b[1].decoding_params.output_shift[ch];

        for (int ch = 0; ch <= rh->max_channel; ch++) {
            ChannelParams *cp = &s->b[index].channel_params[ch];

            dp->quant_step_size[ch] = s->b[1].decoding_params.quant_step_size[ch];

            if (index)
                for (unsigned int filter = 0; filter < NUM_FILTERS; filter++)
                    copy_filter_params(cp, &s->b[1].channel_params[ch], filter);
        }
    }
}

/** Clears a DecodingParams struct the way it should be after a restart header. */
static void clear_decoding_params(DecodingParams *decoding_params)
{
    DecodingParams *dp = decoding_params;

    dp->param_presence_flags   = 0xff;
    dp->blocksize              = 0;

    memset(&dp->matrix_params,  0, sizeof(dp->matrix_params  ));
    memset(dp->quant_step_size, 0, sizeof(dp->quant_step_size));
    memset(dp->sample_buffer,   0, sizeof(dp->sample_buffer  ));
    memset(dp->output_shift,    0, sizeof(dp->output_shift   ));
    memset(dp->max_order, MAX_FIR_ORDER, sizeof(dp->max_order));
}

/** Clears a ChannelParams struct the way it should be after a restart header. */
static void clear_channel_params(ChannelParams *channel_params, int nb_channels)
{
    for (unsigned channel = 0; channel < nb_channels; channel++) {
        ChannelParams *cp = &channel_params[channel];

        memset(&cp->filter_params, 0, sizeof(cp->filter_params));

        /* Default audio coding is 24-bit raw PCM. */
        cp->huff_offset      =  0;
        cp->codebook         =  0;
        cp->huff_lsbs        = 24;
    }
}

/** Sets default vales in our encoder for a DecodingParams struct. */
static void default_decoding_params(MLPEncodeContext *ctx, DecodingParams *dp)
{
    uint8_t param_presence_flags = 0;

    clear_decoding_params(dp);

    param_presence_flags |= PARAM_BLOCKSIZE;
    param_presence_flags |= PARAM_MATRIX;
    param_presence_flags |= PARAM_OUTSHIFT;
    param_presence_flags |= PARAM_QUANTSTEP;
    param_presence_flags |= PARAM_FIR;
    param_presence_flags |= PARAM_IIR;
    param_presence_flags |= PARAM_HUFFOFFSET;
    param_presence_flags |= PARAM_PRESENCE;

    dp->param_presence_flags = param_presence_flags;
}

/****************************************************************************/

/** Calculates the smallest number of bits it takes to encode a given signed
 *  value in two's complement.
 */
static int inline number_sbits(int32_t n)
{
    return 33 - ff_clz(FFABS(n)|1) - !n;
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

static av_cold void mlp_encode_init_static(void)
{
    clear_channel_params (restart_channel_params,  MAX_CHANNELS);
    clear_decoding_params(restart_decoding_params);
    ff_mlp_init_crc();
}

static av_cold int mlp_encode_init(AVCodecContext *avctx)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    MLPEncodeContext *ctx = avctx->priv_data;
    uint64_t channels_present;
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

    ctx->coded_peak_bitrate = mlp_peak_bitrate(9600000, avctx->sample_rate);

    ctx->substream_info |= SUBSTREAM_INFO_ALWAYS_SET;
    if (avctx->ch_layout.nb_channels <= 2)
        ctx->substream_info |= SUBSTREAM_INFO_MAX_2_CHAN;

    switch (avctx->sample_fmt) {
    case AV_SAMPLE_FMT_S16P:
        ctx->coded_sample_fmt[0] = BITS_16;
        ctx->wordlength = 16;
        avctx->bits_per_raw_sample = 16;
        break;
    /* TODO 20 bits: */
    case AV_SAMPLE_FMT_S32P:
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

    ctx->input_timing = -avctx->frame_size;

    ctx->num_channels = avctx->ch_layout.nb_channels + 2; /* +2 noise channels */

    ctx->min_restart_interval = ctx->cur_restart_interval = ctx->max_restart_interval;
    ctx->restart_intervals = ctx->max_restart_interval / ctx->min_restart_interval;

    ctx->num_substreams = 1;

    channels_present = av_channel_layout_subset(&avctx->ch_layout, ~(uint64_t)0);
    if (ctx->avctx->codec_id == AV_CODEC_ID_MLP) {
        static const uint64_t layout_arrangement[] = {
            AV_CH_LAYOUT_MONO,         AV_CH_LAYOUT_STEREO,
            AV_CH_LAYOUT_2_1,          AV_CH_LAYOUT_QUAD,
            AV_CH_LAYOUT_2POINT1,      0, 0,
            AV_CH_LAYOUT_SURROUND,     AV_CH_LAYOUT_4POINT0,
            AV_CH_LAYOUT_5POINT0_BACK, AV_CH_LAYOUT_3POINT1,
            AV_CH_LAYOUT_4POINT1,      AV_CH_LAYOUT_5POINT1_BACK,
        };
        int i;

        for (i = 0;; i++) {
            av_assert1(i < FF_ARRAY_ELEMS(layout_arrangement) ||
                       !"Impossible channel layout");
            if (channels_present == layout_arrangement[i])
                break;
        }
        ctx->channel_arrangement = i;
        ctx->flags = FLAGS_DVDA;
        ctx->channel_occupancy = ff_mlp_ch_info[ctx->channel_arrangement].channel_occupancy;
        ctx->summary_info      = ff_mlp_ch_info[ctx->channel_arrangement].summary_info     ;
    } else {
        /* TrueHD */
        ctx->num_substreams = 1 + (avctx->ch_layout.nb_channels > 2);
        switch (channels_present) {
        case AV_CH_LAYOUT_MONO:
            ctx->ch2_presentation_mod= 3;
            ctx->ch6_presentation_mod= 3;
            ctx->ch8_presentation_mod= 3;
            ctx->thd_substream_info  = 0x14;
            break;
        case AV_CH_LAYOUT_STEREO:
            ctx->ch2_presentation_mod= 1;
            ctx->ch6_presentation_mod= 1;
            ctx->ch8_presentation_mod= 1;
            ctx->thd_substream_info  = 0x14;
            break;
        case AV_CH_LAYOUT_2POINT1:
        case AV_CH_LAYOUT_SURROUND:
        case AV_CH_LAYOUT_3POINT1:
        case AV_CH_LAYOUT_4POINT0:
        case AV_CH_LAYOUT_4POINT1:
        case AV_CH_LAYOUT_5POINT0:
        case AV_CH_LAYOUT_5POINT1:
            ctx->ch2_presentation_mod= 0;
            ctx->ch6_presentation_mod= 0;
            ctx->ch8_presentation_mod= 0;
            ctx->thd_substream_info  = 0x3C;
            break;
        default:
            av_assert1(!"AVCodec.ch_layouts needs to be updated");
        }
        ctx->flags = 0;
        ctx->channel_occupancy = 0;
        ctx->summary_info = 0;
        ctx->channel_arrangement =
        ctx->channel_arrangement8 = layout_truehd(channels_present);
    }

    for (unsigned int index = 0; index < ctx->restart_intervals; index++) {
        for (int n = 0; n < ctx->num_substreams; n++)
            ctx->s[n].b[index].seq_size = ((index + 1) * ctx->min_restart_interval) + 1;
    }


    /* TODO see if noisegen_seed is really worth it. */
    if (ctx->avctx->codec_id == AV_CODEC_ID_MLP) {
        RestartHeader *const rh = &ctx->s[0].restart_header;

        rh->noisegen_seed      = 0;
        rh->min_channel        = 0;
        rh->max_channel        = avctx->ch_layout.nb_channels - 1;
        rh->max_matrix_channel = rh->max_channel;
    } else {
        RestartHeader *rh = &ctx->s[0].restart_header;

        rh->noisegen_seed      = 0;
        rh->min_channel        = 0;
        rh->max_channel        = FFMIN(avctx->ch_layout.nb_channels, 2) - 1;
        rh->max_matrix_channel = rh->max_channel;

        if (avctx->ch_layout.nb_channels > 2) {
            rh = &ctx->s[1].restart_header;

            rh->noisegen_seed      = 0;
            rh->min_channel        = 2;
            rh->max_channel        = avctx->ch_layout.nb_channels - 1;
            rh->max_matrix_channel = rh->max_channel;
        }
    }

    if ((ret = ff_lpc_init(&ctx->lpc_ctx, ctx->avctx->frame_size,
                           MLP_MAX_LPC_ORDER, ctx->lpc_type)) < 0)
        return ret;

    ff_af_queue_init(avctx, &ctx->afq);

    ff_thread_once(&init_static_once, mlp_encode_init_static);

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
        put_bits(&pb,  1, ctx->multichannel_type6ch);
        put_bits(&pb,  1, ctx->multichannel_type8ch);
        put_bits(&pb,  2, 0                        ); /* ignored */
        put_bits(&pb,  2, ctx->ch2_presentation_mod);
        put_bits(&pb,  2, ctx->ch6_presentation_mod);
        put_bits(&pb,  5, ctx->channel_arrangement );
        put_bits(&pb,  2, ctx->ch8_presentation_mod);
        put_bits(&pb, 13, ctx->channel_arrangement8);
    }

    put_bits(&pb, 16, MAJOR_SYNC_INFO_SIGNATURE);
    put_bits(&pb, 16, ctx->flags               );
    put_bits(&pb, 16, 0                        ); /* ignored */
    put_bits(&pb,  1, 1                        ); /* is_vbr */
    put_bits(&pb, 15, ctx->coded_peak_bitrate  );
    put_bits(&pb,  4, ctx->num_substreams      );
    put_bits(&pb,  2, 0                        ); /* ignored */
    put_bits(&pb,  2, 0                        ); /* extended substream info */

    /* channel_meaning */
    if (ctx->avctx->codec_id == AV_CODEC_ID_MLP) {
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
    } else if (ctx->avctx->codec_id == AV_CODEC_ID_TRUEHD) {
        put_bits(&pb,  8, ctx->thd_substream_info  );
        put_bits(&pb,  6, 0                        ); /* reserved */
        put_bits(&pb,  1, 0                        ); /* 2ch control enabled */
        put_bits(&pb,  1, 0                        ); /* 6ch control enabled */
        put_bits(&pb,  1, 0                        ); /* 8ch control enabled */
        put_bits(&pb,  1, 0                        ); /* reserved */
        put_bits(&pb,  7, 0                        ); /* drc start up gain */
        put_bits(&pb,  6, 0                        ); /* 2ch dialogue norm */
        put_bits(&pb,  6, 0                        ); /* 2ch mix level */
        put_bits(&pb,  5, 0                        ); /* 6ch dialogue norm */
        put_bits(&pb,  6, 0                        ); /* 6ch mix level */
        put_bits(&pb,  5, 0                        ); /* 6ch source format */
        put_bits(&pb,  5, 0                        ); /* 8ch dialogue norm */
        put_bits(&pb,  6, 0                        ); /* 8ch mix level */
        put_bits(&pb,  6, 0                        ); /* 8ch source format */
        put_bits(&pb,  1, 0                        ); /* reserved */
        put_bits(&pb,  1, 0                        ); /* extra channel meaning present */
    }

    flush_put_bits(&pb);

    AV_WL16(buf+26, ff_mlp_checksum16(buf, 26));
}

/** Writes a restart header to the bitstream. Damaged streams can start being
 *  decoded losslessly again after such a header and the subsequent decoding
 *  params header.
 */
static void write_restart_header(MLPEncodeContext *ctx, MLPSubstream *s,
                                 PutBitContext *pb)
{
    RestartHeader *rh = s->cur_restart_header;
    uint8_t lossless_check = xor_32_to_8(rh->lossless_check_data);
    unsigned int start_count = put_bits_count(pb);
    PutBitContext tmpb;
    uint8_t checksum;

    put_bits(pb, 14, 0x31ea                ); /* TODO 0x31eb */
    put_bits(pb, 16, ctx->output_timing    );
    put_bits(pb,  4, rh->min_channel       );
    put_bits(pb,  4, rh->max_channel       );
    put_bits(pb,  4, rh->max_matrix_channel);
    put_bits(pb,  4, rh->noise_shift       );
    put_bits(pb, 23, rh->noisegen_seed     );
    put_bits(pb,  4, rh->max_shift         );
    put_bits(pb,  5, rh->max_huff_lsbs     );
    put_bits(pb,  5, rh->max_output_bits   );
    put_bits(pb,  5, rh->max_output_bits   );
    put_bits(pb,  1, rh->data_check_present);
    put_bits(pb,  8, lossless_check        );
    put_bits(pb, 16, 0                     ); /* ignored */

    for (int ch = 0; ch <= rh->max_matrix_channel; ch++)
        put_bits(pb, 6, ch);

    /* Data must be flushed for the checksum to be correct. */
    tmpb = *pb;
    flush_put_bits(&tmpb);

    checksum = ff_mlp_restart_checksum(pb->buf, put_bits_count(pb) - start_count);

    put_bits(pb,  8, checksum);
}

/** Writes matrix params for all primitive matrices to the bitstream. */
static void write_matrix_params(MLPEncodeContext *ctx,
                                MLPSubstream *s,
                                DecodingParams *dp,
                                PutBitContext *pb)
{
    RestartHeader *rh = s->cur_restart_header;
    MatrixParams *mp = &dp->matrix_params;
    int max_channel = rh->max_matrix_channel;

    put_bits(pb, 4, mp->count);

    if (!ctx->noise_type)
        max_channel += 2;

    for (unsigned int mat = 0; mat < mp->count; mat++) {
        put_bits(pb, 4, mp->outch[mat]); /* matrix_out_ch */
        put_bits(pb, 4, mp->fbits[mat]);
        put_bits(pb, 1, mp->lsb_bypass[mat]);

        for (int ch = 0; ch <= max_channel; ch++) {
            int32_t coeff = mp->coeff[mat][ch];

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
static void write_filter_params(MLPEncodeContext *ctx,
                                ChannelParams *cp,
                                PutBitContext *pb,
                                int channel, unsigned int filter)
{
    FilterParams *fp = &cp->filter_params[filter];

    put_bits(pb, 4, fp->order);

    if (fp->order > 0) {
        int32_t *fcoeff = cp->coeff[filter];

        put_bits(pb, 4, fp->shift      );
        put_bits(pb, 5, fp->coeff_bits );
        put_bits(pb, 3, fp->coeff_shift);

        for (int i = 0; i < fp->order; i++) {
            put_sbits(pb, fp->coeff_bits, fcoeff[i] >> fp->coeff_shift);
        }

        /* TODO state data for IIR filter. */
        put_bits(pb, 1, 0);
    }
}

/** Writes decoding parameters to the bitstream. These change very often,
 *  usually at almost every frame.
 */
static void write_decoding_params(MLPEncodeContext *ctx, MLPSubstream *s,
                                  PutBitContext *pb, int params_changed,
                                  unsigned int subblock_index)
{
    DecodingParams *dp = &s->b[subblock_index].major_decoding_params;
    RestartHeader *rh = s->cur_restart_header;

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
            write_matrix_params(ctx, s, dp, pb);
        } else {
            put_bits(pb, 1, 0);
        }
    }

    if (dp->param_presence_flags & PARAM_OUTSHIFT) {
        if (params_changed       & PARAM_OUTSHIFT) {
            put_bits(pb, 1, 1);
            for (int ch = 0; ch <= rh->max_matrix_channel; ch++)
                put_sbits(pb, 4, dp->output_shift[ch]);
        } else {
            put_bits(pb, 1, 0);
        }
    }

    if (dp->param_presence_flags & PARAM_QUANTSTEP) {
        if (params_changed       & PARAM_QUANTSTEP) {
            put_bits(pb, 1, 1);
            for (int ch = 0; ch <= rh->max_channel; ch++)
                put_bits(pb, 4, dp->quant_step_size[ch]);
        } else {
            put_bits(pb, 1, 0);
        }
    }

    for (int ch = rh->min_channel; ch <= rh->max_channel; ch++) {
        ChannelParams *cp = &s->b[subblock_index].major_channel_params[ch];

        if (dp->param_presence_flags & 0xF) {
            put_bits(pb, 1, 1);

            if (dp->param_presence_flags & PARAM_FIR) {
                if (params_changed       & PARAM_FIR) {
                    put_bits(pb, 1, 1);
                    write_filter_params(ctx, cp, pb, ch, FIR);
                } else {
                    put_bits(pb, 1, 0);
                }
            }

            if (dp->param_presence_flags & PARAM_IIR) {
                if (params_changed       & PARAM_IIR) {
                    put_bits(pb, 1, 1);
                    write_filter_params(ctx, cp, pb, ch, IIR);
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
                av_log(ctx->avctx, AV_LOG_ERROR, "Invalid Huff LSBs %d\n", cp->huff_lsbs);
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
static void write_block_data(MLPEncodeContext *ctx, MLPSubstream *s,
                             PutBitContext *pb, unsigned int subblock_index)
{
    RestartHeader *rh = s->cur_restart_header;
    DecodingParams *dp = &s->b[subblock_index].major_decoding_params;
    MatrixParams *mp = &dp->matrix_params;
    int32_t sign_huff_offset[MAX_CHANNELS];
    int codebook_index      [MAX_CHANNELS];
    int lsb_bits            [MAX_CHANNELS];

    for (int ch = rh->min_channel; ch <= rh->max_channel; ch++) {
        ChannelParams *cp = &s->b[subblock_index].major_channel_params[ch];
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

    for (unsigned int i = 0; i < dp->blocksize; i++) {
        for (unsigned int mat = 0; mat < mp->count; mat++) {
            if (mp->lsb_bypass[mat]) {
                const int8_t *bypassed_lsbs = mp->bypassed_lsbs[mat];

                put_bits(pb, 1, bypassed_lsbs[i]);
            }
        }

        for (int ch = rh->min_channel; ch <= rh->max_channel; ch++) {
            int32_t *sample_buffer = dp->sample_buffer[ch];
            int32_t sample = sample_buffer[i] >> dp->quant_step_size[ch];
            sample -= sign_huff_offset[ch];

            if (codebook_index[ch] >= 0) {
                int vlc = sample >> lsb_bits[ch];
                put_bits(pb, ff_mlp_huffman_tables[codebook_index[ch]][vlc][1],
                             ff_mlp_huffman_tables[codebook_index[ch]][vlc][0]);
                sample &= ((1 << lsb_bits[ch]) - 1);
            }

            put_bits(pb, lsb_bits[ch], sample);
        }
    }
}

/** Writes the substream data to the bitstream. */
static uint8_t *write_substr(MLPEncodeContext *ctx,
                             MLPSubstream *s,
                             uint8_t *buf, int buf_size,
                             int restart_frame,
                             uint16_t *substream_data_len)
{
    int32_t *lossless_check_data = &s->b[ctx->frame_index].lossless_check_data;
    unsigned int cur_subblock_index = s->major_cur_subblock_index;
    unsigned int num_subblocks = s->major_filter_state_subblock;
    RestartHeader *rh = &s->restart_header;
    int substr_restart_frame = restart_frame;
    uint8_t parity, checksum;
    PutBitContext pb;
    int params_changed;

    s->cur_restart_header = rh;

    init_put_bits(&pb, buf, buf_size);

    for (unsigned int subblock = 0; subblock <= num_subblocks; subblock++) {
        unsigned int subblock_index = cur_subblock_index++;

        params_changed = s->b[subblock_index].major_params_changed;

        if (substr_restart_frame || params_changed) {
            put_bits(&pb, 1, 1);

            if (substr_restart_frame) {
                put_bits(&pb, 1, 1);

                write_restart_header(ctx, s, &pb);
                rh->lossless_check_data = 0;
            } else {
                put_bits(&pb, 1, 0);
            }

            write_decoding_params(ctx, s, &pb, params_changed,
                                  subblock_index);
        } else {
            put_bits(&pb, 1, 0);
        }

        write_block_data(ctx, s, &pb, subblock_index);

        put_bits(&pb, 1, !substr_restart_frame);

        substr_restart_frame = 0;
    }

    put_bits(&pb, (-put_bits_count(&pb)) & 15, 0);

    rh->lossless_check_data ^= lossless_check_data[0];

    if (ctx->last_frames == 0 && ctx->shorten_by) {
        if (ctx->avctx->codec_id == AV_CODEC_ID_TRUEHD) {
            put_bits(&pb, 16, END_OF_STREAM & 0xFFFF);
            put_bits(&pb, 16, (ctx->shorten_by & 0x1FFF) | 0xE000);
        } else {
            put_bits32(&pb, END_OF_STREAM);
        }
    }

    /* Data must be flushed for the checksum and parity to be correct;
     * notice that we already are word-aligned here. */
    flush_put_bits(&pb);

    parity   = ff_mlp_calculate_parity(buf, put_bytes_output(&pb)) ^ 0xa9;
    checksum = ff_mlp_checksum8       (buf, put_bytes_output(&pb));

    put_bits(&pb, 8, parity  );
    put_bits(&pb, 8, checksum);

    flush_put_bits(&pb);

    substream_data_len[0] = put_bytes_output(&pb);

    buf += substream_data_len[0];

    s->major_cur_subblock_index += s->major_filter_state_subblock + 1;
    s->major_filter_state_subblock = 0;

    return buf;
}

/** Writes the access unit and substream headers to the bitstream. */
static void write_frame_headers(MLPEncodeContext *ctx, uint8_t *frame_header,
                                uint8_t *substream_headers, unsigned int length,
                                int restart_frame,
                                uint16_t substream_data_len[MAX_SUBSTREAMS])
{
    uint16_t access_unit_header = 0;
    uint16_t substream_data_end = 0;
    uint16_t parity_nibble = 0;

    parity_nibble  = ctx->input_timing;
    parity_nibble ^= length;

    for (unsigned int substr = 0; substr < ctx->num_substreams; substr++) {
        uint16_t substr_hdr = 0;

        substream_data_end += substream_data_len[substr];

        substr_hdr |= (0 << 15); /* extraword */
        substr_hdr |= (!restart_frame << 14); /* !restart_frame */
        substr_hdr |= (1 << 13); /* checkdata */
        substr_hdr |= (0 << 12); /* ??? */
        substr_hdr |= (substream_data_end / 2) & 0x0FFF;

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
    AV_WB16(frame_header+2, ctx->input_timing );
}

/** Writes an entire access unit to the bitstream. */
static int write_access_unit(MLPEncodeContext *ctx, uint8_t *buf,
                             int buf_size, int restart_frame)
{
    uint16_t substream_data_len[MAX_SUBSTREAMS];
    uint8_t *buf1, *buf0 = buf;
    int total_length;

    /* Frame header will be written at the end. */
    buf      += 4;
    buf_size -= 4;

    if (restart_frame) {
        write_major_sync(ctx, buf, buf_size);
        buf      += 28;
        buf_size -= 28;
    }

    buf1 = buf;

    /* Substream headers will be written at the end. */
    for (unsigned int substr = 0; substr < ctx->num_substreams; substr++) {
        buf      += 2;
        buf_size -= 2;
    }

    for (int substr = 0; substr < ctx->num_substreams; substr++) {
        MLPSubstream *s = &ctx->s[substr];
        uint8_t *buf0 = buf;

        buf = write_substr(ctx, s, buf, buf_size, restart_frame, &substream_data_len[substr]);
        buf_size -= buf - buf0;
    }

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
static void input_data_internal(MLPEncodeContext *ctx, MLPSubstream *s,
                                uint8_t **const samples,
                                int nb_samples, int is24)
{
    int32_t *lossless_check_data = &s->b[ctx->frame_index].lossless_check_data;
    RestartHeader *rh = &s->restart_header;
    int32_t temp_lossless_check_data = 0;
    uint32_t bits = 0;

    for (int i = 0; i < nb_samples; i++) {
        for (int ch = 0; ch <= rh->max_channel; ch++) {
            const int32_t *samples_32 = (const int32_t *)samples[ch];
            const int16_t *samples_16 = (const int16_t *)samples[ch];
            int32_t *sample_buffer = s->b[ctx->frame_index].inout_buffer[ch];
            int32_t sample;

            sample = is24 ? samples_32[i] >> 8 : samples_16[i] * 256;

            bits = FFMAX(number_sbits(sample), bits);

            temp_lossless_check_data ^= (sample & 0x00ffffff) << ch;
            sample_buffer[i] = sample;
        }
    }

    for (int ch = 0; ch <= rh->max_channel; ch++) {
        for (int i = nb_samples; i < ctx->avctx->frame_size; i++) {
            int32_t *sample_buffer = s->b[ctx->frame_index].inout_buffer[ch];

            sample_buffer[i] = 0;
        }
    }

    s->b[ctx->frame_index].max_output_bits = bits;

    lossless_check_data[0] = temp_lossless_check_data;
}

/** Wrapper function for inputting data in two different bit-depths. */
static void input_data(MLPEncodeContext *ctx, MLPSubstream *s, uint8_t **const samples, int nb_samples)
{
    input_data_internal(ctx, s, samples, nb_samples, ctx->avctx->sample_fmt == AV_SAMPLE_FMT_S32P);
}

static void input_to_sample_buffer(MLPEncodeContext *ctx, MLPSubstream *s)
{
    RestartHeader *rh = &s->restart_header;

    for (unsigned int index = 0; index < ctx->number_of_frames; index++) {
        unsigned int cur_index = (ctx->frame_index + index + 1) % ctx->cur_restart_interval;
        DecodingParams *dp = &s->b[index+1].decoding_params;

        for (int ch = 0; ch <= rh->max_channel; ch++) {
            const int32_t *input_buffer = s->b[cur_index].inout_buffer[ch];
            int32_t *sample_buffer = dp->sample_buffer[ch];
            int off = 0;

            if (dp->blocksize < ctx->avctx->frame_size) {
                DecodingParams *dp = &s->b[index].decoding_params;
                int32_t *sample_buffer = dp->sample_buffer[ch];
                for (unsigned int i = 0; i < dp->blocksize; i++)
                    sample_buffer[i] = input_buffer[i];
                off = dp->blocksize;
            }

            for (unsigned int i = 0; i < dp->blocksize; i++)
                sample_buffer[i] = input_buffer[i + off];
        }
    }
}

/****************************************************************************
 ********* Functions that analyze the data and set the parameters ***********
 ****************************************************************************/

/** Counts the number of trailing zeroes in a value */
static int number_trailing_zeroes(int32_t sample, unsigned int max, unsigned int def)
{
    return sample ? FFMIN(max, ff_ctz(sample)) : def;
}

static void determine_output_shift(MLPEncodeContext *ctx, MLPSubstream *s)
{
    RestartHeader *rh = s->cur_restart_header;
    DecodingParams *dp1 = &s->b[1].decoding_params;
    int32_t sample_mask[MAX_CHANNELS];

    memset(sample_mask, 0, sizeof(sample_mask));

    for (int j = 0; j <= ctx->cur_restart_interval; j++) {
        DecodingParams *dp = &s->b[j].decoding_params;

        for (int ch = 0; ch <= rh->max_matrix_channel; ch++) {
            int32_t *sample_buffer = dp->sample_buffer[ch];

            for (int i = 0; i < dp->blocksize; i++)
                sample_mask[ch] |= sample_buffer[i];
        }
    }

    for (int ch = 0; ch <= rh->max_matrix_channel; ch++)
        dp1->output_shift[ch] = number_trailing_zeroes(sample_mask[ch], 7, 0);

    for (int j = 0; j <= ctx->cur_restart_interval; j++) {
        DecodingParams *dp = &s->b[j].decoding_params;

        for (int ch = 0; ch <= rh->max_matrix_channel; ch++) {
            int32_t *sample_buffer = dp->sample_buffer[ch];
            const int shift = dp1->output_shift[ch];

            for (int i = 0; i < dp->blocksize; i++)
                sample_buffer[i] >>= shift;
        }
    }
}

/** Determines how many bits are zero at the end of all samples so they can be
 *  shifted out.
 */
static void determine_quant_step_size(MLPEncodeContext *ctx, MLPSubstream *s)
{
    RestartHeader *rh = s->cur_restart_header;
    DecodingParams *dp1 = &s->b[1].decoding_params;
    int32_t sample_mask[MAX_CHANNELS];

    memset(sample_mask, 0, sizeof(sample_mask));

    for (int j = 0; j <= ctx->cur_restart_interval; j++) {
        DecodingParams *dp = &s->b[j].decoding_params;

        for (int ch = 0; ch <= rh->max_channel; ch++) {
            int32_t *sample_buffer = dp->sample_buffer[ch];

            for (int i = 0; i < dp->blocksize; i++)
                sample_mask[ch] |= sample_buffer[i];
        }
    }

    for (int ch = 0; ch <= rh->max_channel; ch++)
        dp1->quant_step_size[ch] = number_trailing_zeroes(sample_mask[ch], 15, 0);
}

/** Determines the smallest number of bits needed to encode the filter
 *  coefficients, and if it's possible to right-shift their values without
 *  losing any precision.
 */
static void code_filter_coeffs(MLPEncodeContext *ctx, FilterParams *fp, const int32_t *fcoeff)
{
    uint32_t coeff_mask = 0;
    int bits = 0, shift;

    for (int order = 0; order < fp->order; order++) {
        int32_t coeff = fcoeff[order];

        bits = FFMAX(number_sbits(coeff), bits);

        coeff_mask |= coeff;
    }

    shift = FFMIN(7, coeff_mask ? ff_ctz(coeff_mask) : 0);

    fp->coeff_bits  = FFMAX(1, bits - shift);
    fp->coeff_shift = FFMIN(shift, 16 - fp->coeff_bits);
}

/** Determines the best filter parameters for the given data and writes the
 *  necessary information to the context.
 */
static void set_filter(MLPEncodeContext *ctx, MLPSubstream *s,
                       int channel, int retry_filter)
{
    ChannelParams *cp = &s->b[1].channel_params[channel];
    DecodingParams *dp1 = &s->b[1].decoding_params;
    FilterParams *fp = &cp->filter_params[FIR];

    if (retry_filter)
        dp1->max_order[channel]--;

    if (dp1->max_order[channel] == 0) {
        fp->order = 0;
    } else {
        int32_t *lpc_samples = ctx->lpc_sample_buffer;
        int32_t *fcoeff = cp->coeff[FIR];
        int shift[MAX_LPC_ORDER];
        int order;

        for (unsigned int j = 0; j <= ctx->cur_restart_interval; j++) {
            DecodingParams *dp = &s->b[j].decoding_params;
            int32_t *sample_buffer = dp->sample_buffer[channel];

            for (unsigned int i = 0; i < dp->blocksize; i++)
                lpc_samples[i] = sample_buffer[i];
            lpc_samples += dp->blocksize;
        }

        order = ff_lpc_calc_coefs(&ctx->lpc_ctx, ctx->lpc_sample_buffer,
                                  lpc_samples - ctx->lpc_sample_buffer,
                                  MLP_MIN_LPC_ORDER, dp1->max_order[channel],
                                  ctx->lpc_coeff_precision,
                                  s->coefs[channel], shift, ctx->lpc_type, ctx->lpc_passes,
                                  ctx->prediction_order, MLP_MIN_LPC_SHIFT,
                                  MLP_MAX_LPC_SHIFT, 0);

        fp->order = order;
        fp->shift = order ? shift[order-1] : 0;

        for (unsigned int i = 0; i < order; i++)
            fcoeff[i] = s->coefs[channel][order-1][i];

        code_filter_coeffs(ctx, fp, fcoeff);
    }
}

/** Tries to determine a good prediction filter, and applies it to the samples
 *  buffer if the filter is good enough. Sets the filter data to be cleared if
 *  no good filter was found.
 */
static void determine_filters(MLPEncodeContext *ctx, MLPSubstream *s)
{
    RestartHeader *rh = s->cur_restart_header;

    for (int ch = rh->min_channel; ch <= rh->max_channel; ch++)
        set_filter(ctx, s, ch, 0);
}

static int estimate_coeff(MLPEncodeContext *ctx, MLPSubstream *s,
                          MatrixParams *mp,
                          int ch0, int ch1)
{
    int32_t maxl = INT32_MIN, maxr = INT32_MIN, minl = INT32_MAX, minr = INT32_MAX;
    int64_t summ = 0, sums = 0, suml = 0, sumr = 0, enl = 0, enr = 0;
    const int shift = 14 - ctx->rematrix_precision;
    int32_t cf0, cf1, e[4], d[4];
    int64_t ml, mr;
    int i, count = 0;

    for (int j = 0; j <= ctx->cur_restart_interval; j++) {
        DecodingParams *dp = &s->b[j].decoding_params;
        const int32_t *ch[2];

        ch[0] = dp->sample_buffer[ch0];
        ch[1] = dp->sample_buffer[ch1];

        for (int i = 0; i < dp->blocksize; i++) {
            int32_t lm = ch[0][i], rm = ch[1][i];

            enl  += FFABS(lm);
            enr  += FFABS(rm);

            summ += FFABS(lm + rm);
            sums += FFABS(lm - rm);

            suml += lm;
            sumr += rm;

            maxl = FFMAX(maxl, lm);
            maxr = FFMAX(maxr, rm);

            minl = FFMIN(minl, lm);
            minr = FFMIN(minr, rm);
        }
    }

    summ -= FFABS(suml + sumr);
    sums -= FFABS(suml - sumr);

    ml = maxl - (int64_t)minl;
    mr = maxr - (int64_t)minr;

    if (!summ && !sums)
        return 0;

    if (!ml || !mr)
        return 0;

    if ((FFABS(ml) + FFABS(mr)) >= (1 << 24))
        return 0;

    cf0 = (FFMIN(FFABS(mr), FFABS(ml)) * (1LL << 14)) / FFMAX(FFABS(ml), FFABS(mr));
    cf0 = (cf0 >> shift) << shift;
    cf1 = -cf0;

    if (sums > summ)
        FFSWAP(int32_t, cf0, cf1);

    count = 1;
    i = enl < enr;
    mp->outch[0] = ch0 + i;

    d[!i] = cf0;
    d[ i] = 1 << 14;
    e[!i] = cf1;
    e[ i] = 1 << 14;

    mp->coeff[0][ch0] = av_clip_intp2(d[0], 15);
    mp->coeff[0][ch1] = av_clip_intp2(d[1], 15);

    mp->forco[0][ch0] = av_clip_intp2(e[0], 15);
    mp->forco[0][ch1] = av_clip_intp2(e[1], 15);

    return count;
}

/** Determines how many fractional bits are needed to encode matrix
 *  coefficients. Also shifts the coefficients to fit within 2.14 bits.
 */
static void code_matrix_coeffs(MLPEncodeContext *ctx, MLPSubstream *s,
                               DecodingParams *dp,
                               unsigned int mat)
{
    RestartHeader *rh = s->cur_restart_header;
    MatrixParams *mp = &dp->matrix_params;
    int32_t coeff_mask = 0;

    for (int ch = 0; ch <= rh->max_matrix_channel; ch++)
        coeff_mask |= mp->coeff[mat][ch];

    mp->fbits[mat] = 14 - number_trailing_zeroes(coeff_mask, 14, 14);
}

/** Determines best coefficients to use for the lossless matrix. */
static void lossless_matrix_coeffs(MLPEncodeContext *ctx, MLPSubstream *s)
{
    RestartHeader *rh = s->cur_restart_header;
    DecodingParams *dp = &s->b[1].decoding_params;
    MatrixParams *mp = &dp->matrix_params;

    mp->count = 0;
    if (ctx->num_channels - 2 != 2)
        return;

    mp->count = estimate_coeff(ctx, s, mp,
                               rh->min_channel, rh->max_channel);

    for (int mat = 0; mat < mp->count; mat++)
        code_matrix_coeffs(ctx, s, dp, mat);
}

/** Min and max values that can be encoded with each codebook. The values for
 *  the third codebook take into account the fact that the sign shift for this
 *  codebook is outside the coded value, so it has one more bit of precision.
 *  It should actually be -7 -> 7, shifted down by 0.5.
 */
static const int8_t codebook_extremes[3][2] = {
    {-9, 8}, {-8, 7}, {-15, 14},
};

/** Determines the amount of bits needed to encode the samples using no
 *  codebooks and a specified offset.
 */
static void no_codebook_bits_offset(MLPEncodeContext *ctx,
                                    DecodingParams *dp,
                                    int channel, int32_t offset,
                                    int32_t min, int32_t max,
                                    BestOffset *bo)
{
    int32_t unsign = 0;
    int lsb_bits;

    min -= offset;
    max -= offset;

    lsb_bits = FFMAX(number_sbits(min), number_sbits(max)) - 1;

    lsb_bits += !!lsb_bits;

    if (lsb_bits > 0)
        unsign = 1U << (lsb_bits - 1);

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
                             DecodingParams *dp,
                             int channel,
                             int32_t min, int32_t max,
                             BestOffset *bo)
{
    int32_t offset, unsign = 0;
    uint8_t lsb_bits;

    /* Set offset inside huffoffset's boundaries by adjusting extremes
     * so that more bits are used, thus shifting the offset. */
    if (min < HUFF_OFFSET_MIN)
        max = FFMAX(max, 2 * HUFF_OFFSET_MIN - min + 1);
    if (max > HUFF_OFFSET_MAX)
        min = FFMIN(min, 2 * HUFF_OFFSET_MAX - max - 1);

    lsb_bits = FFMAX(number_sbits(min), number_sbits(max));

    if (lsb_bits > 0)
        unsign = 1 << (lsb_bits - 1);

    /* If all samples are the same (lsb_bits == 0), offset must be
     * adjusted because of sign_shift. */
    offset = min + (max - min) / 2 + !!lsb_bits;

    bo->offset   = offset;
    bo->lsb_bits = lsb_bits;
    bo->bitcount = lsb_bits * dp->blocksize;
    bo->min      = max - unsign + 1;
    bo->max      = min + unsign;
    bo->min      = FFMAX(bo->min, HUFF_OFFSET_MIN);
    bo->max      = FFMIN(bo->max, HUFF_OFFSET_MAX);
}

/** Determines the least amount of bits needed to encode the samples using a
 *  given codebook and a given offset.
 */
static inline void codebook_bits_offset(MLPEncodeContext *ctx,
                                        DecodingParams *dp,
                                        int channel, int codebook,
                                        int32_t sample_min, int32_t sample_max,
                                        int32_t offset, BestOffset *bo)
{
    int32_t codebook_min = codebook_extremes[codebook][0];
    int32_t codebook_max = codebook_extremes[codebook][1];
    int32_t *sample_buffer = dp->sample_buffer[channel];
    int codebook_offset  = 7 + (2 - codebook);
    int32_t unsign_offset = offset;
    uint32_t bitcount = 0;
    int lsb_bits = 0;
    int offset_min = INT_MAX, offset_max = INT_MAX;
    int unsign, mask;

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

    for (int i = 0; i < dp->blocksize; i++) {
        int32_t sample = sample_buffer[i] >> dp->quant_step_size[channel];
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
                                 DecodingParams *dp,
                                 int channel, int codebook,
                                 int offset, int32_t min, int32_t max,
                                 BestOffset *bo, int direction)
{
    uint32_t previous_count = UINT32_MAX;
    int offset_min, offset_max;
    int is_greater = 0;

    offset_min = FFMAX(min, HUFF_OFFSET_MIN);
    offset_max = FFMIN(max, HUFF_OFFSET_MAX);

    while (offset <= offset_max && offset >= offset_min) {
        BestOffset temp_bo;

        codebook_bits_offset(ctx, dp, channel, codebook,
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
static void determine_bits(MLPEncodeContext *ctx, MLPSubstream *s)
{
    RestartHeader *rh = s->cur_restart_header;
    for (unsigned int index = 0; index < ctx->number_of_subblocks; index++) {
        DecodingParams *dp = &s->b[index].decoding_params;

        for (int ch = rh->min_channel; ch <= rh->max_channel; ch++) {
            ChannelParams *cp = &s->b[index].channel_params[ch];
            int32_t *sample_buffer = dp->sample_buffer[ch];
            int32_t min = INT32_MAX, max = INT32_MIN;
            int no_filters_used = !cp->filter_params[FIR].order;
            int average = 0;
            int offset = 0;

            /* Determine extremes and average. */
            for (int i = 0; i < dp->blocksize; i++) {
                int32_t sample = sample_buffer[i] >> dp->quant_step_size[ch];
                if (sample < min)
                    min = sample;
                if (sample > max)
                    max = sample;
                average += sample;
            }
            average /= dp->blocksize;

            /* If filtering is used, we always set the offset to zero, otherwise
             * we search for the offset that minimizes the bitcount. */
            if (no_filters_used) {
                no_codebook_bits(ctx, dp, ch, min, max, &s->b[index].best_offset[ch][0]);
                offset = av_clip(average, HUFF_OFFSET_MIN, HUFF_OFFSET_MAX);
            } else {
                no_codebook_bits_offset(ctx, dp, ch, offset, min, max, &s->b[index].best_offset[ch][0]);
            }

            for (int i = 1; i < NUM_CODEBOOKS; i++) {
                BestOffset temp_bo = { 0, UINT32_MAX, 0, 0, 0, };
                int32_t offset_max;

                codebook_bits_offset(ctx, dp, ch, i - 1,
                                     min, max, offset,
                                     &temp_bo);

                if (no_filters_used) {
                    offset_max = temp_bo.max;

                    codebook_bits(ctx, dp, ch, i - 1, temp_bo.min - 1,
                                  min, max, &temp_bo, 0);
                    codebook_bits(ctx, dp, ch, i - 1, offset_max + 1,
                                  min, max, &temp_bo, 1);
                }

                s->b[index].best_offset[ch][i] = temp_bo;
            }
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
static int apply_filter(MLPEncodeContext *ctx, MLPSubstream *s, int channel)
{
    DecodingParams *dp = &s->b[1].decoding_params;
    ChannelParams *cp = &s->b[1].channel_params[channel];
    FilterParams *fp[NUM_FILTERS] = { &cp->filter_params[FIR],
                                      &cp->filter_params[IIR], };
    const uint8_t codebook = cp->codebook;
    int32_t mask = MSB_MASK(dp->quant_step_size[channel]);
    int32_t *sample_buffer = s->b[0].decoding_params.sample_buffer[channel];
    unsigned int filter_shift = fp[FIR]->shift;
    int32_t *filter_state[NUM_FILTERS] = { ctx->filter_state[FIR],
                                           ctx->filter_state[IIR], };
    int i, j = 1, k = 0;

    for (i = 0; i < 8; i++) {
        filter_state[FIR][i] = sample_buffer[i];
        filter_state[IIR][i] = sample_buffer[i];
    }

    while (1) {
        int32_t *sample_buffer = s->b[j].decoding_params.sample_buffer[channel];
        unsigned int blocksize = s->b[j].decoding_params.blocksize;
        int32_t sample, residual;
        int64_t accum = 0;

        if (!blocksize)
            break;

        for (int filter = 0; filter < NUM_FILTERS; filter++) {
            int32_t *fcoeff = cp->coeff[filter];
            for (unsigned int order = 0; order < fp[filter]->order; order++)
                accum += (int64_t)filter_state[filter][i - 1 - order] *
                    fcoeff[order];
        }

        sample = sample_buffer[k];
        accum  >>= filter_shift;
        residual = sample - (accum & mask);

        if ((codebook > 0) &&
            (residual < SAMPLE_MIN(24) ||
             residual > SAMPLE_MAX(24)))
            return -1;

        filter_state[FIR][i] = sample;
        filter_state[IIR][i] = residual;

        i++;
        k++;
        if (k >= blocksize) {
            k = 0;
            j++;
            if (j > ctx->cur_restart_interval)
                break;
        }
    }

    for (int l = 0, j = 0; j <= ctx->cur_restart_interval; j++) {
        int32_t *sample_buffer = s->b[j].decoding_params.sample_buffer[channel];
        unsigned int blocksize = s->b[j].decoding_params.blocksize;

        for (int i = 0; i < blocksize; i++, l++)
            sample_buffer[i] = filter_state[IIR][l];
    }

    return 0;
}

static void apply_filters(MLPEncodeContext *ctx, MLPSubstream *s)
{
    RestartHeader *rh = s->cur_restart_header;

    for (int ch = rh->min_channel; ch <= rh->max_channel; ch++) {
        while (apply_filter(ctx, s, ch) < 0) {
            /* Filter is horribly wrong. Retry. */
            set_filter(ctx, s, ch, 1);
        }
    }
}

/** Generates two noise channels worth of data. */
static void generate_2_noise_channels(MLPEncodeContext *ctx, MLPSubstream *s)
{
    RestartHeader *rh = s->cur_restart_header;
    uint32_t seed = rh->noisegen_seed;

    for (unsigned int j = 0; j <= ctx->cur_restart_interval; j++) {
        DecodingParams *dp = &s->b[j].decoding_params;
        int32_t *sample_buffer2 = dp->sample_buffer[ctx->num_channels-2];
        int32_t *sample_buffer1 = dp->sample_buffer[ctx->num_channels-1];

        for (unsigned int i = 0; i < dp->blocksize; i++) {
            uint16_t seed_shr7 = seed >> 7;
            sample_buffer2[i] = ((int8_t)(seed >> 15)) * (1 << rh->noise_shift);
            sample_buffer1[i] = ((int8_t) seed_shr7)   * (1 << rh->noise_shift);

            seed = (seed << 16) ^ seed_shr7 ^ (seed_shr7 << 5);
        }
    }

    rh->noisegen_seed = seed & ((1 << 24)-1);
}

/** Rematrixes all channels using chosen coefficients. */
static void rematrix_channels(MLPEncodeContext *ctx, MLPSubstream *s)
{
    RestartHeader *rh = s->cur_restart_header;
    DecodingParams *dp1 = &s->b[1].decoding_params;
    MatrixParams *mp1 = &dp1->matrix_params;
    const int maxchan = rh->max_matrix_channel;
    int32_t orig_samples[MAX_NCHANNELS];
    int32_t rematrix_samples[MAX_NCHANNELS];
    uint8_t lsb_bypass[MAX_MATRICES] = { 0 };

    for (unsigned int j = 0; j <= ctx->cur_restart_interval; j++) {
        DecodingParams *dp = &s->b[j].decoding_params;
        MatrixParams *mp = &dp->matrix_params;

        for (unsigned int i = 0; i < dp->blocksize; i++) {
            for (int ch = 0; ch <= maxchan; ch++)
                orig_samples[ch] = rematrix_samples[ch] = dp->sample_buffer[ch][i];

            for (int mat = 0; mat < mp1->count; mat++) {
                unsigned int outch = mp1->outch[mat];
                int64_t accum = 0;

                for (int ch = 0; ch <= maxchan; ch++) {
                    int32_t sample = rematrix_samples[ch];

                    accum += (int64_t)sample * mp1->forco[mat][ch];
                }

                rematrix_samples[outch] = accum >> 14;
            }

            for (int ch = 0; ch <= maxchan; ch++)
                dp->sample_buffer[ch][i] = rematrix_samples[ch];

            for (unsigned int mat = 0; mat < mp1->count; mat++) {
                int8_t *bypassed_lsbs = mp->bypassed_lsbs[mat];
                unsigned int outch = mp1->outch[mat];
                int64_t accum = 0;
                int8_t bit;

                for (int ch = 0; ch <= maxchan; ch++) {
                    int32_t sample = rematrix_samples[ch];

                    accum += (int64_t)sample * mp1->coeff[mat][ch];
                }

                rematrix_samples[outch] = accum >> 14;
                bit = rematrix_samples[outch] != orig_samples[outch];

                bypassed_lsbs[i] = bit;
                lsb_bypass[mat] |= bit;
            }
        }
    }

    for (unsigned int mat = 0; mat < mp1->count; mat++)
        mp1->lsb_bypass[mat] = lsb_bypass[mat];
}

/****************************************************************************
 **** Functions that deal with determining the best parameters and output ***
 ****************************************************************************/

typedef struct PathCounter {
    char    path[MAX_HEADER_INTERVAL + 2];
    int     cur_idx;
    uint32_t bitcount;
} PathCounter;

#define CODEBOOK_CHANGE_BITS    21

static void clear_path_counter(PathCounter *path_counter)
{
    memset(path_counter, 0, (NUM_CODEBOOKS + 1) * sizeof(*path_counter));
}

static int compare_best_offset(const BestOffset *prev, const BestOffset *cur)
{
    return prev->lsb_bits != cur->lsb_bits;
}

static uint32_t best_codebook_path_cost(MLPEncodeContext *ctx, MLPSubstream *s,
                                        int channel,
                                        PathCounter *src, int cur_codebook)
{
    int idx = src->cur_idx;
    const BestOffset *cur_bo = s->b[idx].best_offset[channel],
                    *prev_bo = idx ? s->b[idx - 1].best_offset[channel] :
                                     restart_best_offset;
    uint32_t bitcount = src->bitcount;
    int prev_codebook = src->path[idx];

    bitcount += cur_bo[cur_codebook].bitcount;

    if (prev_codebook != cur_codebook ||
        compare_best_offset(&prev_bo[prev_codebook], &cur_bo[cur_codebook]))
        bitcount += CODEBOOK_CHANGE_BITS;

    return bitcount;
}

static void set_best_codebook(MLPEncodeContext *ctx, MLPSubstream *s)
{
    RestartHeader *rh = s->cur_restart_header;

    for (int channel = rh->min_channel; channel <= rh->max_channel; channel++) {
        const BestOffset *prev_bo = restart_best_offset;
        BestOffset *cur_bo;
        PathCounter path_counter[NUM_CODEBOOKS + 1];
        unsigned int best_codebook;
        char *best_path;

        clear_path_counter(path_counter);

        for (unsigned int index = 0; index < ctx->number_of_subblocks; index++) {
            uint32_t best_bitcount = UINT32_MAX;

            cur_bo = s->b[index].best_offset[channel];

            for (unsigned int codebook = 0; codebook < NUM_CODEBOOKS; codebook++) {
                uint32_t prev_best_bitcount = UINT32_MAX;

                for (unsigned int last_best = 0; last_best < 2; last_best++) {
                    PathCounter *dst_path = &path_counter[codebook];
                    PathCounter *src_path;
                    uint32_t temp_bitcount;

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

                    temp_bitcount = best_codebook_path_cost(ctx, s, channel, src_path, codebook);

                    if (temp_bitcount < best_bitcount) {
                        best_bitcount = temp_bitcount;
                        best_codebook = codebook;
                    }

                    if (temp_bitcount < prev_best_bitcount) {
                        prev_best_bitcount = temp_bitcount;
                        if (src_path != dst_path)
                            memcpy(dst_path, src_path, sizeof(PathCounter));
                        if (dst_path->cur_idx < FF_ARRAY_ELEMS(dst_path->path) - 1)
                            dst_path->path[++dst_path->cur_idx] = codebook;
                        dst_path->bitcount = temp_bitcount;
                    }
                }
            }

            prev_bo = cur_bo;

            memcpy(&path_counter[NUM_CODEBOOKS], &path_counter[best_codebook], sizeof(PathCounter));
        }

        best_path = path_counter[NUM_CODEBOOKS].path + 1;

        /* Update context. */
        for (unsigned int index = 0; index < ctx->number_of_subblocks; index++) {
            ChannelParams *cp = &s->b[index].channel_params[channel];
            DecodingParams *dp = &s->b[index].decoding_params;

            best_codebook = *best_path++;
            cur_bo = &s->b[index].best_offset[channel][best_codebook];

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
static void set_major_params(MLPEncodeContext *ctx, MLPSubstream *s)
{
    RestartHeader *rh = s->cur_restart_header;
    uint8_t max_huff_lsbs = 0, max_output_bits = 0;
    int8_t max_shift = 0;

    for (int index = 0; index < s->b[ctx->restart_intervals-1].seq_size; index++) {
        memcpy(&s->b[index].major_decoding_params,
               &s->b[index].decoding_params, sizeof(DecodingParams));
        for (int ch = 0; ch <= rh->max_matrix_channel; ch++) {
            int8_t shift = s->b[index].decoding_params.output_shift[ch];

            max_shift = FFMAX(max_shift, shift);
        }
        for (int ch = rh->min_channel; ch <= rh->max_channel; ch++) {
            uint8_t huff_lsbs = s->b[index].channel_params[ch].huff_lsbs;

            max_huff_lsbs = FFMAX(max_huff_lsbs, huff_lsbs);

            memcpy(&s->b[index].major_channel_params[ch],
                   &s->b[index].channel_params[ch],
                   sizeof(ChannelParams));
        }
    }

    rh->max_huff_lsbs = max_huff_lsbs;
    rh->max_shift     = max_shift;

    for (int index = 0; index < ctx->number_of_frames; index++)
        if (max_output_bits < s->b[index].max_output_bits)
            max_output_bits = s->b[index].max_output_bits;
    rh->max_output_bits = max_output_bits;

    s->cur_restart_header = &s->restart_header;

    for (int index = 0; index <= ctx->cur_restart_interval; index++)
        s->b[index].major_params_changed = compare_decoding_params(ctx, s, index);

    s->major_filter_state_subblock = 1;
    s->major_cur_subblock_index = 0;
}

static void analyze_sample_buffer(MLPEncodeContext *ctx, MLPSubstream *s)
{
    s->cur_restart_header = &s->restart_header;

    /* Copy frame_size from frames 0...max to decoding_params 1...max + 1
     * decoding_params[0] is for the filter state subblock.
     */
    for (unsigned int index = 0; index < ctx->number_of_frames; index++) {
        DecodingParams *dp = &s->b[index+1].decoding_params;
        dp->blocksize = ctx->avctx->frame_size;
    }
    /* The official encoder seems to always encode a filter state subblock
     * even if there are no filters. TODO check if it is possible to skip
     * the filter state subblock for no filters.
     */
    s->b[0].decoding_params.blocksize  = 8;
    s->b[1].decoding_params.blocksize -= 8;

    input_to_sample_buffer   (ctx, s);
    determine_output_shift   (ctx, s);
    generate_2_noise_channels(ctx, s);
    lossless_matrix_coeffs   (ctx, s);
    rematrix_channels        (ctx, s);
    determine_quant_step_size(ctx, s);
    determine_filters        (ctx, s);
    apply_filters            (ctx, s);

    copy_restart_frame_params(ctx, s);

    determine_bits(ctx, s);

    set_best_codebook(ctx, s);
}

static void process_major_frame(MLPEncodeContext *ctx, MLPSubstream *s)
{
    ctx->number_of_frames = ctx->major_number_of_frames;

    s->cur_restart_header = &s->restart_header;

    generate_2_noise_channels(ctx, s);
    rematrix_channels        (ctx, s);

    apply_filters(ctx, s);
}

/****************************************************************************/

static int mlp_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                            const AVFrame *frame, int *got_packet)
{
    MLPEncodeContext *ctx = avctx->priv_data;
    int bytes_written = 0;
    int channels = avctx->ch_layout.nb_channels;
    int restart_frame, ret;
    const uint8_t *data;

    if (!frame && !ctx->last_frames)
        ctx->last_frames = (ctx->afq.remaining_samples + avctx->frame_size - 1) / avctx->frame_size;

    if (!frame && !ctx->last_frames--)
        return 0;

    if ((ret = ff_alloc_packet(avctx, avpkt, 87500 * channels)) < 0)
        return ret;

    if (frame) {
        /* add current frame to queue */
        if ((ret = ff_af_queue_add(&ctx->afq, frame)) < 0)
            return ret;
    }

    data = frame ? frame->data[0] : NULL;

    ctx->frame_index = avctx->frame_num % ctx->cur_restart_interval;

    if (avctx->frame_num < ctx->cur_restart_interval) {
        if (data)
            goto input_and_return;
    }

    restart_frame = !ctx->frame_index;

    if (restart_frame) {
        avpkt->flags |= AV_PKT_FLAG_KEY;
        for (int n = 0; n < ctx->num_substreams; n++)
            set_major_params(ctx, &ctx->s[n]);

        if (ctx->min_restart_interval != ctx->cur_restart_interval)
            process_major_frame(ctx, &ctx->s[0]);
    }

    bytes_written = write_access_unit(ctx, avpkt->data, avpkt->size, restart_frame);

    ctx->output_timing += avctx->frame_size;
    ctx->input_timing  += avctx->frame_size;

input_and_return:

    if (frame) {
        ctx->shorten_by = avctx->frame_size - frame->nb_samples;
        ctx->next_major_frame_size += avctx->frame_size;
        ctx->next_major_number_of_frames++;
    }
    if (data)
        for (int n = 0; n < ctx->num_substreams; n++)
            input_data(ctx, &ctx->s[n], frame->extended_data, frame->nb_samples);

    restart_frame = (ctx->frame_index + 1) % ctx->min_restart_interval;

    if (!restart_frame) {
        for (unsigned int seq_index = 0; seq_index < ctx->restart_intervals; seq_index++) {
            unsigned int number_of_samples;

            ctx->number_of_frames = ctx->next_major_number_of_frames;
            ctx->number_of_subblocks = ctx->next_major_number_of_frames + 1;

            number_of_samples = avctx->frame_size * ctx->number_of_frames;

            for (int n = 0; n < ctx->num_substreams; n++) {
                MLPSubstream *s = &ctx->s[n];

                for (int i = 0; i < s->b[seq_index].seq_size; i++) {
                    clear_channel_params(s->b[i].channel_params, channels);
                    default_decoding_params(ctx, &s->b[i].decoding_params);
                }
            }

            if (number_of_samples > 0) {
                for (int n = 0; n < ctx->num_substreams; n++)
                    analyze_sample_buffer(ctx, &ctx->s[n]);
            }
        }

        if (ctx->frame_index == (ctx->cur_restart_interval - 1)) {
            ctx->major_frame_size = ctx->next_major_frame_size;
            ctx->next_major_frame_size = 0;
            ctx->major_number_of_frames = ctx->next_major_number_of_frames;
            ctx->next_major_number_of_frames = 0;
        }
    }

    if (!frame && ctx->last_frames < ctx->cur_restart_interval - 1)
        avctx->frame_num++;

    if (bytes_written > 0) {
        ff_af_queue_remove(&ctx->afq,
                           FFMIN(avctx->frame_size, ctx->afq.remaining_samples),
                           &avpkt->pts,
                           &avpkt->duration);

        av_shrink_packet(avpkt, bytes_written);

        *got_packet = 1;
    } else {
        *got_packet = 0;
    }

    return 0;
}

static av_cold int mlp_encode_close(AVCodecContext *avctx)
{
    MLPEncodeContext *ctx = avctx->priv_data;

    ff_lpc_end(&ctx->lpc_ctx);
    ff_af_queue_close(&ctx->afq);

    return 0;
}

#define FLAGS AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM
#define OFFSET(x) offsetof(MLPEncodeContext, x)
static const AVOption mlp_options[] = {
{ "max_interval", "Max number of frames between each new header", OFFSET(max_restart_interval),  AV_OPT_TYPE_INT, {.i64 = 16 }, MIN_HEADER_INTERVAL, MAX_HEADER_INTERVAL, FLAGS },
{ "lpc_coeff_precision", "LPC coefficient precision", OFFSET(lpc_coeff_precision), AV_OPT_TYPE_INT, {.i64 = 15 }, 0, 15, FLAGS },
{ "lpc_type", "LPC algorithm", OFFSET(lpc_type), AV_OPT_TYPE_INT, {.i64 = FF_LPC_TYPE_LEVINSON }, FF_LPC_TYPE_LEVINSON, FF_LPC_TYPE_CHOLESKY, FLAGS, .unit = "lpc_type" },
{ "levinson", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = FF_LPC_TYPE_LEVINSON }, 0, 0, FLAGS, .unit = "lpc_type" },
{ "cholesky", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = FF_LPC_TYPE_CHOLESKY }, 0, 0, FLAGS, .unit = "lpc_type" },
{ "lpc_passes", "Number of passes to use for Cholesky factorization during LPC analysis", OFFSET(lpc_passes),  AV_OPT_TYPE_INT, {.i64 = 2 }, 1, INT_MAX, FLAGS },
{ "codebook_search", "Max number of codebook searches", OFFSET(max_codebook_search),  AV_OPT_TYPE_INT, {.i64 = 3 }, 1, 100, FLAGS },
{ "prediction_order", "Search method for selecting prediction order", OFFSET(prediction_order), AV_OPT_TYPE_INT, {.i64 = ORDER_METHOD_EST }, ORDER_METHOD_EST, ORDER_METHOD_SEARCH, FLAGS, .unit = "predm" },
{ "estimation", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = ORDER_METHOD_EST },    0, 0, FLAGS, .unit = "predm" },
{ "search",     NULL, 0, AV_OPT_TYPE_CONST, {.i64 = ORDER_METHOD_SEARCH }, 0, 0, FLAGS, .unit = "predm" },
{ "rematrix_precision", "Rematrix coefficient precision", OFFSET(rematrix_precision), AV_OPT_TYPE_INT, {.i64 = 1 }, 0, 14, FLAGS },
{ NULL },
};

static const AVClass mlp_class = {
    .class_name = "mlpenc",
    .item_name  = av_default_item_name,
    .option     = mlp_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

#if CONFIG_MLP_ENCODER
const FFCodec ff_mlp_encoder = {
    .p.name                 ="mlp",
    CODEC_LONG_NAME("MLP (Meridian Lossless Packing)"),
    .p.type                 = AVMEDIA_TYPE_AUDIO,
    .p.id                   = AV_CODEC_ID_MLP,
    .p.capabilities         = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                              AV_CODEC_CAP_EXPERIMENTAL,
    .priv_data_size         = sizeof(MLPEncodeContext),
    .init                   = mlp_encode_init,
    FF_CODEC_ENCODE_CB(mlp_encode_frame),
    .close                  = mlp_encode_close,
    .p.priv_class           = &mlp_class,
    .p.sample_fmts          = (const enum AVSampleFormat[]) {AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_NONE},
    .p.supported_samplerates = (const int[]) {44100, 48000, 88200, 96000, 176400, 192000, 0},
    .p.ch_layouts           = ff_mlp_ch_layouts,
    .caps_internal          = FF_CODEC_CAP_INIT_CLEANUP,
};
#endif
#if CONFIG_TRUEHD_ENCODER
const FFCodec ff_truehd_encoder = {
    .p.name                 ="truehd",
    CODEC_LONG_NAME("TrueHD"),
    .p.type                 = AVMEDIA_TYPE_AUDIO,
    .p.id                   = AV_CODEC_ID_TRUEHD,
    .p.capabilities         = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                              AV_CODEC_CAP_SMALL_LAST_FRAME |
                              AV_CODEC_CAP_EXPERIMENTAL,
    .priv_data_size         = sizeof(MLPEncodeContext),
    .init                   = mlp_encode_init,
    FF_CODEC_ENCODE_CB(mlp_encode_frame),
    .close                  = mlp_encode_close,
    .p.priv_class           = &mlp_class,
    .p.sample_fmts          = (const enum AVSampleFormat[]) {AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_NONE},
    .p.supported_samplerates = (const int[]) {44100, 48000, 88200, 96000, 176400, 192000, 0},
    .p.ch_layouts           = (const AVChannelLayout[]) {
                                  AV_CHANNEL_LAYOUT_MONO,
                                  AV_CHANNEL_LAYOUT_STEREO,
                                  AV_CHANNEL_LAYOUT_2POINT1,
                                  AV_CHANNEL_LAYOUT_SURROUND,
                                  AV_CHANNEL_LAYOUT_3POINT1,
                                  AV_CHANNEL_LAYOUT_4POINT0,
                                  AV_CHANNEL_LAYOUT_4POINT1,
                                  AV_CHANNEL_LAYOUT_5POINT0,
                                  AV_CHANNEL_LAYOUT_5POINT1,
                                  { 0 }
                              },
    .caps_internal          = FF_CODEC_CAP_INIT_CLEANUP,
};
#endif
