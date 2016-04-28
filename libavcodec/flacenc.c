/*
 * FLAC audio encoder
 * Copyright (c) 2006  Justin Ruggles <justin.ruggles@gmail.com>
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

#include "libavutil/avassert.h"
#include "libavutil/crc.h"
#include "libavutil/intmath.h"
#include "libavutil/md5.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "bswapdsp.h"
#include "put_bits.h"
#include "golomb.h"
#include "internal.h"
#include "lpc.h"
#include "flac.h"
#include "flacdata.h"
#include "flacdsp.h"

#define FLAC_SUBFRAME_CONSTANT  0
#define FLAC_SUBFRAME_VERBATIM  1
#define FLAC_SUBFRAME_FIXED     8
#define FLAC_SUBFRAME_LPC      32

#define MAX_FIXED_ORDER     4
#define MAX_PARTITION_ORDER 8
#define MAX_PARTITIONS     (1 << MAX_PARTITION_ORDER)
#define MAX_LPC_PRECISION  15
#define MAX_LPC_SHIFT      15

enum CodingMode {
    CODING_MODE_RICE  = 4,
    CODING_MODE_RICE2 = 5,
};

typedef struct CompressionOptions {
    int compression_level;
    int block_time_ms;
    enum FFLPCType lpc_type;
    int lpc_passes;
    int lpc_coeff_precision;
    int min_prediction_order;
    int max_prediction_order;
    int prediction_order_method;
    int min_partition_order;
    int max_partition_order;
    int ch_mode;
    int exact_rice_parameters;
    int multi_dim_quant;
} CompressionOptions;

typedef struct RiceContext {
    enum CodingMode coding_mode;
    int porder;
    int params[MAX_PARTITIONS];
} RiceContext;

typedef struct FlacSubframe {
    int type;
    int type_code;
    int obits;
    int wasted;
    int order;
    int32_t coefs[MAX_LPC_ORDER];
    int shift;

    RiceContext rc;
    uint32_t rc_udata[FLAC_MAX_BLOCKSIZE];
    uint64_t rc_sums[32][MAX_PARTITIONS];

    int32_t samples[FLAC_MAX_BLOCKSIZE];
    int32_t residual[FLAC_MAX_BLOCKSIZE+11];
} FlacSubframe;

typedef struct FlacFrame {
    FlacSubframe subframes[FLAC_MAX_CHANNELS];
    int blocksize;
    int bs_code[2];
    uint8_t crc8;
    int ch_mode;
    int verbatim_only;
} FlacFrame;

typedef struct FlacEncodeContext {
    AVClass *class;
    PutBitContext pb;
    int channels;
    int samplerate;
    int sr_code[2];
    int bps_code;
    int max_blocksize;
    int min_framesize;
    int max_framesize;
    int max_encoded_framesize;
    uint32_t frame_count;
    uint64_t sample_count;
    uint8_t md5sum[16];
    FlacFrame frame;
    CompressionOptions options;
    AVCodecContext *avctx;
    LPCContext lpc_ctx;
    struct AVMD5 *md5ctx;
    uint8_t *md5_buffer;
    unsigned int md5_buffer_size;
    BswapDSPContext bdsp;
    FLACDSPContext flac_dsp;

    int flushed;
    int64_t next_pts;
} FlacEncodeContext;


/**
 * Write streaminfo metadata block to byte array.
 */
static void write_streaminfo(FlacEncodeContext *s, uint8_t *header)
{
    PutBitContext pb;

    memset(header, 0, FLAC_STREAMINFO_SIZE);
    init_put_bits(&pb, header, FLAC_STREAMINFO_SIZE);

    /* streaminfo metadata block */
    put_bits(&pb, 16, s->max_blocksize);
    put_bits(&pb, 16, s->max_blocksize);
    put_bits(&pb, 24, s->min_framesize);
    put_bits(&pb, 24, s->max_framesize);
    put_bits(&pb, 20, s->samplerate);
    put_bits(&pb, 3, s->channels-1);
    put_bits(&pb,  5, s->avctx->bits_per_raw_sample - 1);
    /* write 36-bit sample count in 2 put_bits() calls */
    put_bits(&pb, 24, (s->sample_count & 0xFFFFFF000LL) >> 12);
    put_bits(&pb, 12,  s->sample_count & 0x000000FFFLL);
    flush_put_bits(&pb);
    memcpy(&header[18], s->md5sum, 16);
}


/**
 * Set blocksize based on samplerate.
 * Choose the closest predefined blocksize >= BLOCK_TIME_MS milliseconds.
 */
static int select_blocksize(int samplerate, int block_time_ms)
{
    int i;
    int target;
    int blocksize;

    av_assert0(samplerate > 0);
    blocksize = ff_flac_blocksize_table[1];
    target    = (samplerate * block_time_ms) / 1000;
    for (i = 0; i < 16; i++) {
        if (target >= ff_flac_blocksize_table[i] &&
            ff_flac_blocksize_table[i] > blocksize) {
            blocksize = ff_flac_blocksize_table[i];
        }
    }
    return blocksize;
}


static av_cold void dprint_compression_options(FlacEncodeContext *s)
{
    AVCodecContext     *avctx = s->avctx;
    CompressionOptions *opt   = &s->options;

    av_log(avctx, AV_LOG_DEBUG, " compression: %d\n", opt->compression_level);

    switch (opt->lpc_type) {
    case FF_LPC_TYPE_NONE:
        av_log(avctx, AV_LOG_DEBUG, " lpc type: None\n");
        break;
    case FF_LPC_TYPE_FIXED:
        av_log(avctx, AV_LOG_DEBUG, " lpc type: Fixed pre-defined coefficients\n");
        break;
    case FF_LPC_TYPE_LEVINSON:
        av_log(avctx, AV_LOG_DEBUG, " lpc type: Levinson-Durbin recursion with Welch window\n");
        break;
    case FF_LPC_TYPE_CHOLESKY:
        av_log(avctx, AV_LOG_DEBUG, " lpc type: Cholesky factorization, %d pass%s\n",
               opt->lpc_passes, opt->lpc_passes == 1 ? "" : "es");
        break;
    }

    av_log(avctx, AV_LOG_DEBUG, " prediction order: %d, %d\n",
           opt->min_prediction_order, opt->max_prediction_order);

    switch (opt->prediction_order_method) {
    case ORDER_METHOD_EST:
        av_log(avctx, AV_LOG_DEBUG, " order method: %s\n", "estimate");
        break;
    case ORDER_METHOD_2LEVEL:
        av_log(avctx, AV_LOG_DEBUG, " order method: %s\n", "2-level");
        break;
    case ORDER_METHOD_4LEVEL:
        av_log(avctx, AV_LOG_DEBUG, " order method: %s\n", "4-level");
        break;
    case ORDER_METHOD_8LEVEL:
        av_log(avctx, AV_LOG_DEBUG, " order method: %s\n", "8-level");
        break;
    case ORDER_METHOD_SEARCH:
        av_log(avctx, AV_LOG_DEBUG, " order method: %s\n", "full search");
        break;
    case ORDER_METHOD_LOG:
        av_log(avctx, AV_LOG_DEBUG, " order method: %s\n", "log search");
        break;
    }


    av_log(avctx, AV_LOG_DEBUG, " partition order: %d, %d\n",
           opt->min_partition_order, opt->max_partition_order);

    av_log(avctx, AV_LOG_DEBUG, " block size: %d\n", avctx->frame_size);

    av_log(avctx, AV_LOG_DEBUG, " lpc precision: %d\n",
           opt->lpc_coeff_precision);
}


