/**
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

#include "libavutil/crc.h"
#include "libavutil/md5.h"
#include "avcodec.h"
#include "get_bits.h"
#include "dsputil.h"
#include "golomb.h"
#include "lpc.h"
#include "flac.h"
#include "flacdata.h"

#define FLAC_SUBFRAME_CONSTANT  0
#define FLAC_SUBFRAME_VERBATIM  1
#define FLAC_SUBFRAME_FIXED     8
#define FLAC_SUBFRAME_LPC      32

#define MAX_FIXED_ORDER     4
#define MAX_PARTITION_ORDER 8
#define MAX_PARTITIONS     (1 << MAX_PARTITION_ORDER)
#define MAX_LPC_PRECISION  15
#define MAX_LPC_SHIFT      15
#define MAX_RICE_PARAM     14

typedef struct CompressionOptions {
    int compression_level;
    int block_time_ms;
    int use_lpc;
    int lpc_coeff_precision;
    int min_prediction_order;
    int max_prediction_order;
    int prediction_order_method;
    int min_partition_order;
    int max_partition_order;
} CompressionOptions;

typedef struct RiceContext {
    int porder;
    int params[MAX_PARTITIONS];
} RiceContext;

typedef struct FlacSubframe {
    int type;
    int type_code;
    int obits;
    int order;
    int32_t coefs[MAX_LPC_ORDER];
    int shift;
    RiceContext rc;
    int32_t samples[FLAC_MAX_BLOCKSIZE];
    int32_t residual[FLAC_MAX_BLOCKSIZE+1];
} FlacSubframe;

typedef struct FlacFrame {
    FlacSubframe subframes[FLAC_MAX_CHANNELS];
    int blocksize;
    int bs_code[2];
    uint8_t crc8;
    int ch_mode;
} FlacFrame;

typedef struct FlacEncodeContext {
    PutBitContext pb;
    int channels;
    int samplerate;
    int sr_code[2];
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
    DSPContext dsp;
    struct AVMD5 *md5ctx;
} FlacEncodeContext;

/**
 * Writes streaminfo metadata block to byte array
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
    put_bits(&pb, 5, 15);       /* bits per sample - 1 */
    /* write 36-bit sample count in 2 put_bits() calls */
    put_bits(&pb, 24, (s->sample_count & 0xFFFFFF000LL) >> 12);
    put_bits(&pb, 12,  s->sample_count & 0x000000FFFLL);
    flush_put_bits(&pb);
    memcpy(&header[18], s->md5sum, 16);
}

/**
 * Sets blocksize based on samplerate
 * Chooses the closest predefined blocksize >= BLOCK_TIME_MS milliseconds
 */
static int select_blocksize(int samplerate, int block_time_ms)
{
    int i;
    int target;
    int blocksize;

    assert(samplerate > 0);
    blocksize = ff_flac_blocksize_table[1];
    target = (samplerate * block_time_ms) / 1000;
    for(i=0; i<16; i++) {
        if(target >= ff_flac_blocksize_table[i] && ff_flac_blocksize_table[i] > blocksize) {
            blocksize = ff_flac_blocksize_table[i];
        }
    }
    return blocksize;
}

static av_cold int flac_encode_init(AVCodecContext *avctx)
{
    int freq = avctx->sample_rate;
    int channels = avctx->channels;
    FlacEncodeContext *s = avctx->priv_data;
    int i, level;
    uint8_t *streaminfo;

    s->avctx = avctx;

    dsputil_init(&s->dsp, avctx);

    if(avctx->sample_fmt != SAMPLE_FMT_S16) {
        return -1;
    }

    if(channels < 1 || channels > FLAC_MAX_CHANNELS) {
        return -1;
    }
    s->channels = channels;

    /* find samplerate in table */
    if(freq < 1)
        return -1;
    for(i=4; i<12; i++) {
        if(freq == ff_flac_sample_rate_table[i]) {
            s->samplerate = ff_flac_sample_rate_table[i];
            s->sr_code[0] = i;
            s->sr_code[1] = 0;
            break;
        }
    }
    /* if not in table, samplerate is non-standard */
    if(i == 12) {
        if(freq % 1000 == 0 && freq < 255000) {
            s->sr_code[0] = 12;
            s->sr_code[1] = freq / 1000;
        } else if(freq % 10 == 0 && freq < 655350) {
            s->sr_code[0] = 14;
            s->sr_code[1] = freq / 10;
        } else if(freq < 65535) {
            s->sr_code[0] = 13;
            s->sr_code[1] = freq;
        } else {
            return -1;
        }
        s->samplerate = freq;
    }

    /* set compression option defaults based on avctx->compression_level */
    if(avctx->compression_level < 0) {
        s->options.compression_level = 5;
    } else {
        s->options.compression_level = avctx->compression_level;
    }
    av_log(avctx, AV_LOG_DEBUG, " compression: %d\n", s->options.compression_level);

    level= s->options.compression_level;
    if(level > 12) {
        av_log(avctx, AV_LOG_ERROR, "invalid compression level: %d\n",
               s->options.compression_level);
        return -1;
    }

    s->options.block_time_ms       = ((int[]){ 27, 27, 27,105,105,105,105,105,105,105,105,105,105})[level];
    s->options.use_lpc             = ((int[]){  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1})[level];
    s->options.min_prediction_order= ((int[]){  2,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1})[level];
    s->options.max_prediction_order= ((int[]){  3,  4,  4,  6,  8,  8,  8,  8, 12, 12, 12, 32, 32})[level];
    s->options.prediction_order_method = ((int[]){ ORDER_METHOD_EST,    ORDER_METHOD_EST,    ORDER_METHOD_EST,
                                                   ORDER_METHOD_EST,    ORDER_METHOD_EST,    ORDER_METHOD_EST,
                                                   ORDER_METHOD_4LEVEL, ORDER_METHOD_LOG,    ORDER_METHOD_4LEVEL,
                                                   ORDER_METHOD_LOG,    ORDER_METHOD_SEARCH, ORDER_METHOD_LOG,
                                                   ORDER_METHOD_SEARCH})[level];
    s->options.min_partition_order = ((int[]){  2,  2,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0})[level];
    s->options.max_partition_order = ((int[]){  2,  2,  3,  3,  3,  8,  8,  8,  8,  8,  8,  8,  8})[level];

    /* set compression option overrides from AVCodecContext */
    if(avctx->use_lpc >= 0) {
        s->options.use_lpc = av_clip(avctx->use_lpc, 0, 11);
    }
    if(s->options.use_lpc == 1)
        av_log(avctx, AV_LOG_DEBUG, " use lpc: Levinson-Durbin recursion with Welch window\n");
    else if(s->options.use_lpc > 1)
        av_log(avctx, AV_LOG_DEBUG, " use lpc: Cholesky factorization\n");

    if(avctx->min_prediction_order >= 0) {
        if(s->options.use_lpc) {
            if(avctx->min_prediction_order < MIN_LPC_ORDER ||
                    avctx->min_prediction_order > MAX_LPC_ORDER) {
                av_log(avctx, AV_LOG_ERROR, "invalid min prediction order: %d\n",
                       avctx->min_prediction_order);
                return -1;
            }
        } else {
            if(avctx->min_prediction_order > MAX_FIXED_ORDER) {
                av_log(avctx, AV_LOG_ERROR, "invalid min prediction order: %d\n",
                       avctx->min_prediction_order);
                return -1;
            }
        }
        s->options.min_prediction_order = avctx->min_prediction_order;
    }
    if(avctx->max_prediction_order >= 0) {
        if(s->options.use_lpc) {
            if(avctx->max_prediction_order < MIN_LPC_ORDER ||
                    avctx->max_prediction_order > MAX_LPC_ORDER) {
                av_log(avctx, AV_LOG_ERROR, "invalid max prediction order: %d\n",
                       avctx->max_prediction_order);
                return -1;
            }
        } else {
            if(avctx->max_prediction_order > MAX_FIXED_ORDER) {
                av_log(avctx, AV_LOG_ERROR, "invalid max prediction order: %d\n",
                       avctx->max_prediction_order);
                return -1;
            }
        }
        s->options.max_prediction_order = avctx->max_prediction_order;
    }
    if(s->options.max_prediction_order < s->options.min_prediction_order) {
        av_log(avctx, AV_LOG_ERROR, "invalid prediction orders: min=%d max=%d\n",
               s->options.min_prediction_order, s->options.max_prediction_order);
        return -1;
    }
    av_log(avctx, AV_LOG_DEBUG, " prediction order: %d, %d\n",
           s->options.min_prediction_order, s->options.max_prediction_order);

    if(avctx->prediction_order_method >= 0) {
        if(avctx->prediction_order_method > ORDER_METHOD_LOG) {
            av_log(avctx, AV_LOG_ERROR, "invalid prediction order method: %d\n",
                   avctx->prediction_order_method);
            return -1;
        }
        s->options.prediction_order_method = avctx->prediction_order_method;
    }
    switch(s->options.prediction_order_method) {
        case ORDER_METHOD_EST:    av_log(avctx, AV_LOG_DEBUG, " order method: %s\n",
                                         "estimate"); break;
        case ORDER_METHOD_2LEVEL: av_log(avctx, AV_LOG_DEBUG, " order method: %s\n",
                                         "2-level"); break;
        case ORDER_METHOD_4LEVEL: av_log(avctx, AV_LOG_DEBUG, " order method: %s\n",
                                         "4-level"); break;
        case ORDER_METHOD_8LEVEL: av_log(avctx, AV_LOG_DEBUG, " order method: %s\n",
                                         "8-level"); break;
        case ORDER_METHOD_SEARCH: av_log(avctx, AV_LOG_DEBUG, " order method: %s\n",
                                         "full search"); break;
        case ORDER_METHOD_LOG:    av_log(avctx, AV_LOG_DEBUG, " order method: %s\n",
                                         "log search"); break;
    }

    if(avctx->min_partition_order >= 0) {
        if(avctx->min_partition_order > MAX_PARTITION_ORDER) {
            av_log(avctx, AV_LOG_ERROR, "invalid min partition order: %d\n",
                   avctx->min_partition_order);
            return -1;
        }
        s->options.min_partition_order = avctx->min_partition_order;
    }
    if(avctx->max_partition_order >= 0) {
        if(avctx->max_partition_order > MAX_PARTITION_ORDER) {
            av_log(avctx, AV_LOG_ERROR, "invalid max partition order: %d\n",
                   avctx->max_partition_order);
            return -1;
        }
        s->options.max_partition_order = avctx->max_partition_order;
    }
    if(s->options.max_partition_order < s->options.min_partition_order) {
        av_log(avctx, AV_LOG_ERROR, "invalid partition orders: min=%d max=%d\n",
               s->options.min_partition_order, s->options.max_partition_order);
        return -1;
    }
    av_log(avctx, AV_LOG_DEBUG, " partition order: %d, %d\n",
           s->options.min_partition_order, s->options.max_partition_order);

    if(avctx->frame_size > 0) {
        if(avctx->frame_size < FLAC_MIN_BLOCKSIZE ||
                avctx->frame_size > FLAC_MAX_BLOCKSIZE) {
            av_log(avctx, AV_LOG_ERROR, "invalid block size: %d\n",
                   avctx->frame_size);
            return -1;
        }
    } else {
        s->avctx->frame_size = select_blocksize(s->samplerate, s->options.block_time_ms);
    }
    s->max_blocksize = s->avctx->frame_size;
    av_log(avctx, AV_LOG_DEBUG, " block size: %d\n", s->avctx->frame_size);

    /* set LPC precision */
    if(avctx->lpc_coeff_precision > 0) {
        if(avctx->lpc_coeff_precision > MAX_LPC_PRECISION) {
            av_log(avctx, AV_LOG_ERROR, "invalid lpc coeff precision: %d\n",
                   avctx->lpc_coeff_precision);
            return -1;
        }
        s->options.lpc_coeff_precision = avctx->lpc_coeff_precision;
    } else {
        /* default LPC precision */
        s->options.lpc_coeff_precision = 15;
    }
    av_log(avctx, AV_LOG_DEBUG, " lpc precision: %d\n",
           s->options.lpc_coeff_precision);

    /* set maximum encoded frame size in verbatim mode */
    s->max_framesize = ff_flac_get_max_frame_size(s->avctx->frame_size,
                                                  s->channels, 16);

    /* initialize MD5 context */
    s->md5ctx = av_malloc(av_md5_size);
    if(!s->md5ctx)
        return AVERROR(ENOMEM);
    av_md5_init(s->md5ctx);

    streaminfo = av_malloc(FLAC_STREAMINFO_SIZE);
    write_streaminfo(s, streaminfo);
    avctx->extradata = streaminfo;
    avctx->extradata_size = FLAC_STREAMINFO_SIZE;

    s->frame_count = 0;
    s->min_framesize = s->max_framesize;

    avctx->coded_frame = avcodec_alloc_frame();
    avctx->coded_frame->key_frame = 1;

    return 0;
}