static av_cold int flac_encode_init(AVCodecContext *avctx)
{
    int freq = avctx->sample_rate;
    int channels = avctx->channels;
    FlacEncodeContext *s = avctx->priv_data;
    int i, level, ret;
    uint8_t *streaminfo;

    s->avctx = avctx;

    switch (avctx->sample_fmt) {
    case AV_SAMPLE_FMT_S16:
        avctx->bits_per_raw_sample = 16;
        s->bps_code                = 4;
        break;
    case AV_SAMPLE_FMT_S32:
        if (avctx->bits_per_raw_sample != 24)
            av_log(avctx, AV_LOG_WARNING, "encoding as 24 bits-per-sample\n");
        avctx->bits_per_raw_sample = 24;
        s->bps_code                = 6;
        break;
    }

    if (channels < 1 || channels > FLAC_MAX_CHANNELS) {
        av_log(avctx, AV_LOG_ERROR, "%d channels not supported (max %d)\n",
               channels, FLAC_MAX_CHANNELS);
        return AVERROR(EINVAL);
    }
    s->channels = channels;

    /* find samplerate in table */
    if (freq < 1)
        return -1;
    for (i = 4; i < 12; i++) {
        if (freq == ff_flac_sample_rate_table[i]) {
            s->samplerate = ff_flac_sample_rate_table[i];
            s->sr_code[0] = i;
            s->sr_code[1] = 0;
            break;
        }
    }
    /* if not in table, samplerate is non-standard */
    if (i == 12) {
        if (freq % 1000 == 0 && freq < 255000) {
            s->sr_code[0] = 12;
            s->sr_code[1] = freq / 1000;
        } else if (freq % 10 == 0 && freq < 655350) {
            s->sr_code[0] = 14;
            s->sr_code[1] = freq / 10;
        } else if (freq < 65535) {
            s->sr_code[0] = 13;
            s->sr_code[1] = freq;
        } else {
            av_log(avctx, AV_LOG_ERROR, "%d Hz not supported\n", freq);
            return AVERROR(EINVAL);
        }
        s->samplerate = freq;
    }

    /* set compression option defaults based on avctx->compression_level */
    if (avctx->compression_level < 0)
        s->options.compression_level = 5;
    else
        s->options.compression_level = avctx->compression_level;

    level = s->options.compression_level;
    if (level > 12) {
        av_log(avctx, AV_LOG_ERROR, "invalid compression level: %d\n",
               s->options.compression_level);
        return AVERROR(EINVAL);
    }

    s->options.block_time_ms = ((int[]){ 27, 27, 27,105,105,105,105,105,105,105,105,105,105})[level];

    if (s->options.lpc_type == FF_LPC_TYPE_DEFAULT)
        s->options.lpc_type  = ((int[]){ FF_LPC_TYPE_FIXED,    FF_LPC_TYPE_FIXED,    FF_LPC_TYPE_FIXED,
                                         FF_LPC_TYPE_LEVINSON, FF_LPC_TYPE_LEVINSON, FF_LPC_TYPE_LEVINSON,
                                         FF_LPC_TYPE_LEVINSON, FF_LPC_TYPE_LEVINSON, FF_LPC_TYPE_LEVINSON,
                                         FF_LPC_TYPE_LEVINSON, FF_LPC_TYPE_LEVINSON, FF_LPC_TYPE_LEVINSON,
                                         FF_LPC_TYPE_LEVINSON})[level];

    if (s->options.min_prediction_order < 0)
        s->options.min_prediction_order = ((int[]){  2,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1})[level];
    if (s->options.max_prediction_order < 0)
        s->options.max_prediction_order = ((int[]){  3,  4,  4,  6,  8,  8,  8,  8, 12, 12, 12, 32, 32})[level];

    if (s->options.prediction_order_method < 0)
        s->options.prediction_order_method = ((int[]){ ORDER_METHOD_EST,    ORDER_METHOD_EST,    ORDER_METHOD_EST,
                                                       ORDER_METHOD_EST,    ORDER_METHOD_EST,    ORDER_METHOD_EST,
                                                       ORDER_METHOD_4LEVEL, ORDER_METHOD_LOG,    ORDER_METHOD_4LEVEL,
                                                       ORDER_METHOD_LOG,    ORDER_METHOD_SEARCH, ORDER_METHOD_LOG,
                                                       ORDER_METHOD_SEARCH})[level];

    if (s->options.min_partition_order > s->options.max_partition_order) {
        av_log(avctx, AV_LOG_ERROR, "invalid partition orders: min=%d max=%d\n",
               s->options.min_partition_order, s->options.max_partition_order);
        return AVERROR(EINVAL);
    }
    if (s->options.min_partition_order < 0)
        s->options.min_partition_order = ((int[]){  2,  2,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0})[level];
    if (s->options.max_partition_order < 0)
        s->options.max_partition_order = ((int[]){  2,  2,  3,  3,  3,  8,  8,  8,  8,  8,  8,  8,  8})[level];

#if FF_API_PRIVATE_OPT
FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->min_prediction_order >= 0) {
        if (s->options.lpc_type == FF_LPC_TYPE_FIXED) {
            if (avctx->min_prediction_order > MAX_FIXED_ORDER) {
                av_log(avctx, AV_LOG_WARNING,
                       "invalid min prediction order %d, clamped to %d\n",
                       avctx->min_prediction_order, MAX_FIXED_ORDER);
                avctx->min_prediction_order = MAX_FIXED_ORDER;
            }
        } else if (avctx->min_prediction_order < MIN_LPC_ORDER ||
                   avctx->min_prediction_order > MAX_LPC_ORDER) {
            av_log(avctx, AV_LOG_ERROR, "invalid min prediction order: %d\n",
                   avctx->min_prediction_order);
            return AVERROR(EINVAL);
        }
        s->options.min_prediction_order = avctx->min_prediction_order;
    }
    if (avctx->max_prediction_order >= 0) {
        if (s->options.lpc_type == FF_LPC_TYPE_FIXED) {
            if (avctx->max_prediction_order > MAX_FIXED_ORDER) {
                av_log(avctx, AV_LOG_WARNING,
                       "invalid max prediction order %d, clamped to %d\n",
                       avctx->max_prediction_order, MAX_FIXED_ORDER);
                avctx->max_prediction_order = MAX_FIXED_ORDER;
            }
        } else if (avctx->max_prediction_order < MIN_LPC_ORDER ||
                   avctx->max_prediction_order > MAX_LPC_ORDER) {
            av_log(avctx, AV_LOG_ERROR, "invalid max prediction order: %d\n",
                   avctx->max_prediction_order);
            return AVERROR(EINVAL);
        }
        s->options.max_prediction_order = avctx->max_prediction_order;
    }
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    if (s->options.lpc_type == FF_LPC_TYPE_NONE) {
        s->options.min_prediction_order = 0;
        s->options.max_prediction_order = 0;
    } else if (s->options.lpc_type == FF_LPC_TYPE_FIXED) {
        if (s->options.min_prediction_order > MAX_FIXED_ORDER) {
            av_log(avctx, AV_LOG_WARNING,
                   "invalid min prediction order %d, clamped to %d\n",
                   s->options.min_prediction_order, MAX_FIXED_ORDER);
            s->options.min_prediction_order = MAX_FIXED_ORDER;
        }
        if (s->options.max_prediction_order > MAX_FIXED_ORDER) {
            av_log(avctx, AV_LOG_WARNING,
                   "invalid max prediction order %d, clamped to %d\n",
                   s->options.max_prediction_order, MAX_FIXED_ORDER);
            s->options.max_prediction_order = MAX_FIXED_ORDER;
        }
    }

    if (s->options.max_prediction_order < s->options.min_prediction_order) {
        av_log(avctx, AV_LOG_ERROR, "invalid prediction orders: min=%d max=%d\n",
               s->options.min_prediction_order, s->options.max_prediction_order);
        return AVERROR(EINVAL);
    }

    if (avctx->frame_size > 0) {
        if (avctx->frame_size < FLAC_MIN_BLOCKSIZE ||
                avctx->frame_size > FLAC_MAX_BLOCKSIZE) {
            av_log(avctx, AV_LOG_ERROR, "invalid block size: %d\n",
                   avctx->frame_size);
            return AVERROR(EINVAL);
        }
    } else {
        s->avctx->frame_size = select_blocksize(s->samplerate, s->options.block_time_ms);
    }
    s->max_blocksize = s->avctx->frame_size;

    /* set maximum encoded frame size in verbatim mode */
    s->max_framesize = ff_flac_get_max_frame_size(s->avctx->frame_size,
                                                  s->channels,
                                                  s->avctx->bits_per_raw_sample);

    /* initialize MD5 context */
    s->md5ctx = av_md5_alloc();
    if (!s->md5ctx)
        return AVERROR(ENOMEM);
    av_md5_init(s->md5ctx);

    streaminfo = av_malloc(FLAC_STREAMINFO_SIZE);
    if (!streaminfo)
        return AVERROR(ENOMEM);
    write_streaminfo(s, streaminfo);
    avctx->extradata = streaminfo;
    avctx->extradata_size = FLAC_STREAMINFO_SIZE;

    s->frame_count   = 0;
    s->min_framesize = s->max_framesize;

    if (channels == 3 &&
            avctx->channel_layout != (AV_CH_LAYOUT_STEREO|AV_CH_FRONT_CENTER) ||
        channels == 4 &&
            avctx->channel_layout != AV_CH_LAYOUT_2_2 &&
            avctx->channel_layout != AV_CH_LAYOUT_QUAD ||
        channels == 5 &&
            avctx->channel_layout != AV_CH_LAYOUT_5POINT0 &&
            avctx->channel_layout != AV_CH_LAYOUT_5POINT0_BACK ||
        channels == 6 &&
            avctx->channel_layout != AV_CH_LAYOUT_5POINT1 &&
            avctx->channel_layout != AV_CH_LAYOUT_5POINT1_BACK) {
        if (avctx->channel_layout) {
            av_log(avctx, AV_LOG_ERROR, "Channel layout not supported by Flac, "
                                             "output stream will have incorrect "
                                             "channel layout.\n");
        } else {
            av_log(avctx, AV_LOG_WARNING, "No channel layout specified. The encoder "
                                               "will use Flac channel layout for "
                                               "%d channels.\n", channels);
        }
    }

    ret = ff_lpc_init(&s->lpc_ctx, avctx->frame_size,
                      s->options.max_prediction_order, FF_LPC_TYPE_LEVINSON);

    ff_bswapdsp_init(&s->bdsp);
    ff_flacdsp_init(&s->flac_dsp, avctx->sample_fmt, channels,
                    avctx->bits_per_raw_sample);

    dprint_compression_options(s);

    return ret;
}