static void init_frame(FlacEncodeContext *s)
{
    int i, ch;
    FlacFrame *frame;

    frame = &s->frame;

    for(i=0; i<16; i++) {
        if(s->avctx->frame_size == ff_flac_blocksize_table[i]) {
            frame->blocksize = ff_flac_blocksize_table[i];
            frame->bs_code[0] = i;
            frame->bs_code[1] = 0;
            break;
        }
    }
    if(i == 16) {
        frame->blocksize = s->avctx->frame_size;
        if(frame->blocksize <= 256) {
            frame->bs_code[0] = 6;
            frame->bs_code[1] = frame->blocksize-1;
        } else {
            frame->bs_code[0] = 7;
            frame->bs_code[1] = frame->blocksize-1;
        }
    }

    for(ch=0; ch<s->channels; ch++) {
        frame->subframes[ch].obits = 16;
    }
}

/**
 * Copy channel-interleaved input samples into separate subframes
 */
static void copy_samples(FlacEncodeContext *s, int16_t *samples)
{
    int i, j, ch;
    FlacFrame *frame;

    frame = &s->frame;
    for(i=0,j=0; i<frame->blocksize; i++) {
        for(ch=0; ch<s->channels; ch++,j++) {
            frame->subframes[ch].samples[i] = samples[j];
        }
    }
}


#define rice_encode_count(sum, n, k) (((n)*((k)+1))+((sum-(n>>1))>>(k)))

/**
 * Solve for d/dk(rice_encode_count) = n-((sum-(n>>1))>>(k+1)) = 0
 */
static int find_optimal_param(uint32_t sum, int n)
{
    int k;
    uint32_t sum2;

    if(sum <= n>>1)
        return 0;
    sum2 = sum-(n>>1);
    k = av_log2(n<256 ? FASTDIV(sum2,n) : sum2/n);
    return FFMIN(k, MAX_RICE_PARAM);
}

static uint32_t calc_optimal_rice_params(RiceContext *rc, int porder,
                                         uint32_t *sums, int n, int pred_order)
{
    int i;
    int k, cnt, part;
    uint32_t all_bits;

    part = (1 << porder);
    all_bits = 4 * part;

    cnt = (n >> porder) - pred_order;
    for(i=0; i<part; i++) {
        k = find_optimal_param(sums[i], cnt);
        rc->params[i] = k;
        all_bits += rice_encode_count(sums[i], cnt, k);
        cnt = n >> porder;
    }

    rc->porder = porder;

    return all_bits;
}

static void calc_sums(int pmin, int pmax, uint32_t *data, int n, int pred_order,
                      uint32_t sums[][MAX_PARTITIONS])
{
    int i, j;
    int parts;
    uint32_t *res, *res_end;

    /* sums for highest level */
    parts = (1 << pmax);
    res = &data[pred_order];
    res_end = &data[n >> pmax];
    for(i=0; i<parts; i++) {
        uint32_t sum = 0;
        while(res < res_end){
            sum += *(res++);
        }
        sums[pmax][i] = sum;
        res_end+= n >> pmax;
    }
    /* sums for lower levels */
    for(i=pmax-1; i>=pmin; i--) {
        parts = (1 << i);
        for(j=0; j<parts; j++) {
            sums[i][j] = sums[i+1][2*j] + sums[i+1][2*j+1];
        }
    }
}