static void init_frame(FlacEncodeContext *s, int nb_samples)
{
    int i, ch;
    FlacFrame *frame;

    frame = &s->frame;

    for (i = 0; i < 16; i++) {
        if (nb_samples == ff_flac_blocksize_table[i]) {
            frame->blocksize  = ff_flac_blocksize_table[i];
            frame->bs_code[0] = i;
            frame->bs_code[1] = 0;
            break;
        }
    }
    if (i == 16) {
        frame->blocksize = nb_samples;
        if (frame->blocksize <= 256) {
            frame->bs_code[0] = 6;
            frame->bs_code[1] = frame->blocksize-1;
        } else {
            frame->bs_code[0] = 7;
            frame->bs_code[1] = frame->blocksize-1;
        }
    }

    for (ch = 0; ch < s->channels; ch++) {
        FlacSubframe *sub = &frame->subframes[ch];

        sub->wasted = 0;
        sub->obits  = s->avctx->bits_per_raw_sample;

        if (sub->obits > 16)
            sub->rc.coding_mode = CODING_MODE_RICE2;
        else
            sub->rc.coding_mode = CODING_MODE_RICE;
    }

    frame->verbatim_only = 0;
}


/**
 * Copy channel-interleaved input samples into separate subframes.
 */
static void copy_samples(FlacEncodeContext *s, const void *samples)
{
    int i, j, ch;
    FlacFrame *frame;
    int shift = av_get_bytes_per_sample(s->avctx->sample_fmt) * 8 -
                s->avctx->bits_per_raw_sample;

#define COPY_SAMPLES(bits) do {                                     \
    const int ## bits ## _t *samples0 = samples;                    \
    frame = &s->frame;                                              \
    for (i = 0, j = 0; i < frame->blocksize; i++)                   \
        for (ch = 0; ch < s->channels; ch++, j++)                   \
            frame->subframes[ch].samples[i] = samples0[j] >> shift; \
} while (0)

    if (s->avctx->sample_fmt == AV_SAMPLE_FMT_S16)
        COPY_SAMPLES(16);
    else
        COPY_SAMPLES(32);
}


static uint64_t rice_count_exact(const int32_t *res, int n, int k)
{
    int i;
    uint64_t count = 0;

    for (i = 0; i < n; i++) {
        int32_t v = -2 * res[i] - 1;
        v ^= v >> 31;
        count += (v >> k) + 1 + k;
    }
    return count;
}