static uint32_t calc_rice_params(RiceContext *rc, int pmin, int pmax,
                                 int32_t *data, int n, int pred_order)
{
    int i;
    uint32_t bits[MAX_PARTITION_ORDER+1];
    int opt_porder;
    RiceContext tmp_rc;
    uint32_t *udata;
    uint32_t sums[MAX_PARTITION_ORDER+1][MAX_PARTITIONS];

    assert(pmin >= 0 && pmin <= MAX_PARTITION_ORDER);
    assert(pmax >= 0 && pmax <= MAX_PARTITION_ORDER);
    assert(pmin <= pmax);

    udata = av_malloc(n * sizeof(uint32_t));
    for(i=0; i<n; i++) {
        udata[i] = (2*data[i]) ^ (data[i]>>31);
    }

    calc_sums(pmin, pmax, udata, n, pred_order, sums);

    opt_porder = pmin;
    bits[pmin] = UINT32_MAX;
    for(i=pmin; i<=pmax; i++) {
        bits[i] = calc_optimal_rice_params(&tmp_rc, i, sums[i], n, pred_order);
        if(bits[i] <= bits[opt_porder]) {
            opt_porder = i;
            *rc= tmp_rc;
        }
    }

    av_freep(&udata);
    return bits[opt_porder];
}

static int get_max_p_order(int max_porder, int n, int order)
{
    int porder = FFMIN(max_porder, av_log2(n^(n-1)));
    if(order > 0)
        porder = FFMIN(porder, av_log2(n/order));
    return porder;
}

static uint32_t calc_rice_params_fixed(RiceContext *rc, int pmin, int pmax,
                                       int32_t *data, int n, int pred_order,
                                       int bps)
{
    uint32_t bits;
    pmin = get_max_p_order(pmin, n, pred_order);
    pmax = get_max_p_order(pmax, n, pred_order);
    bits = pred_order*bps + 6;
    bits += calc_rice_params(rc, pmin, pmax, data, n, pred_order);
    return bits;
}

static uint32_t calc_rice_params_lpc(RiceContext *rc, int pmin, int pmax,
                                     int32_t *data, int n, int pred_order,
                                     int bps, int precision)
{
    uint32_t bits;
    pmin = get_max_p_order(pmin, n, pred_order);
    pmax = get_max_p_order(pmax, n, pred_order);
    bits = pred_order*bps + 4 + 5 + pred_order*precision + 6;
    bits += calc_rice_params(rc, pmin, pmax, data, n, pred_order);
    return bits;
}

static void encode_residual_verbatim(int32_t *res, int32_t *smp, int n)
{
    assert(n > 0);
    memcpy(res, smp, n * sizeof(int32_t));
}

static void encode_residual_fixed(int32_t *res, const int32_t *smp, int n,
                                  int order)
{
    int i;

    for(i=0; i<order; i++) {
        res[i] = smp[i];
    }

    if(order==0){
        for(i=order; i<n; i++)
            res[i]= smp[i];
    }else if(order==1){
        for(i=order; i<n; i++)
            res[i]= smp[i] - smp[i-1];
    }else if(order==2){
        int a = smp[order-1] - smp[order-2];
        for(i=order; i<n; i+=2) {
            int b = smp[i] - smp[i-1];
            res[i]= b - a;
            a = smp[i+1] - smp[i];
            res[i+1]= a - b;
        }
    }else if(order==3){
        int a = smp[order-1] - smp[order-2];
        int c = smp[order-1] - 2*smp[order-2] + smp[order-3];
        for(i=order; i<n; i+=2) {
            int b = smp[i] - smp[i-1];
            int d = b - a;
            res[i]= d - c;
            a = smp[i+1] - smp[i];
            c = a - b;
            res[i+1]= c - d;
        }
    }else{
        int a = smp[order-1] - smp[order-2];
        int c = smp[order-1] - 2*smp[order-2] + smp[order-3];
        int e = smp[order-1] - 3*smp[order-2] + 3*smp[order-3] - smp[order-4];
        for(i=order; i<n; i+=2) {
            int b = smp[i] - smp[i-1];
            int d = b - a;
            int f = d - c;
            res[i]= f - e;
            a = smp[i+1] - smp[i];
            c = a - b;
            e = c - d;
            res[i+1]= e - f;
        }
    }
}

#define LPC1(x) {\
    int c = coefs[(x)-1];\
    p0 += c*s;\
    s = smp[i-(x)+1];\
    p1 += c*s;\
}

static av_always_inline void encode_residual_lpc_unrolled(
    int32_t *res, const int32_t *smp, int n,
    int order, const int32_t *coefs, int shift, int big)
{
    int i;
    for(i=order; i<n; i+=2) {
        int s = smp[i-order];
        int p0 = 0, p1 = 0;
        if(big) {
            switch(order) {
                case 32: LPC1(32)
                case 31: LPC1(31)
                case 30: LPC1(30)
                case 29: LPC1(29)
                case 28: LPC1(28)
                case 27: LPC1(27)
                case 26: LPC1(26)
                case 25: LPC1(25)
                case 24: LPC1(24)
                case 23: LPC1(23)
                case 22: LPC1(22)
                case 21: LPC1(21)
                case 20: LPC1(20)
                case 19: LPC1(19)
                case 18: LPC1(18)
                case 17: LPC1(17)
                case 16: LPC1(16)
                case 15: LPC1(15)
                case 14: LPC1(14)
                case 13: LPC1(13)
                case 12: LPC1(12)
                case 11: LPC1(11)
                case 10: LPC1(10)
                case  9: LPC1( 9)
                         LPC1( 8)
                         LPC1( 7)
                         LPC1( 6)
                         LPC1( 5)
                         LPC1( 4)
                         LPC1( 3)
                         LPC1( 2)
                         LPC1( 1)
            }
        } else {
            switch(order) {
                case  8: LPC1( 8)
                case  7: LPC1( 7)
                case  6: LPC1( 6)
                case  5: LPC1( 5)
                case  4: LPC1( 4)
                case  3: LPC1( 3)
                case  2: LPC1( 2)
                case  1: LPC1( 1)
            }
        }
        res[i  ] = smp[i  ] - (p0 >> shift);
        res[i+1] = smp[i+1] - (p1 >> shift);
    }
}

static void encode_residual_lpc(int32_t *res, const int32_t *smp, int n,
                                int order, const int32_t *coefs, int shift)
{
    int i;
    for(i=0; i<order; i++) {
        res[i] = smp[i];
    }
#if CONFIG_SMALL
    for(i=order; i<n; i+=2) {
        int j;
        int s = smp[i];
        int p0 = 0, p1 = 0;
        for(j=0; j<order; j++) {
            int c = coefs[j];
            p1 += c*s;
            s = smp[i-j-1];
            p0 += c*s;
        }
        res[i  ] = smp[i  ] - (p0 >> shift);
        res[i+1] = smp[i+1] - (p1 >> shift);
    }
#else
    switch(order) {
        case  1: encode_residual_lpc_unrolled(res, smp, n, 1, coefs, shift, 0); break;
        case  2: encode_residual_lpc_unrolled(res, smp, n, 2, coefs, shift, 0); break;
        case  3: encode_residual_lpc_unrolled(res, smp, n, 3, coefs, shift, 0); break;
        case  4: encode_residual_lpc_unrolled(res, smp, n, 4, coefs, shift, 0); break;
        case  5: encode_residual_lpc_unrolled(res, smp, n, 5, coefs, shift, 0); break;
        case  6: encode_residual_lpc_unrolled(res, smp, n, 6, coefs, shift, 0); break;
        case  7: encode_residual_lpc_unrolled(res, smp, n, 7, coefs, shift, 0); break;
        case  8: encode_residual_lpc_unrolled(res, smp, n, 8, coefs, shift, 0); break;
        default: encode_residual_lpc_unrolled(res, smp, n, order, coefs, shift, 1); break;
    }
#endif
}