static uint64_t subframe_count_exact(FlacEncodeContext *s, FlacSubframe *sub,
                                     int pred_order)
{
    int p, porder, psize;
    int i, part_end;
    uint64_t count = 0;

    /* subframe header */
    count += 8;

    if (sub->wasted)
        count += sub->wasted;

    /* subframe */
    if (sub->type == FLAC_SUBFRAME_CONSTANT) {
        count += sub->obits;
    } else if (sub->type == FLAC_SUBFRAME_VERBATIM) {
        count += s->frame.blocksize * sub->obits;
    } else {
        /* warm-up samples */
        count += pred_order * sub->obits;

        /* LPC coefficients */
        if (sub->type == FLAC_SUBFRAME_LPC)
            count += 4 + 5 + pred_order * s->options.lpc_coeff_precision;

        /* rice-encoded block */
        count += 2;

        /* partition order */
        porder = sub->rc.porder;
        psize  = s->frame.blocksize >> porder;
        count += 4;

        /* residual */
        i        = pred_order;
        part_end = psize;
        for (p = 0; p < 1 << porder; p++) {
            int k = sub->rc.params[p];
            count += sub->rc.coding_mode;
            count += rice_count_exact(&sub->residual[i], part_end - i, k);
            i = part_end;
            part_end = FFMIN(s->frame.blocksize, part_end + psize);
        }
    }

    return count;
}


#define rice_encode_count(sum, n, k) (((n)*((k)+1))+((sum-(n>>1))>>(k)))

/**
 * Solve for d/dk(rice_encode_count) = n-((sum-(n>>1))>>(k+1)) = 0.
 */
static int find_optimal_param(uint64_t sum, int n, int max_param)
{
    int k;
    uint64_t sum2;

    if (sum <= n >> 1)
        return 0;
    sum2 = sum - (n >> 1);
    k    = av_log2(av_clipl_int32(sum2 / n));
    return FFMIN(k, max_param);
}

static int find_optimal_param_exact(uint64_t sums[32][MAX_PARTITIONS], int i, int max_param)
{
    int bestk = 0;
    int64_t bestbits = INT64_MAX;
    int k;

    for (k = 0; k <= max_param; k++) {
        int64_t bits = sums[k][i];
        if (bits < bestbits) {
            bestbits = bits;
            bestk = k;
        }
    }

    return bestk;
}

static uint64_t calc_optimal_rice_params(RiceContext *rc, int porder,
                                         uint64_t sums[32][MAX_PARTITIONS],
                                         int n, int pred_order, int max_param, int exact)
{
    int i;
    int k, cnt, part;
    uint64_t all_bits;

    part     = (1 << porder);
    all_bits = 4 * part;

    cnt = (n >> porder) - pred_order;
    for (i = 0; i < part; i++) {
        if (exact) {
            k = find_optimal_param_exact(sums, i, max_param);
            all_bits += sums[k][i];
        } else {
            k = find_optimal_param(sums[0][i], cnt, max_param);
            all_bits += rice_encode_count(sums[0][i], cnt, k);
        }
        rc->params[i] = k;
        cnt = n >> porder;
    }

    rc->porder = porder;

    return all_bits;
}


static void calc_sum_top(int pmax, int kmax, const uint32_t *data, int n, int pred_order,
                         uint64_t sums[32][MAX_PARTITIONS])
{
    int i, k;
    int parts;
    const uint32_t *res, *res_end;

    /* sums for highest level */
    parts   = (1 << pmax);

    for (k = 0; k <= kmax; k++) {
        res     = &data[pred_order];
        res_end = &data[n >> pmax];
        for (i = 0; i < parts; i++) {
            if (kmax) {
                uint64_t sum = (1LL + k) * (res_end - res);
                while (res < res_end)
                    sum += *(res++) >> k;
                sums[k][i] = sum;
            } else {
                uint64_t sum = 0;
                while (res < res_end)
                    sum += *(res++);
                sums[k][i] = sum;
            }
            res_end += n >> pmax;
        }
    }
}

static void calc_sum_next(int level, uint64_t sums[32][MAX_PARTITIONS], int kmax)
{
    int i, k;
    int parts = (1 << level);
    for (i = 0; i < parts; i++) {
        for (k=0; k<=kmax; k++)
            sums[k][i] = sums[k][2*i] + sums[k][2*i+1];
    }
}

static uint64_t calc_rice_params(RiceContext *rc,
                                 uint32_t udata[FLAC_MAX_BLOCKSIZE],
                                 uint64_t sums[32][MAX_PARTITIONS],
                                 int pmin, int pmax,
                                 const int32_t *data, int n, int pred_order, int exact)
{
    int i;
    uint64_t bits[MAX_PARTITION_ORDER+1];
    int opt_porder;
    RiceContext tmp_rc;
    int kmax = (1 << rc->coding_mode) - 2;

    av_assert1(pmin >= 0 && pmin <= MAX_PARTITION_ORDER);
    av_assert1(pmax >= 0 && pmax <= MAX_PARTITION_ORDER);
    av_assert1(pmin <= pmax);

    tmp_rc.coding_mode = rc->coding_mode;

    for (i = 0; i < n; i++)
        udata[i] = (2 * data[i]) ^ (data[i] >> 31);

    calc_sum_top(pmax, exact ? kmax : 0, udata, n, pred_order, sums);

    opt_porder = pmin;
    bits[pmin] = UINT32_MAX;
    for (i = pmax; ; ) {
        bits[i] = calc_optimal_rice_params(&tmp_rc, i, sums, n, pred_order, kmax, exact);
        if (bits[i] < bits[opt_porder] || pmax == pmin) {
            opt_porder = i;
            *rc = tmp_rc;
        }
        if (i == pmin)
            break;
        calc_sum_next(--i, sums, exact ? kmax : 0);
    }

    return bits[opt_porder];
}


static int get_max_p_order(int max_porder, int n, int order)
{
    int porder = FFMIN(max_porder, av_log2(n^(n-1)));
    if (order > 0)
        porder = FFMIN(porder, av_log2(n/order));
    return porder;
}


static uint64_t find_subframe_rice_params(FlacEncodeContext *s,
                                          FlacSubframe *sub, int pred_order)
{
    int pmin = get_max_p_order(s->options.min_partition_order,
                               s->frame.blocksize, pred_order);
    int pmax = get_max_p_order(s->options.max_partition_order,
                               s->frame.blocksize, pred_order);

    uint64_t bits = 8 + pred_order * sub->obits + 2 + sub->rc.coding_mode;
    if (sub->type == FLAC_SUBFRAME_LPC)
        bits += 4 + 5 + pred_order * s->options.lpc_coeff_precision;
    bits += calc_rice_params(&sub->rc, sub->rc_udata, sub->rc_sums, pmin, pmax, sub->residual,
                             s->frame.blocksize, pred_order, s->options.exact_rice_parameters);
    return bits;
}


static void encode_residual_fixed(int32_t *res, const int32_t *smp, int n,
                                  int order)
{
    int i;

    for (i = 0; i < order; i++)
        res[i] = smp[i];

    if (order == 0) {
        for (i = order; i < n; i++)
            res[i] = smp[i];
    } else if (order == 1) {
        for (i = order; i < n; i++)
            res[i] = smp[i] - smp[i-1];
    } else if (order == 2) {
        int a = smp[order-1] - smp[order-2];
        for (i = order; i < n; i += 2) {
            int b    = smp[i  ] - smp[i-1];
            res[i]   = b - a;
            a        = smp[i+1] - smp[i  ];
            res[i+1] = a - b;
        }
    } else if (order == 3) {
        int a = smp[order-1] -   smp[order-2];
        int c = smp[order-1] - 2*smp[order-2] + smp[order-3];
        for (i = order; i < n; i += 2) {
            int b    = smp[i  ] - smp[i-1];
            int d    = b - a;
            res[i]   = d - c;
            a        = smp[i+1] - smp[i  ];
            c        = a - b;
            res[i+1] = c - d;
        }
    } else {
        int a = smp[order-1] -   smp[order-2];
        int c = smp[order-1] - 2*smp[order-2] +   smp[order-3];
        int e = smp[order-1] - 3*smp[order-2] + 3*smp[order-3] - smp[order-4];
        for (i = order; i < n; i += 2) {
            int b    = smp[i  ] - smp[i-1];
            int d    = b - a;
            int f    = d - c;
            res[i  ] = f - e;
            a        = smp[i+1] - smp[i  ];
            c        = a - b;
            e        = c - d;
            res[i+1] = e - f;
        }
    }
}