static int encode_residual(FlacEncodeContext *ctx, int ch)
{
    int i, n;
    int min_order, max_order, opt_order, precision, omethod;
    int min_porder, max_porder;
    FlacFrame *frame;
    FlacSubframe *sub;
    int32_t coefs[MAX_LPC_ORDER][MAX_LPC_ORDER];
    int shift[MAX_LPC_ORDER];
    int32_t *res, *smp;

    frame = &ctx->frame;
    sub = &frame->subframes[ch];
    res = sub->residual;
    smp = sub->samples;
    n = frame->blocksize;

    /* CONSTANT */
    for(i=1; i<n; i++) {
        if(smp[i] != smp[0]) break;
    }
    if(i == n) {
        sub->type = sub->type_code = FLAC_SUBFRAME_CONSTANT;
        res[0] = smp[0];
        return sub->obits;
    }

    /* VERBATIM */
    if(n < 5) {
        sub->type = sub->type_code = FLAC_SUBFRAME_VERBATIM;
        encode_residual_verbatim(res, smp, n);
        return sub->obits * n;
    }

    min_order = ctx->options.min_prediction_order;
    max_order = ctx->options.max_prediction_order;
    min_porder = ctx->options.min_partition_order;
    max_porder = ctx->options.max_partition_order;
    precision = ctx->options.lpc_coeff_precision;
    omethod = ctx->options.prediction_order_method;

    /* FIXED */
    if(!ctx->options.use_lpc || max_order == 0 || (n <= max_order)) {
        uint32_t bits[MAX_FIXED_ORDER+1];
        if(max_order > MAX_FIXED_ORDER) max_order = MAX_FIXED_ORDER;
        opt_order = 0;
        bits[0] = UINT32_MAX;
        for(i=min_order; i<=max_order; i++) {
            encode_residual_fixed(res, smp, n, i);
            bits[i] = calc_rice_params_fixed(&sub->rc, min_porder, max_porder, res,
                                             n, i, sub->obits);
            if(bits[i] < bits[opt_order]) {
                opt_order = i;
            }
        }
        sub->order = opt_order;
        sub->type = FLAC_SUBFRAME_FIXED;
        sub->type_code = sub->type | sub->order;
        if(sub->order != max_order) {
            encode_residual_fixed(res, smp, n, sub->order);
            return calc_rice_params_fixed(&sub->rc, min_porder, max_porder, res, n,
                                          sub->order, sub->obits);
        }
        return bits[sub->order];
    }

    /* LPC */
    opt_order = ff_lpc_calc_coefs(&ctx->dsp, smp, n, min_order, max_order,
                                  precision, coefs, shift, ctx->options.use_lpc,
                                  omethod, MAX_LPC_SHIFT, 0);

    if(omethod == ORDER_METHOD_2LEVEL ||
       omethod == ORDER_METHOD_4LEVEL ||
       omethod == ORDER_METHOD_8LEVEL) {
        int levels = 1 << omethod;
        uint32_t bits[levels];
        int order;
        int opt_index = levels-1;
        opt_order = max_order-1;
        bits[opt_index] = UINT32_MAX;
        for(i=levels-1; i>=0; i--) {
            order = min_order + (((max_order-min_order+1) * (i+1)) / levels)-1;
            if(order < 0) order = 0;
            encode_residual_lpc(res, smp, n, order+1, coefs[order], shift[order]);
            bits[i] = calc_rice_params_lpc(&sub->rc, min_porder, max_porder,
                                           res, n, order+1, sub->obits, precision);
            if(bits[i] < bits[opt_index]) {
                opt_index = i;
                opt_order = order;
            }
        }
        opt_order++;
    } else if(omethod == ORDER_METHOD_SEARCH) {
        // brute-force optimal order search
        uint32_t bits[MAX_LPC_ORDER];
        opt_order = 0;
        bits[0] = UINT32_MAX;
        for(i=min_order-1; i<max_order; i++) {
            encode_residual_lpc(res, smp, n, i+1, coefs[i], shift[i]);
            bits[i] = calc_rice_params_lpc(&sub->rc, min_porder, max_porder,
                                           res, n, i+1, sub->obits, precision);
            if(bits[i] < bits[opt_order]) {
                opt_order = i;
            }
        }
        opt_order++;
    } else if(omethod == ORDER_METHOD_LOG) {
        uint32_t bits[MAX_LPC_ORDER];
        int step;

        opt_order= min_order - 1 + (max_order-min_order)/3;
        memset(bits, -1, sizeof(bits));

        for(step=16 ;step; step>>=1){
            int last= opt_order;
            for(i=last-step; i<=last+step; i+= step){
                if(i<min_order-1 || i>=max_order || bits[i] < UINT32_MAX)
                    continue;
                encode_residual_lpc(res, smp, n, i+1, coefs[i], shift[i]);
                bits[i] = calc_rice_params_lpc(&sub->rc, min_porder, max_porder,
                                            res, n, i+1, sub->obits, precision);
                if(bits[i] < bits[opt_order])
                    opt_order= i;
            }
        }
        opt_order++;
    }

    sub->order = opt_order;
    sub->type = FLAC_SUBFRAME_LPC;
    sub->type_code = sub->type | (sub->order-1);
    sub->shift = shift[sub->order-1];
    for(i=0; i<sub->order; i++) {
        sub->coefs[i] = coefs[sub->order-1][i];
    }
    encode_residual_lpc(res, smp, n, sub->order, sub->coefs, sub->shift);
    return calc_rice_params_lpc(&sub->rc, min_porder, max_porder, res, n, sub->order,
                                sub->obits, precision);
}

static int encode_residual_v(FlacEncodeContext *ctx, int ch)
{
    int i, n;
    FlacFrame *frame;
    FlacSubframe *sub;
    int32_t *res, *smp;

    frame = &ctx->frame;
    sub = &frame->subframes[ch];
    res = sub->residual;
    smp = sub->samples;
    n = frame->blocksize;

    /* CONSTANT */
    for(i=1; i<n; i++) {
        if(smp[i] != smp[0]) break;
    }
    if(i == n) {
        sub->type = sub->type_code = FLAC_SUBFRAME_CONSTANT;
        res[0] = smp[0];
        return sub->obits;
    }

    /* VERBATIM */
    sub->type = sub->type_code = FLAC_SUBFRAME_VERBATIM;
    encode_residual_verbatim(res, smp, n);
    return sub->obits * n;
}

static int estimate_stereo_mode(int32_t *left_ch, int32_t *right_ch, int n)
{
    int i, best;
    int32_t lt, rt;
    uint64_t sum[4];
    uint64_t score[4];
    int k;

    /* calculate sum of 2nd order residual for each channel */
    sum[0] = sum[1] = sum[2] = sum[3] = 0;
    for(i=2; i<n; i++) {
        lt = left_ch[i] - 2*left_ch[i-1] + left_ch[i-2];
        rt = right_ch[i] - 2*right_ch[i-1] + right_ch[i-2];
        sum[2] += FFABS((lt + rt) >> 1);
        sum[3] += FFABS(lt - rt);
        sum[0] += FFABS(lt);
        sum[1] += FFABS(rt);
    }
    /* estimate bit counts */
    for(i=0; i<4; i++) {
        k = find_optimal_param(2*sum[i], n);
        sum[i] = rice_encode_count(2*sum[i], n, k);
    }

    /* calculate score for each mode */
    score[0] = sum[0] + sum[1];
    score[1] = sum[0] + sum[3];
    score[2] = sum[1] + sum[3];
    score[3] = sum[2] + sum[3];

    /* return mode with lowest score */
    best = 0;
    for(i=1; i<4; i++) {
        if(score[i] < score[best]) {
            best = i;
        }
    }
    if(best == 0) {
        return FLAC_CHMODE_INDEPENDENT;
    } else if(best == 1) {
        return FLAC_CHMODE_LEFT_SIDE;
    } else if(best == 2) {
        return FLAC_CHMODE_RIGHT_SIDE;
    } else {
        return FLAC_CHMODE_MID_SIDE;
    }
}

/**
 * Perform stereo channel decorrelation
 */
static void channel_decorrelation(FlacEncodeContext *ctx)
{
    FlacFrame *frame;
    int32_t *left, *right;
    int i, n;

    frame = &ctx->frame;
    n = frame->blocksize;
    left  = frame->subframes[0].samples;
    right = frame->subframes[1].samples;

    if(ctx->channels != 2) {
        frame->ch_mode = FLAC_CHMODE_INDEPENDENT;
        return;
    }

    frame->ch_mode = estimate_stereo_mode(left, right, n);

    /* perform decorrelation and adjust bits-per-sample */
    if(frame->ch_mode == FLAC_CHMODE_INDEPENDENT) {
        return;
    }
    if(frame->ch_mode == FLAC_CHMODE_MID_SIDE) {
        int32_t tmp;
        for(i=0; i<n; i++) {
            tmp = left[i];
            left[i] = (tmp + right[i]) >> 1;
            right[i] = tmp - right[i];
        }
        frame->subframes[1].obits++;
    } else if(frame->ch_mode == FLAC_CHMODE_LEFT_SIDE) {
        for(i=0; i<n; i++) {
            right[i] = left[i] - right[i];
        }
        frame->subframes[1].obits++;
    } else {
        for(i=0; i<n; i++) {
            left[i] -= right[i];
        }
        frame->subframes[0].obits++;
    }
}

static void write_utf8(PutBitContext *pb, uint32_t val)
{
    uint8_t tmp;
    PUT_UTF8(val, tmp, put_bits(pb, 8, tmp);)
}

static void output_frame_header(FlacEncodeContext *s)
{
    FlacFrame *frame;
    int crc;

    frame = &s->frame;

    put_bits(&s->pb, 16, 0xFFF8);
    put_bits(&s->pb, 4, frame->bs_code[0]);
    put_bits(&s->pb, 4, s->sr_code[0]);
    if(frame->ch_mode == FLAC_CHMODE_INDEPENDENT) {
        put_bits(&s->pb, 4, s->channels-1);
    } else {
        put_bits(&s->pb, 4, frame->ch_mode);
    }
    put_bits(&s->pb, 3, 4); /* bits-per-sample code */
    put_bits(&s->pb, 1, 0);
    write_utf8(&s->pb, s->frame_count);
    if(frame->bs_code[0] == 6) {
        put_bits(&s->pb, 8, frame->bs_code[1]);
    } else if(frame->bs_code[0] == 7) {
        put_bits(&s->pb, 16, frame->bs_code[1]);
    }
    if(s->sr_code[0] == 12) {
        put_bits(&s->pb, 8, s->sr_code[1]);
    } else if(s->sr_code[0] > 12) {
        put_bits(&s->pb, 16, s->sr_code[1]);
    }
    flush_put_bits(&s->pb);
    crc = av_crc(av_crc_get_table(AV_CRC_8_ATM), 0,
                 s->pb.buf, put_bits_count(&s->pb)>>3);
    put_bits(&s->pb, 8, crc);
}

static void output_subframe_constant(FlacEncodeContext *s, int ch)
{
    FlacSubframe *sub;
    int32_t res;

    sub = &s->frame.subframes[ch];
    res = sub->residual[0];
    put_sbits(&s->pb, sub->obits, res);
}

static void output_subframe_verbatim(FlacEncodeContext *s, int ch)
{
    int i;
    FlacFrame *frame;
    FlacSubframe *sub;
    int32_t res;

    frame = &s->frame;
    sub = &frame->subframes[ch];

    for(i=0; i<frame->blocksize; i++) {
        res = sub->residual[i];
        put_sbits(&s->pb, sub->obits, res);
    }
}

static void output_residual(FlacEncodeContext *ctx, int ch)
{
    int i, j, p, n, parts;
    int k, porder, psize, res_cnt;
    FlacFrame *frame;
    FlacSubframe *sub;
    int32_t *res;

    frame = &ctx->frame;
    sub = &frame->subframes[ch];
    res = sub->residual;
    n = frame->blocksize;

    /* rice-encoded block */
    put_bits(&ctx->pb, 2, 0);

    /* partition order */
    porder = sub->rc.porder;
    psize = n >> porder;
    parts = (1 << porder);
    put_bits(&ctx->pb, 4, porder);
    res_cnt = psize - sub->order;

    /* residual */
    j = sub->order;
    for(p=0; p<parts; p++) {
        k = sub->rc.params[p];
        put_bits(&ctx->pb, 4, k);
        if(p == 1) res_cnt = psize;
        for(i=0; i<res_cnt && j<n; i++, j++) {
            set_sr_golomb_flac(&ctx->pb, res[j], k, INT32_MAX, 0);
        }
    }
}

static void output_subframe_fixed(FlacEncodeContext *ctx, int ch)
{
    int i;
    FlacFrame *frame;
    FlacSubframe *sub;

    frame = &ctx->frame;
    sub = &frame->subframes[ch];

    /* warm-up samples */
    for(i=0; i<sub->order; i++) {
        put_sbits(&ctx->pb, sub->obits, sub->residual[i]);
    }

    /* residual */
    output_residual(ctx, ch);
}