static int encode_residual_ch(FlacEncodeContext *s, int ch)
{
    int i, n;
    int min_order, max_order, opt_order, omethod;
    FlacFrame *frame;
    FlacSubframe *sub;
    int32_t coefs[MAX_LPC_ORDER][MAX_LPC_ORDER];
    int shift[MAX_LPC_ORDER];
    int32_t *res, *smp;

    frame = &s->frame;
    sub   = &frame->subframes[ch];
    res   = sub->residual;
    smp   = sub->samples;
    n     = frame->blocksize;

    /* CONSTANT */
    for (i = 1; i < n; i++)
        if(smp[i] != smp[0])
            break;
    if (i == n) {
        sub->type = sub->type_code = FLAC_SUBFRAME_CONSTANT;
        res[0] = smp[0];
        return subframe_count_exact(s, sub, 0);
    }

    /* VERBATIM */
    if (frame->verbatim_only || n < 5) {
        sub->type = sub->type_code = FLAC_SUBFRAME_VERBATIM;
        memcpy(res, smp, n * sizeof(int32_t));
        return subframe_count_exact(s, sub, 0);
    }

    min_order  = s->options.min_prediction_order;
    max_order  = s->options.max_prediction_order;
    omethod    = s->options.prediction_order_method;

    /* FIXED */
    sub->type = FLAC_SUBFRAME_FIXED;
    if (s->options.lpc_type == FF_LPC_TYPE_NONE  ||
        s->options.lpc_type == FF_LPC_TYPE_FIXED || n <= max_order) {
        uint64_t bits[MAX_FIXED_ORDER+1];
        if (max_order > MAX_FIXED_ORDER)
            max_order = MAX_FIXED_ORDER;
        opt_order = 0;
        bits[0]   = UINT32_MAX;
        for (i = min_order; i <= max_order; i++) {
            encode_residual_fixed(res, smp, n, i);
            bits[i] = find_subframe_rice_params(s, sub, i);
            if (bits[i] < bits[opt_order])
                opt_order = i;
        }
        sub->order     = opt_order;
        sub->type_code = sub->type | sub->order;
        if (sub->order != max_order) {
            encode_residual_fixed(res, smp, n, sub->order);
            find_subframe_rice_params(s, sub, sub->order);
        }
        return subframe_count_exact(s, sub, sub->order);
    }

    /* LPC */
    sub->type = FLAC_SUBFRAME_LPC;
    opt_order = ff_lpc_calc_coefs(&s->lpc_ctx, smp, n, min_order, max_order,
                                  s->options.lpc_coeff_precision, coefs, shift, s->options.lpc_type,
                                  s->options.lpc_passes, omethod,
                                  MAX_LPC_SHIFT, 0);

    if (omethod == ORDER_METHOD_2LEVEL ||
        omethod == ORDER_METHOD_4LEVEL ||
        omethod == ORDER_METHOD_8LEVEL) {
        int levels = 1 << omethod;
        uint64_t bits[1 << ORDER_METHOD_8LEVEL];
        int order       = -1;
        int opt_index   = levels-1;
        opt_order       = max_order-1;
        bits[opt_index] = UINT32_MAX;
        for (i = levels-1; i >= 0; i--) {
            int last_order = order;
            order = min_order + (((max_order-min_order+1) * (i+1)) / levels)-1;
            order = av_clip(order, min_order - 1, max_order - 1);
            if (order == last_order)
                continue;
            if (s->bps_code * 4 + s->options.lpc_coeff_precision + av_log2(order) <= 32) {
                s->flac_dsp.lpc16_encode(res, smp, n, order+1, coefs[order],
                                         shift[order]);
            } else {
                s->flac_dsp.lpc32_encode(res, smp, n, order+1, coefs[order],
                                         shift[order]);
            }
            bits[i] = find_subframe_rice_params(s, sub, order+1);
            if (bits[i] < bits[opt_index]) {
                opt_index = i;
                opt_order = order;
            }
        }
        opt_order++;
    } else if (omethod == ORDER_METHOD_SEARCH) {
        // brute-force optimal order search
        uint64_t bits[MAX_LPC_ORDER];
        opt_order = 0;
        bits[0]   = UINT32_MAX;
        for (i = min_order-1; i < max_order; i++) {
            if (s->bps_code * 4 + s->options.lpc_coeff_precision + av_log2(i) <= 32) {
                s->flac_dsp.lpc16_encode(res, smp, n, i+1, coefs[i], shift[i]);
            } else {
                s->flac_dsp.lpc32_encode(res, smp, n, i+1, coefs[i], shift[i]);
            }
            bits[i] = find_subframe_rice_params(s, sub, i+1);
            if (bits[i] < bits[opt_order])
                opt_order = i;
        }
        opt_order++;
    } else if (omethod == ORDER_METHOD_LOG) {
        uint64_t bits[MAX_LPC_ORDER];
        int step;

        opt_order = min_order - 1 + (max_order-min_order)/3;
        memset(bits, -1, sizeof(bits));

        for (step = 16; step; step >>= 1) {
            int last = opt_order;
            for (i = last-step; i <= last+step; i += step) {
                if (i < min_order-1 || i >= max_order || bits[i] < UINT32_MAX)
                    continue;
                if (s->bps_code * 4 + s->options.lpc_coeff_precision + av_log2(i) <= 32) {
                    s->flac_dsp.lpc32_encode(res, smp, n, i+1, coefs[i], shift[i]);
                } else {
                    s->flac_dsp.lpc16_encode(res, smp, n, i+1, coefs[i], shift[i]);
                }
                bits[i] = find_subframe_rice_params(s, sub, i+1);
                if (bits[i] < bits[opt_order])
                    opt_order = i;
            }
        }
        opt_order++;
    }

    if (s->options.multi_dim_quant) {
        int allsteps = 1;
        int i, step, improved;
        int64_t best_score = INT64_MAX;
        int32_t qmax;

        qmax = (1 << (s->options.lpc_coeff_precision - 1)) - 1;

        for (i=0; i<opt_order; i++)
            allsteps *= 3;

        do {
            improved = 0;
            for (step = 0; step < allsteps; step++) {
                int tmp = step;
                int32_t lpc_try[MAX_LPC_ORDER];
                int64_t score = 0;
                int diffsum = 0;

                for (i=0; i<opt_order; i++) {
                    int diff = ((tmp + 1) % 3) - 1;
                    lpc_try[i] = av_clip(coefs[opt_order - 1][i] + diff, -qmax, qmax);
                    tmp /= 3;
                    diffsum += !!diff;
                }
                if (diffsum >8)
                    continue;

                if (s->bps_code * 4 + s->options.lpc_coeff_precision + av_log2(opt_order - 1) <= 32) {
                    s->flac_dsp.lpc16_encode(res, smp, n, opt_order, lpc_try, shift[opt_order-1]);
                } else {
                    s->flac_dsp.lpc32_encode(res, smp, n, opt_order, lpc_try, shift[opt_order-1]);
                }
                score = find_subframe_rice_params(s, sub, opt_order);
                if (score < best_score) {
                    best_score = score;
                    memcpy(coefs[opt_order-1], lpc_try, sizeof(*coefs));
                    improved=1;
                }
            }
        } while(improved);
    }

    sub->order     = opt_order;
    sub->type_code = sub->type | (sub->order-1);
    sub->shift     = shift[sub->order-1];
    for (i = 0; i < sub->order; i++)
        sub->coefs[i] = coefs[sub->order-1][i];

    if (s->bps_code * 4 + s->options.lpc_coeff_precision + av_log2(opt_order) <= 32) {
        s->flac_dsp.lpc16_encode(res, smp, n, sub->order, sub->coefs, sub->shift);
    } else {
        s->flac_dsp.lpc32_encode(res, smp, n, sub->order, sub->coefs, sub->shift);
    }

    find_subframe_rice_params(s, sub, sub->order);

    return subframe_count_exact(s, sub, sub->order);
}


static int count_frame_header(FlacEncodeContext *s)
{
    uint8_t av_unused tmp;
    int count;

    /*
    <14> Sync code
    <1>  Reserved
    <1>  Blocking strategy
    <4>  Block size in inter-channel samples
    <4>  Sample rate
    <4>  Channel assignment
    <3>  Sample size in bits
    <1>  Reserved
    */
    count = 32;

    /* coded frame number */
    PUT_UTF8(s->frame_count, tmp, count += 8;)

    /* explicit block size */
    if (s->frame.bs_code[0] == 6)
        count += 8;
    else if (s->frame.bs_code[0] == 7)
        count += 16;

    /* explicit sample rate */
    count += ((s->sr_code[0] == 12) + (s->sr_code[0] > 12) * 2) * 8;

    /* frame header CRC-8 */
    count += 8;

    return count;
}


static int encode_frame(FlacEncodeContext *s)
{
    int ch;
    uint64_t count;

    count = count_frame_header(s);

    for (ch = 0; ch < s->channels; ch++)
        count += encode_residual_ch(s, ch);

    count += (8 - (count & 7)) & 7; // byte alignment
    count += 16;                    // CRC-16

    count >>= 3;
    if (count > INT_MAX)
        return AVERROR_BUG;
    return count;
}


static void remove_wasted_bits(FlacEncodeContext *s)
{
    int ch, i;

    for (ch = 0; ch < s->channels; ch++) {
        FlacSubframe *sub = &s->frame.subframes[ch];
        int32_t v         = 0;

        for (i = 0; i < s->frame.blocksize; i++) {
            v |= sub->samples[i];
            if (v & 1)
                break;
        }

        if (v && !(v & 1)) {
            v = ff_ctz(v);

            for (i = 0; i < s->frame.blocksize; i++)
                sub->samples[i] >>= v;

            sub->wasted = v;
            sub->obits -= v;

            /* for 24-bit, check if removing wasted bits makes the range better
               suited for using RICE instead of RICE2 for entropy coding */
            if (sub->obits <= 17)
                sub->rc.coding_mode = CODING_MODE_RICE;
        }
    }
}


static int estimate_stereo_mode(const int32_t *left_ch, const int32_t *right_ch, int n,
                                int max_rice_param)
{
    int i, best;
    int32_t lt, rt;
    uint64_t sum[4];
    uint64_t score[4];
    int k;

    /* calculate sum of 2nd order residual for each channel */
    sum[0] = sum[1] = sum[2] = sum[3] = 0;
    for (i = 2; i < n; i++) {
        lt = left_ch[i]  - 2*left_ch[i-1]  + left_ch[i-2];
        rt = right_ch[i] - 2*right_ch[i-1] + right_ch[i-2];
        sum[2] += FFABS((lt + rt) >> 1);
        sum[3] += FFABS(lt - rt);
        sum[0] += FFABS(lt);
        sum[1] += FFABS(rt);
    }
    /* estimate bit counts */
    for (i = 0; i < 4; i++) {
        k      = find_optimal_param(2 * sum[i], n, max_rice_param);
        sum[i] = rice_encode_count( 2 * sum[i], n, k);
    }

    /* calculate score for each mode */
    score[0] = sum[0] + sum[1];
    score[1] = sum[0] + sum[3];
    score[2] = sum[1] + sum[3];
    score[3] = sum[2] + sum[3];

    /* return mode with lowest score */
    best = 0;
    for (i = 1; i < 4; i++)
        if (score[i] < score[best])
            best = i;

    return best;
}


/**
 * Perform stereo channel decorrelation.
 */
static void channel_decorrelation(FlacEncodeContext *s)
{
    FlacFrame *frame;
    int32_t *left, *right;
    int i, n;

    frame = &s->frame;
    n     = frame->blocksize;
    left  = frame->subframes[0].samples;
    right = frame->subframes[1].samples;

    if (s->channels != 2) {
        frame->ch_mode = FLAC_CHMODE_INDEPENDENT;
        return;
    }

    if (s->options.ch_mode < 0) {
        int max_rice_param = (1 << frame->subframes[0].rc.coding_mode) - 2;
        frame->ch_mode = estimate_stereo_mode(left, right, n, max_rice_param);
    } else
        frame->ch_mode = s->options.ch_mode;

    /* perform decorrelation and adjust bits-per-sample */
    if (frame->ch_mode == FLAC_CHMODE_INDEPENDENT)
        return;
    if (frame->ch_mode == FLAC_CHMODE_MID_SIDE) {
        int32_t tmp;
        for (i = 0; i < n; i++) {
            tmp      = left[i];
            left[i]  = (tmp + right[i]) >> 1;
            right[i] =  tmp - right[i];
        }
        frame->subframes[1].obits++;
    } else if (frame->ch_mode == FLAC_CHMODE_LEFT_SIDE) {
        for (i = 0; i < n; i++)
            right[i] = left[i] - right[i];
        frame->subframes[1].obits++;
    } else {
        for (i = 0; i < n; i++)
            left[i] -= right[i];
        frame->subframes[0].obits++;
    }
}


static void write_utf8(PutBitContext *pb, uint32_t val)
{
    uint8_t tmp;
    PUT_UTF8(val, tmp, put_bits(pb, 8, tmp);)
}