static void output_subframe_lpc(FlacEncodeContext *ctx, int ch)
{
    int i, cbits;
    FlacFrame *frame;
    FlacSubframe *sub;

    frame = &ctx->frame;
    sub = &frame->subframes[ch];

    /* warm-up samples */
    for(i=0; i<sub->order; i++) {
        put_sbits(&ctx->pb, sub->obits, sub->residual[i]);
    }

    /* LPC coefficients */
    cbits = ctx->options.lpc_coeff_precision;
    put_bits(&ctx->pb, 4, cbits-1);
    put_sbits(&ctx->pb, 5, sub->shift);
    for(i=0; i<sub->order; i++) {
        put_sbits(&ctx->pb, cbits, sub->coefs[i]);
    }

    /* residual */
    output_residual(ctx, ch);
}

static void output_subframes(FlacEncodeContext *s)
{
    FlacFrame *frame;
    FlacSubframe *sub;
    int ch;

    frame = &s->frame;

    for(ch=0; ch<s->channels; ch++) {
        sub = &frame->subframes[ch];

        /* subframe header */
        put_bits(&s->pb, 1, 0);
        put_bits(&s->pb, 6, sub->type_code);
        put_bits(&s->pb, 1, 0); /* no wasted bits */

        /* subframe */
        if(sub->type == FLAC_SUBFRAME_CONSTANT) {
            output_subframe_constant(s, ch);
        } else if(sub->type == FLAC_SUBFRAME_VERBATIM) {
            output_subframe_verbatim(s, ch);
        } else if(sub->type == FLAC_SUBFRAME_FIXED) {
            output_subframe_fixed(s, ch);
        } else if(sub->type == FLAC_SUBFRAME_LPC) {
            output_subframe_lpc(s, ch);
        }
    }
}

static void output_frame_footer(FlacEncodeContext *s)
{
    int crc;
    flush_put_bits(&s->pb);
    crc = bswap_16(av_crc(av_crc_get_table(AV_CRC_16_ANSI), 0,
                          s->pb.buf, put_bits_count(&s->pb)>>3));
    put_bits(&s->pb, 16, crc);
    flush_put_bits(&s->pb);
}

static void update_md5_sum(FlacEncodeContext *s, int16_t *samples)
{
#if HAVE_BIGENDIAN
    int i;
    for(i = 0; i < s->frame.blocksize*s->channels; i++) {
        int16_t smp = le2me_16(samples[i]);
        av_md5_update(s->md5ctx, (uint8_t *)&smp, 2);
    }
#else
    av_md5_update(s->md5ctx, (uint8_t *)samples, s->frame.blocksize*s->channels*2);
#endif
}

static int flac_encode_frame(AVCodecContext *avctx, uint8_t *frame,
                             int buf_size, void *data)
{
    int ch;
    FlacEncodeContext *s;
    int16_t *samples = data;
    int out_bytes;
    int reencoded=0;

    s = avctx->priv_data;

    if(buf_size < s->max_framesize*2) {
        av_log(avctx, AV_LOG_ERROR, "output buffer too small\n");
        return 0;
    }

    /* when the last block is reached, update the header in extradata */
    if (!data) {
        s->max_framesize = s->max_encoded_framesize;
        av_md5_final(s->md5ctx, s->md5sum);
        write_streaminfo(s, avctx->extradata);
        return 0;
    }

    init_frame(s);

    copy_samples(s, samples);

    channel_decorrelation(s);

    for(ch=0; ch<s->channels; ch++) {
        encode_residual(s, ch);
    }

write_frame:
    init_put_bits(&s->pb, frame, buf_size);
    output_frame_header(s);
    output_subframes(s);
    output_frame_footer(s);
    out_bytes = put_bits_count(&s->pb) >> 3;

    if(out_bytes > s->max_framesize) {
        if(reencoded) {
            /* still too large. must be an error. */
            av_log(avctx, AV_LOG_ERROR, "error encoding frame\n");
            return -1;
        }

        /* frame too large. use verbatim mode */
        for(ch=0; ch<s->channels; ch++) {
            encode_residual_v(s, ch);
        }
        reencoded = 1;
        goto write_frame;
    }

    s->frame_count++;
    s->sample_count += avctx->frame_size;
    update_md5_sum(s, samples);
    if (out_bytes > s->max_encoded_framesize)
        s->max_encoded_framesize = out_bytes;
    if (out_bytes < s->min_framesize)
        s->min_framesize = out_bytes;

    return out_bytes;
}

static av_cold int flac_encode_close(AVCodecContext *avctx)
{
    if (avctx->priv_data) {
        FlacEncodeContext *s = avctx->priv_data;
        av_freep(&s->md5ctx);
    }
    av_freep(&avctx->extradata);
    avctx->extradata_size = 0;
    av_freep(&avctx->coded_frame);
    return 0;
}

AVCodec flac_encoder = {
    "flac",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_FLAC,
    sizeof(FlacEncodeContext),
    flac_encode_init,
    flac_encode_frame,
    flac_encode_close,
    NULL,
    .capabilities = CODEC_CAP_SMALL_LAST_FRAME | CODEC_CAP_DELAY,
    .sample_fmts = (const enum SampleFormat[]){SAMPLE_FMT_S16,SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("FLAC (Free Lossless Audio Codec)"),
};