static void write_frame_header(FlacEncodeContext *s)
{
    FlacFrame *frame;
    int crc;

    frame = &s->frame;

    put_bits(&s->pb, 16, 0xFFF8);
    put_bits(&s->pb, 4, frame->bs_code[0]);
    put_bits(&s->pb, 4, s->sr_code[0]);

    if (frame->ch_mode == FLAC_CHMODE_INDEPENDENT)
        put_bits(&s->pb, 4, s->channels-1);
    else
        put_bits(&s->pb, 4, frame->ch_mode + FLAC_MAX_CHANNELS - 1);

    put_bits(&s->pb, 3, s->bps_code);
    put_bits(&s->pb, 1, 0);
    write_utf8(&s->pb, s->frame_count);

    if (frame->bs_code[0] == 6)
        put_bits(&s->pb, 8, frame->bs_code[1]);
    else if (frame->bs_code[0] == 7)
        put_bits(&s->pb, 16, frame->bs_code[1]);

    if (s->sr_code[0] == 12)
        put_bits(&s->pb, 8, s->sr_code[1]);
    else if (s->sr_code[0] > 12)
        put_bits(&s->pb, 16, s->sr_code[1]);

    flush_put_bits(&s->pb);
    crc = av_crc(av_crc_get_table(AV_CRC_8_ATM), 0, s->pb.buf,
                 put_bits_count(&s->pb) >> 3);
    put_bits(&s->pb, 8, crc);
}


static void write_subframes(FlacEncodeContext *s)
{
    int ch;

    for (ch = 0; ch < s->channels; ch++) {
        FlacSubframe *sub = &s->frame.subframes[ch];
        int i, p, porder, psize;
        int32_t *part_end;
        int32_t *res       =  sub->residual;
        int32_t *frame_end = &sub->residual[s->frame.blocksize];

        /* subframe header */
        put_bits(&s->pb, 1, 0);
        put_bits(&s->pb, 6, sub->type_code);
        put_bits(&s->pb, 1, !!sub->wasted);
        if (sub->wasted)
            put_bits(&s->pb, sub->wasted, 1);

        /* subframe */
        if (sub->type == FLAC_SUBFRAME_CONSTANT) {
            put_sbits(&s->pb, sub->obits, res[0]);
        } else if (sub->type == FLAC_SUBFRAME_VERBATIM) {
            while (res < frame_end)
                put_sbits(&s->pb, sub->obits, *res++);
        } else {
            /* warm-up samples */
            for (i = 0; i < sub->order; i++)
                put_sbits(&s->pb, sub->obits, *res++);

            /* LPC coefficients */
            if (sub->type == FLAC_SUBFRAME_LPC) {
                int cbits = s->options.lpc_coeff_precision;
                put_bits( &s->pb, 4, cbits-1);
                put_sbits(&s->pb, 5, sub->shift);
                for (i = 0; i < sub->order; i++)
                    put_sbits(&s->pb, cbits, sub->coefs[i]);
            }

            /* rice-encoded block */
            put_bits(&s->pb, 2, sub->rc.coding_mode - 4);

            /* partition order */
            porder  = sub->rc.porder;
            psize   = s->frame.blocksize >> porder;
            put_bits(&s->pb, 4, porder);

            /* residual */
            part_end  = &sub->residual[psize];
            for (p = 0; p < 1 << porder; p++) {
                int k = sub->rc.params[p];
                put_bits(&s->pb, sub->rc.coding_mode, k);
                while (res < part_end)
                    set_sr_golomb_flac(&s->pb, *res++, k, INT32_MAX, 0);
                part_end = FFMIN(frame_end, part_end + psize);
            }
        }
    }
}


static void write_frame_footer(FlacEncodeContext *s)
{
    int crc;
    flush_put_bits(&s->pb);
    crc = av_bswap16(av_crc(av_crc_get_table(AV_CRC_16_ANSI), 0, s->pb.buf,
                            put_bits_count(&s->pb)>>3));
    put_bits(&s->pb, 16, crc);
    flush_put_bits(&s->pb);
}


static int write_frame(FlacEncodeContext *s, AVPacket *avpkt)
{
    init_put_bits(&s->pb, avpkt->data, avpkt->size);
    write_frame_header(s);
    write_subframes(s);
    write_frame_footer(s);
    return put_bits_count(&s->pb) >> 3;
}


static int update_md5_sum(FlacEncodeContext *s, const void *samples)
{
    const uint8_t *buf;
    int buf_size = s->frame.blocksize * s->channels *
                   ((s->avctx->bits_per_raw_sample + 7) / 8);

    if (s->avctx->bits_per_raw_sample > 16 || HAVE_BIGENDIAN) {
        av_fast_malloc(&s->md5_buffer, &s->md5_buffer_size, buf_size);
        if (!s->md5_buffer)
            return AVERROR(ENOMEM);
    }

    if (s->avctx->bits_per_raw_sample <= 16) {
        buf = (const uint8_t *)samples;
#if HAVE_BIGENDIAN
        s->bdsp.bswap16_buf((uint16_t *) s->md5_buffer,
                            (const uint16_t *) samples, buf_size / 2);
        buf = s->md5_buffer;
#endif
    } else {
        int i;
        const int32_t *samples0 = samples;
        uint8_t *tmp            = s->md5_buffer;

        for (i = 0; i < s->frame.blocksize * s->channels; i++) {
            int32_t v = samples0[i] >> 8;
            AV_WL24(tmp + 3*i, v);
        }
        buf = s->md5_buffer;
    }
    av_md5_update(s->md5ctx, buf, buf_size);

    return 0;
}


static int flac_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                             const AVFrame *frame, int *got_packet_ptr)
{
    FlacEncodeContext *s;
    int frame_bytes, out_bytes, ret;

    s = avctx->priv_data;

    /* when the last block is reached, update the header in extradata */
    if (!frame) {
        s->max_framesize = s->max_encoded_framesize;
        av_md5_final(s->md5ctx, s->md5sum);
        write_streaminfo(s, avctx->extradata);

#if FF_API_SIDEDATA_ONLY_PKT
FF_DISABLE_DEPRECATION_WARNINGS
        if (avctx->side_data_only_packets && !s->flushed) {
FF_ENABLE_DEPRECATION_WARNINGS
#else
        if (!s->flushed) {
#endif
            uint8_t *side_data = av_packet_new_side_data(avpkt, AV_PKT_DATA_NEW_EXTRADATA,
                                                         avctx->extradata_size);
            if (!side_data)
                return AVERROR(ENOMEM);
            memcpy(side_data, avctx->extradata, avctx->extradata_size);

            avpkt->pts = s->next_pts;

            *got_packet_ptr = 1;
            s->flushed = 1;
        }

        return 0;
    }

    /* change max_framesize for small final frame */
    if (frame->nb_samples < s->frame.blocksize) {
        s->max_framesize = ff_flac_get_max_frame_size(frame->nb_samples,
                                                      s->channels,
                                                      avctx->bits_per_raw_sample);
    }

    init_frame(s, frame->nb_samples);

    copy_samples(s, frame->data[0]);

    channel_decorrelation(s);

    remove_wasted_bits(s);

    frame_bytes = encode_frame(s);

    /* Fall back on verbatim mode if the compressed frame is larger than it
       would be if encoded uncompressed. */
    if (frame_bytes < 0 || frame_bytes > s->max_framesize) {
        s->frame.verbatim_only = 1;
        frame_bytes = encode_frame(s);
        if (frame_bytes < 0) {
            av_log(avctx, AV_LOG_ERROR, "Bad frame count\n");
            return frame_bytes;
        }
    }

    if ((ret = ff_alloc_packet2(avctx, avpkt, frame_bytes, 0)) < 0)
        return ret;

    out_bytes = write_frame(s, avpkt);

    s->frame_count++;
    s->sample_count += frame->nb_samples;
    if ((ret = update_md5_sum(s, frame->data[0])) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error updating MD5 checksum\n");
        return ret;
    }
    if (out_bytes > s->max_encoded_framesize)
        s->max_encoded_framesize = out_bytes;
    if (out_bytes < s->min_framesize)
        s->min_framesize = out_bytes;

    avpkt->pts      = frame->pts;
    avpkt->duration = ff_samples_to_time_base(avctx, frame->nb_samples);
    avpkt->size     = out_bytes;

    s->next_pts = avpkt->pts + avpkt->duration;

    *got_packet_ptr = 1;
    return 0;
}


static av_cold int flac_encode_close(AVCodecContext *avctx)
{
    if (avctx->priv_data) {
        FlacEncodeContext *s = avctx->priv_data;
        av_freep(&s->md5ctx);
        av_freep(&s->md5_buffer);
        ff_lpc_end(&s->lpc_ctx);
    }
    av_freep(&avctx->extradata);
    avctx->extradata_size = 0;
    return 0;
}

#define FLAGS AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM
static const AVOption options[] = {
{ "lpc_coeff_precision", "LPC coefficient precision", offsetof(FlacEncodeContext, options.lpc_coeff_precision), AV_OPT_TYPE_INT, {.i64 = 15 }, 0, MAX_LPC_PRECISION, FLAGS },
{ "lpc_type", "LPC algorithm", offsetof(FlacEncodeContext, options.lpc_type), AV_OPT_TYPE_INT, {.i64 = FF_LPC_TYPE_DEFAULT }, FF_LPC_TYPE_DEFAULT, FF_LPC_TYPE_NB-1, FLAGS, "lpc_type" },
{ "none",     NULL, 0, AV_OPT_TYPE_CONST, {.i64 = FF_LPC_TYPE_NONE },     INT_MIN, INT_MAX, FLAGS, "lpc_type" },
{ "fixed",    NULL, 0, AV_OPT_TYPE_CONST, {.i64 = FF_LPC_TYPE_FIXED },    INT_MIN, INT_MAX, FLAGS, "lpc_type" },
{ "levinson", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = FF_LPC_TYPE_LEVINSON }, INT_MIN, INT_MAX, FLAGS, "lpc_type" },
{ "cholesky", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = FF_LPC_TYPE_CHOLESKY }, INT_MIN, INT_MAX, FLAGS, "lpc_type" },
{ "lpc_passes", "Number of passes to use for Cholesky factorization during LPC analysis", offsetof(FlacEncodeContext, options.lpc_passes),  AV_OPT_TYPE_INT, {.i64 = 2 }, 1, INT_MAX, FLAGS },
{ "min_partition_order",  NULL, offsetof(FlacEncodeContext, options.min_partition_order),  AV_OPT_TYPE_INT, {.i64 = -1 },      -1, MAX_PARTITION_ORDER, FLAGS },
{ "max_partition_order",  NULL, offsetof(FlacEncodeContext, options.max_partition_order),  AV_OPT_TYPE_INT, {.i64 = -1 },      -1, MAX_PARTITION_ORDER, FLAGS },
{ "prediction_order_method", "Search method for selecting prediction order", offsetof(FlacEncodeContext, options.prediction_order_method), AV_OPT_TYPE_INT, {.i64 = -1 }, -1, ORDER_METHOD_LOG, FLAGS, "predm" },
{ "estimation", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = ORDER_METHOD_EST },    INT_MIN, INT_MAX, FLAGS, "predm" },
{ "2level",     NULL, 0, AV_OPT_TYPE_CONST, {.i64 = ORDER_METHOD_2LEVEL }, INT_MIN, INT_MAX, FLAGS, "predm" },
{ "4level",     NULL, 0, AV_OPT_TYPE_CONST, {.i64 = ORDER_METHOD_4LEVEL }, INT_MIN, INT_MAX, FLAGS, "predm" },
{ "8level",     NULL, 0, AV_OPT_TYPE_CONST, {.i64 = ORDER_METHOD_8LEVEL }, INT_MIN, INT_MAX, FLAGS, "predm" },
{ "search",     NULL, 0, AV_OPT_TYPE_CONST, {.i64 = ORDER_METHOD_SEARCH }, INT_MIN, INT_MAX, FLAGS, "predm" },
{ "log",        NULL, 0, AV_OPT_TYPE_CONST, {.i64 = ORDER_METHOD_LOG },    INT_MIN, INT_MAX, FLAGS, "predm" },
{ "ch_mode", "Stereo decorrelation mode", offsetof(FlacEncodeContext, options.ch_mode), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, FLAC_CHMODE_MID_SIDE, FLAGS, "ch_mode" },
{ "auto",       NULL, 0, AV_OPT_TYPE_CONST, { .i64 = -1                      }, INT_MIN, INT_MAX, FLAGS, "ch_mode" },
{ "indep",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FLAC_CHMODE_INDEPENDENT }, INT_MIN, INT_MAX, FLAGS, "ch_mode" },
{ "left_side",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FLAC_CHMODE_LEFT_SIDE   }, INT_MIN, INT_MAX, FLAGS, "ch_mode" },
{ "right_side", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FLAC_CHMODE_RIGHT_SIDE  }, INT_MIN, INT_MAX, FLAGS, "ch_mode" },
{ "mid_side",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FLAC_CHMODE_MID_SIDE    }, INT_MIN, INT_MAX, FLAGS, "ch_mode" },
{ "exact_rice_parameters", "Calculate rice parameters exactly", offsetof(FlacEncodeContext, options.exact_rice_parameters), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
{ "multi_dim_quant",       "Multi-dimensional quantization",    offsetof(FlacEncodeContext, options.multi_dim_quant),       AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
{ "min_prediction_order", NULL, offsetof(FlacEncodeContext, options.min_prediction_order), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, MAX_LPC_ORDER, FLAGS },
{ "max_prediction_order", NULL, offsetof(FlacEncodeContext, options.max_prediction_order), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, MAX_LPC_ORDER, FLAGS },

{ NULL },
};

static const AVClass flac_encoder_class = {
    .class_name = "FLAC encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_flac_encoder = {
    .name           = "flac",
    .long_name      = NULL_IF_CONFIG_SMALL("FLAC (Free Lossless Audio Codec)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_FLAC,
    .priv_data_size = sizeof(FlacEncodeContext),
    .init           = flac_encode_init,
    .encode2        = flac_encode_frame,
    .close          = flac_encode_close,
    .capabilities   = AV_CODEC_CAP_SMALL_LAST_FRAME | AV_CODEC_CAP_DELAY | AV_CODEC_CAP_LOSSLESS,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_S32,
                                                     AV_SAMPLE_FMT_NONE },
    .priv_class     = &flac_encoder_class,
};
