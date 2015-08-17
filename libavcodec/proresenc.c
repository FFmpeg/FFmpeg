/*
 * Copyright (c) 2011 Michael Jackson
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * version 2 of the License.
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

// define DEBUG

typedef short DCTELEM;

#include "avcodec.h"
#include "put_bits.h"
#include "fdctdsp.h"
#include "pixblockdsp.h"
#include "simple_idct.h"
#include "bytestream.h"
#include "libavutil/opt.h"
#include "libavutil/x86_cpu.h"
#include "internal.h"
#include "mathops.h"

typedef struct {
    uint8_t *buf;
    unsigned buf_size;
    unsigned mb_x;
    unsigned mb_y;
    unsigned mb_count;
    int data_size;
    unsigned h;
    unsigned last_mb_w;
    uint8_t *edge_buf;
    int edge_stride;
    unsigned qp;
    int over_qp;
    int loaded;
    DECLARE_ALIGNED(16, DCTELEM, blocks)[8*12*64];
} SliceContext;

typedef struct {
    const AVClass *class;
    AVFrame coded_frame;
    const AVFrame *frame;
    FDCTDSPContext fdsp;
    PixblockDSPContext pdsp;
    int frame_type;              ///< 0 = progressive, 1 = tff, 2 = bff
    SliceContext *slices;
    int slice_count;             ///< number of slices in the current picture
    unsigned width, height;
    unsigned mb_width;           ///< width of the current picture in mb
    unsigned mb_height;          ///< height of the current picture in mb
    unsigned mb_count;
    uint8_t progressive_scan[64];
    uint8_t interlaced_scan[64];
    int16_t qmat_luma[225][64];
    int16_t qmat_chroma[225][64];
    uint8_t qmat[2][64];         ///< quantization matrix
    const uint8_t *scan;
    int first_field;
    uint8_t *buf;
    unsigned qp;
    uint64_t bitrate;
    int frame_size;
    int picture_size;
    int left_size;
    float bt;
    char *profile;
    unsigned mb_size;
    int qmax;
    unsigned rc_qp;
    int quant_bias;
} ProresEncContext;

#define QMAT_SHIFT 16
#define QUANT_BIAS_SHIFT 8

static const uint8_t progressive_scan[64] = {
     0,  1,  8,  9,  2,  3, 10, 11,
    16, 17, 24, 25, 18, 19, 26, 27,
     4,  5, 12, 20, 13,  6,  7, 14,
    21, 28, 29, 22, 15, 23, 30, 31,
    32, 33, 40, 48, 41, 34, 35, 42,
    49, 56, 57, 50, 43, 36, 37, 44,
    51, 58, 59, 52, 45, 38, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

static const uint8_t interlaced_scan[64] = {
     0,  8,  1,  9, 16, 24, 17, 25,
     2, 10,  3, 11, 18, 26, 19, 27,
    32, 40, 33, 34, 41, 48, 56, 49,
    42, 35, 43, 50, 57, 58, 51, 59,
     4, 12,  5,  6, 13, 20, 28, 21,
    14,  7, 15, 22, 29, 36, 44, 37,
    30, 23, 31, 38, 45, 52, 60, 53,
    46, 39, 47, 54, 61, 62, 55, 63,
};

static const struct {
    const char *name;
    const char tag[4];
    const double ratio;
    const uint8_t qmat_luma[64];
    const uint8_t qmat_chroma[64];
} profiles[] = {
    { "proxy", "apco", 13/63.0,
      {
          4,  7,  9, 11, 13, 14, 15, 63,
          7,  7, 11, 12, 14, 15, 63, 63,
          9, 11, 13, 14, 15, 63, 63, 63,
         11, 11, 13, 14, 63, 63, 63, 63,
         11, 13, 14, 63, 63, 63, 63, 63,
         13, 14, 63, 63, 63, 63, 63, 63,
         13, 63, 63, 63, 63, 63, 63, 63,
         63, 63, 63, 63, 63, 63, 63, 63,
      },
      {
          4,  7,  9, 11, 13, 14, 63, 63,
          7,  7, 11, 12, 14, 63, 63, 63,
          9, 11, 13, 14, 63, 63, 63, 63,
         11, 11, 13, 14, 63, 63, 63, 63,
         11, 13, 14, 63, 63, 63, 63, 63,
         13, 14, 63, 63, 63, 63, 63, 63,
         13, 63, 63, 63, 63, 63, 63, 63,
         63, 63, 63, 63, 63, 63, 63, 63,
      }
    },
    { "lt", "apcs", 13/28.0,
      {
          4,  5,  6,  7,  9, 11, 13, 15,
          5,  5,  7,  8, 11, 13, 15, 17,
          6,  7,  9, 11, 13, 15, 15, 17,
          7,  7,  9, 11, 13, 15, 17, 19,
          7,  9, 11, 13, 14, 16, 19, 23,
          9, 11, 13, 14, 16, 19, 23, 29,
          9, 11, 13, 15, 17, 21, 28, 35,
         11, 13, 16, 17, 21, 28, 35, 41,
      },
      {
          4,  5,  6,  7,  9, 11, 13, 15,
          5,  5,  7,  8, 11, 13, 15, 17,
          6,  7,  9, 11, 13, 15, 15, 17,
          7,  7,  9, 11, 13, 15, 17, 19,
          7,  9, 11, 13, 14, 16, 19, 23,
          9, 11, 13, 14, 16, 19, 23, 29,
          9, 11, 13, 15, 17, 21, 28, 35,
         11, 13, 16, 17, 21, 28, 35, 41,
      }
    },
    { "std", "apcn", 2/3.0,
      {
          4,  4,  5,  5,  6,  7,  7,  9,
          4,  4,  5,  6,  7,  7,  9,  9,
          5,  5,  6,  7,  7,  9,  9, 10,
          5,  5,  6,  7,  7,  9,  9, 10,
          5,  6,  7,  7,  8,  9, 10, 12,
          6,  7,  7,  8,  9, 10, 12, 15,
          6,  7,  7,  9, 10, 11, 14, 17,
          7,  7,  9, 10, 11, 14, 17, 21,
      },
      {
          4,  4,  5,  5,  6,  7,  7,  9,
          4,  4,  5,  6,  7,  7,  9,  9,
          5,  5,  6,  7,  7,  9,  9, 10,
          5,  5,  6,  7,  7,  9,  9, 10,
          5,  6,  7,  7,  8,  9, 10, 12,
          6,  7,  7,  8,  9, 10, 12, 15,
          6,  7,  7,  9, 10, 11, 14, 17,
          7,  7,  9, 10, 11, 14, 17, 21,
      }
    },
    { "hq", "apch", 1.0,
      {
          4,  4,  4,  4,  4,  4,  4,  4,
          4,  4,  4,  4,  4,  4,  4,  4,
          4,  4,  4,  4,  4,  4,  4,  4,
          4,  4,  4,  4,  4,  4,  4,  5,
          4,  4,  4,  4,  4,  4,  5,  5,
          4,  4,  4,  4,  4,  5,  5,  6,
          4,  4,  4,  4,  5,  5,  6,  7,
          4,  4,  4,  4,  5,  6,  7,  7,
      },
      {
          4,  4,  4,  4,  4,  4,  4,  4,
          4,  4,  4,  4,  4,  4,  4,  4,
          4,  4,  4,  4,  4,  4,  4,  4,
          4,  4,  4,  4,  4,  4,  4,  5,
          4,  4,  4,  4,  4,  4,  5,  5,
          4,  4,  4,  4,  4,  5,  5,  6,
          4,  4,  4,  4,  5,  5,  6,  7,
          4,  4,  4,  4,  5,  6,  7,  7,
      }
    },
};

static int compute_slice_mb_width(int mb_width)
{
    int count = 0;
    int slice_mb_count = 8;
    int mb_x = mb_width & 7;
    for (count = 0; mb_x > 0; count++) {
        slice_mb_count >>= 1;
        mb_x -= slice_mb_count;
    }
    return (mb_width >> 3) + count;
}

static int prores_encode_init(AVCodecContext *avctx)
{
    ProresEncContext *ctx = avctx->priv_data;
    FDCTDSPContext fdsp;
    PixblockDSPContext pdsp;
    int interlaced = !!(avctx->flags & CODEC_FLAG_INTERLACED_DCT);
    int i, q, slice_mb_count, profile_id;
    int mb_x = 0, mb_y = 0;
    uint64_t frame_size;
    uint8_t *buf;

    if (avctx->pix_fmt != PIX_FMT_YUV422P10 &&
        avctx->pix_fmt != PIX_FMT_YUV444P10) {
        av_log(avctx, AV_LOG_ERROR, "pixel format incompatible with prores encoder\n");
        return -1;
    }

    if (ctx->profile) {
        profile_id = -1;
        for (i = 0; i < FF_ARRAY_ELEMS(profiles); i++) {
            if (!strcmp(ctx->profile, profiles[i].name)) {
                profile_id = i;
                break;
            }
        }
        if (profile_id == -1) {
            av_log(avctx, AV_LOG_ERROR, "unknown profile: %s\n", ctx->profile);
            return -1;
        }
        memcpy(ctx->qmat[0], profiles[profile_id].qmat_luma, 64);
        memcpy(ctx->qmat[1], profiles[profile_id].qmat_chroma, 64);
    } else {
        profile_id = 3; // HQ
        memset(ctx->qmat[0], 4, 64); // default matrix
        memset(ctx->qmat[1], 4, 64);
    }

    if (avctx->pix_fmt == PIX_FMT_YUV444P10)
        avctx->codec_tag = AV_RL32("ap4h");
    else
        avctx->codec_tag = AV_RL32(profiles[profile_id].tag);

    if (avctx->flags & CODEC_FLAG_QSCALE)
        ctx->qp = avctx->global_quality / FF_QP2LAMBDA;

    if (!ctx->qp && !ctx->bitrate) {
        uint64_t pixels = avctx->width * avctx->height;
        if (pixels > 2048*1152)
            frame_size = 9 * pixels / 16;
        else if (pixels > 1920*1080)
            frame_size = 1048576;
        else if (pixels > 1280*720)
            frame_size = 917504;
        else if (pixels > 720*576)
            frame_size = 458752;
        else if (pixels > 720*486)
            frame_size = 305834;
        else
            frame_size = 262144;
        frame_size = frame_size * profiles[profile_id].ratio;
        if (avctx->pix_fmt == PIX_FMT_YUV444P10)
            frame_size = frame_size * 3 / 2;
        ctx->bitrate = frame_size*8 / av_q2d(avctx->time_base);
    }

    avctx->bits_per_raw_sample = 10;

    memset(&fdsp,0, sizeof(fdsp));
    memset(&pdsp,0, sizeof(pdsp));
    ff_fdctdsp_init(&fdsp, avctx);
    ff_pixblockdsp_init(&pdsp, avctx);
    ctx->fdsp = fdsp;
    ctx->pdsp = pdsp;

    if (avctx->color_primaries == AVCOL_PRI_UNSPECIFIED &&
        avctx->color_trc == AVCOL_TRC_UNSPECIFIED) {
        if (avctx->height >= 720) {
            avctx->color_primaries = AVCOL_PRI_BT709;
        } else if (avctx->height >= 576) {
            avctx->color_primaries = AVCOL_PRI_BT470BG;
        } else if (avctx->height >= 480) {
            avctx->color_primaries = AVCOL_PRI_SMPTE170M;
        }
    }

    switch (avctx->color_primaries) {
    case AVCOL_PRI_BT709:
        avctx->color_trc = AVCOL_TRC_BT709;
        break;
    case AVCOL_PRI_SMPTE170M:
    case AVCOL_PRI_BT470BG:
        avctx->color_trc = AVCOL_TRC_BT709;
        break;
    }

    ctx->width = avctx->width;
    ctx->height = avctx->height;
    ctx->mb_width = (avctx->width+15) >> 4;

    if (interlaced) {
        ctx->scan = interlaced_scan;
        ctx->mb_height = (avctx->height+31) >> 5;
    } else {
        ctx->scan = progressive_scan;
        ctx->mb_height = (avctx->height+15) >> 4;
    }

    ctx->mb_count = ctx->mb_width * ctx->mb_height;
    ctx->slice_count = compute_slice_mb_width(ctx->mb_width) * ctx->mb_height;

    if (ctx->qp) {
        if (ctx->bitrate) {
            av_log(avctx, AV_LOG_ERROR, "error, choose either bitrate mode "
                   "or qscale mode\n");
            return -1;
        }
    } else {
        ctx->frame_size = ctx->bitrate * av_q2d(avctx->time_base) / 8;
        ctx->picture_size = ctx->frame_size / (interlaced+1);
        avctx->bit_rate = ctx->bitrate;
    }

    ctx->coded_frame.key_frame = 1;
    ctx->coded_frame.pict_type = AV_PICTURE_TYPE_I;
    avctx->coded_frame = &ctx->coded_frame;
    avctx->global_quality = ctx->qp*FF_QP2LAMBDA;

    ctx->slices = av_mallocz(ctx->slice_count * sizeof(*ctx->slices));
    if (!ctx->slices)
        return AVERROR(ENOMEM);

    ctx->buf = av_malloc(ctx->slice_count * (8 + 8 * 12 * 64 * 2));
    if (!ctx->buf)
        return AVERROR(ENOMEM);
    buf = ctx->buf;

    slice_mb_count = 8;

    for (i = 0; i < ctx->slice_count; i++) {
        SliceContext *slice = &ctx->slices[i];

        while (ctx->mb_width - mb_x < slice_mb_count)
            slice_mb_count >>= 1;

        slice->qp = ctx->qp;
        slice->mb_x = mb_x;
        slice->mb_y = mb_y;
        slice->mb_count = slice_mb_count;
        slice->buf = buf;
        slice->buf_size = 8 + slice_mb_count * 12 * 64 * 2;
        buf += slice->buf_size;

        if (mb_y+1 == ctx->mb_height)
            slice->h = (avctx->height >> interlaced) - mb_y*16;
        else
            slice->h = 16;

        mb_x += slice_mb_count;
        if (mb_x < ctx->mb_width) {
            slice->last_mb_w = 16;
        } else {
            slice->last_mb_w = avctx->width - 16*(mb_x-1);
            slice_mb_count = 8;
            mb_x = 0;
            mb_y++;
        }
    }

    if (mb_y != ctx->mb_height || mb_x) {
        av_log(avctx, AV_LOG_ERROR, "error slice count: %d != %d %d\n",
               mb_y, ctx->mb_height, mb_x);
        return -1;
    }

    ctx->quant_bias = 3<<(QUANT_BIAS_SHIFT-3); //(a + x*3/8)/x
    if (avctx->intra_quant_bias != FF_DEFAULT_QUANT_BIAS)
        ctx->quant_bias = avctx->intra_quant_bias;

    if (avctx->intra_matrix) {
        for (i = 0; i < 64; i++)
            ctx->qmat[0][i] = ctx->qmat[1][i] = avctx->intra_matrix[i];
    }

    for (q = 1; q <= 224; q++) {
        int qscale = q > 128 ? q - 96 << 2 : q;
        for (i = 0; i < 64; i++) {
            ctx->qmat_luma[q][i] = (1 << QMAT_SHIFT) / (qscale * ctx->qmat[0][i]);
            ctx->qmat_chroma[q][i] = (1 << QMAT_SHIFT) / (qscale * ctx->qmat[1][i]);
        }
    }

    ctx->rc_qp = 1;

    return 0;
}

static av_always_inline void encode_codeword(PutBitContext *pb, unsigned val, uint8_t codebook)
{
    unsigned switch_bits = codebook & 3;
    unsigned rice_order = codebook >> 5;

    if (val >> rice_order > switch_bits) {
        unsigned exp_order = (codebook >> 2) & 7;
        val += (1 << exp_order) - ((switch_bits + 1) << rice_order);
        put_bits(pb, ((av_log2(val)+1)<<1) - exp_order + switch_bits, val);
    } else if (rice_order) {
        put_bits(pb, (val >> rice_order)+1+rice_order,
                 (1 << rice_order) + (val & ((1<<rice_order)-1)));
    } else {
        put_bits(pb, val+1, 1);
    }
}

static av_always_inline int quantize(DCTELEM val, int qscale, int quant_bias)
{
    int bias = quant_bias << (QMAT_SHIFT - QUANT_BIAS_SHIFT);
    unsigned threshold1 = (1 << QMAT_SHIFT) - bias - 1;
    unsigned threshold2 = threshold1 << 1;
    int level = val * qscale;
    int ret;

    if (((unsigned)(level + threshold1)) > threshold2) {
        if (level < 0)
            ret = -((bias - level) >> QMAT_SHIFT);
        else
            ret = (bias + level) >> QMAT_SHIFT;
    } else {
        ret = 0;
    }

    return ret;
}

static const uint8_t dc_codebook[7] = { 0x04, 0x28, 0x28, 0x4D, 0x4D, 0x70, 0x70};

static void encode_dc_coeffs(AVCodecContext *avctx, PutBitContext *pb,
                             const int16_t *qmat, DCTELEM *blocks,
                             int blocks_per_slice)
{
    ProresEncContext *ctx = avctx->priv_data;
    DCTELEM prev_dc;
    int code, sign, level;
    int prev_sign, prev_code;
    int i;

    level = quantize(*blocks - 16384, qmat[0], ctx->quant_bias);
    prev_dc = level;
    MASK_ABS(sign, level);
    encode_codeword(pb, (level<<1) - (sign&1), 0xB8);

    blocks += 64;

    prev_code = 5;
    prev_sign = 0;

    for (i = 1; i < blocks_per_slice; i++, blocks += 64) {
        level = quantize(*blocks - 16384, qmat[0], ctx->quant_bias) - prev_dc;
        prev_dc += level;
        MASK_ABS(sign, level);
        if (!level)
            prev_sign = 0;
        code = (level<<1) + (prev_sign ^ sign);
        encode_codeword(pb, code, dc_codebook[FFMIN(prev_code, 6)]);
        prev_code = code;
        prev_sign = sign;
    }
}

// adaptive codebook switching lut according to previous run/level values
static const uint8_t run_to_cb[16] = { 0x06, 0x06, 0x05, 0x05, 0x04, 0x29, 0x29, 0x29, 0x29, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x4C };
static const uint8_t lev_to_cb[10] = { 0x04, 0x0A, 0x05, 0x06, 0x04, 0x28, 0x28, 0x28, 0x28, 0x4C };

static void encode_ac_coeffs(AVCodecContext *avctx, PutBitContext *pb,
                             const int16_t *qmat, DCTELEM *blocks,
                             int blocks_per_slice)
{
    ProresEncContext *ctx = avctx->priv_data;
    int block_mask, sign;
    unsigned pos, run;
    unsigned prev_run, prev_level;
    int level;
    int max_coeffs, i;
    int log2_block_count = av_log2(blocks_per_slice);
    int last_non_zero;

    prev_run   = 4;
    prev_level = 2;

    max_coeffs = 64 << log2_block_count;
    block_mask = blocks_per_slice - 1;

    pos = blocks_per_slice;
    last_non_zero = pos - 1;

    for (; pos < max_coeffs; pos++) {
        i = ctx->scan[pos >> log2_block_count];
        level = quantize(blocks[((pos & block_mask) << 6) + i], qmat[i], ctx->quant_bias);
        if (level) {
            run = pos - last_non_zero - 1;
            encode_codeword(pb, run, run_to_cb[FFMIN(prev_run,  15)]);
            prev_run = run;
            MASK_ABS(sign, level);
            encode_codeword(pb, level - 1, lev_to_cb[FFMIN(prev_level, 9)]);
            put_bits(pb, 1, sign&1);
            prev_level = level;
            last_non_zero = pos;
        }
    }
}

static av_always_inline void copy_edge(AVCodecContext *avctx, SliceContext *slice, int h_shift,
                                       const uint8_t *src, int src_stride)
{
    ProresEncContext *ctx = avctx->priv_data;
    uint8_t *dst;
    int i, w;

    if (!slice->edge_buf) {
        slice->edge_buf = av_malloc(16*32*8*2);
        slice->edge_stride = (16*8*2) << !!ctx->frame_type;
    }
    memset(slice->edge_buf, 0, 16*32*8*2);
    dst = slice->edge_buf;
    w = ((slice->mb_count-1)*16 + slice->last_mb_w) >> h_shift;
    for (i = 0; i < slice->h; i++) {
        memcpy(dst, src, w<<1);
        src += src_stride;
        dst += slice->edge_stride;
    }
}

static void read_slice_luma(AVCodecContext *avctx, SliceContext *slice,
                            DCTELEM *blocks,
                            const uint8_t *src, int src_stride)
{
    ProresEncContext *ctx = avctx->priv_data;
    DCTELEM *block = blocks;
    int i;

    if (slice->h < 16 || slice->last_mb_w < 16) {
        copy_edge(avctx, slice, 0, src, src_stride);
        src = slice->edge_buf;
        src_stride = slice->edge_stride;
    }

    for (i = 0; i < slice->mb_count; i++) {
        ctx->pdsp.get_pixels(block+(0<<6), src, src_stride);
        ctx->pdsp.get_pixels(block+(1<<6), src+16, src_stride);
        ctx->pdsp.get_pixels(block+(2<<6), src+8*src_stride, src_stride);
        ctx->pdsp.get_pixels(block+(3<<6), src+8*src_stride+16, src_stride);
        ctx->fdsp.fdct(block+(0<<6));
        ctx->fdsp.fdct(block+(1<<6));
        ctx->fdsp.fdct(block+(2<<6));
        ctx->fdsp.fdct(block+(3<<6));
        block += 4*64;
        src += 32;
    }
}

static void read_slice_chroma(AVCodecContext *avctx, SliceContext *slice,
                              DCTELEM *blocks,
                              const uint8_t *src, int src_stride,
                              int log2_blocks_per_mb)
{
    ProresEncContext *ctx = avctx->priv_data;
    DCTELEM *block = blocks;
    int i, j;

    if (slice->h < 16 || slice->last_mb_w < 16) {
        copy_edge(avctx, slice, log2_blocks_per_mb == 1, src, src_stride);
        src = slice->edge_buf;
        src_stride = slice->edge_stride;
    }

    for (i = 0; i < slice->mb_count; i++) {
        for (j = 0; j < log2_blocks_per_mb; j++) {
            ctx->pdsp.get_pixels(block+(0<<6), src, src_stride);
            ctx->pdsp.get_pixels(block+(1<<6), src+8*src_stride, src_stride);
            ctx->fdsp.fdct(block+(0<<6));
            ctx->fdsp.fdct(block+(1<<6));
            block += 2*64;
            src += 16;
        }
    }
}

static int encode_slice(AVCodecContext *avctx, SliceContext *slice,
                        DCTELEM *blocks, int log2_blocks_per_mb,
                        const int16_t *qmat, uint8_t *buf, int buf_size)
{
    int blocks_per_slice = slice->mb_count << log2_blocks_per_mb;
    PutBitContext pb;

    init_put_bits(&pb, buf, buf_size<<3);

    encode_dc_coeffs(avctx, &pb, qmat, blocks, blocks_per_slice);
    encode_ac_coeffs(avctx, &pb, qmat, blocks, blocks_per_slice);
    avpriv_align_put_bits(&pb);
    flush_put_bits(&pb);

    return put_bits_count(&pb)>>3;
}

static int encode_slice_thread(AVCodecContext *avctx, void *arg, int jobnr, int threadnr)
{
    ProresEncContext *ctx = avctx->priv_data;
    SliceContext *slice = &ctx->slices[jobnr];
    int y_data_size, u_data_size, v_data_size;
    const uint8_t *src_y, *src_u, *src_v;
    const AVFrame *pic = ctx->frame;
    int log2_chroma_blocks_per_mb;
    int luma_stride, chroma_stride;
    int mb_x_shift, buf_size;
    uint8_t *buf;

    if (avctx->pix_fmt == PIX_FMT_YUV444P10) {
        mb_x_shift = 5;
        log2_chroma_blocks_per_mb = 2;
    } else {
        mb_x_shift = 4;
        log2_chroma_blocks_per_mb = 1;
    }

    if (!slice->loaded) {
        if (ctx->frame_type == 0) {
            luma_stride   = pic->linesize[0];
            chroma_stride = pic->linesize[1];
        } else {
            luma_stride   = pic->linesize[0] << 1;
            chroma_stride = pic->linesize[1] << 1;
        }

        src_y = pic->data[0] + (slice->mb_y << 4) * luma_stride + (slice->mb_x << 5);
        src_u = pic->data[1] + (slice->mb_y << 4) * chroma_stride + (slice->mb_x << mb_x_shift);
        src_v = pic->data[2] + (slice->mb_y << 4) * chroma_stride + (slice->mb_x << mb_x_shift);

        if (ctx->frame_type && ctx->first_field ^ pic->top_field_first) {
            src_y += pic->linesize[0];
            src_u += pic->linesize[1];
            src_v += pic->linesize[2];
        }

        read_slice_luma(avctx, slice, slice->blocks, src_y, luma_stride);
        read_slice_chroma(avctx, slice, slice->blocks + 8*4*64, src_u, chroma_stride,
                          log2_chroma_blocks_per_mb);
        read_slice_chroma(avctx, slice, slice->blocks + 8*8*64, src_v, chroma_stride,
                          log2_chroma_blocks_per_mb);

        slice->loaded = 1;
    }

    buf = slice->buf;
    buf[0] = 8 << 3; // slice header size
    buf[1] = slice->qp;
    buf += 8;
    buf_size = slice->buf_size - 8;
    y_data_size = encode_slice(avctx, slice, slice->blocks, 2,
                               ctx->qmat_luma[slice->qp], buf, buf_size);
    AV_WB16(slice->buf + 2, y_data_size);
    buf += y_data_size;
    buf_size -= y_data_size;

    if (buf_size < 0)
        return -1;

    u_data_size = encode_slice(avctx, slice, slice->blocks + 8*4*64,
                               log2_chroma_blocks_per_mb,
                               ctx->qmat_chroma[slice->qp], buf, buf_size);
    AV_WB16(slice->buf + 4, u_data_size);
    buf += u_data_size;
    buf_size -= u_data_size;

    if (buf_size < 0)
        return -1;

    v_data_size = encode_slice(avctx, slice, slice->blocks + 8*8*64,
                               log2_chroma_blocks_per_mb,
                               ctx->qmat_chroma[slice->qp], buf, buf_size);
    AV_WB16(slice->buf + 6, v_data_size);
    buf += v_data_size;
    buf_size -= v_data_size;

    if (buf_size < 0)
        return -1;

    slice->data_size = 8 + y_data_size + u_data_size + v_data_size;

    return 0;
}

static int prores_find_qp(AVCodecContext *avctx)
{
    ProresEncContext *ctx = avctx->priv_data;
    int size = 0;
    int up_step = 1;
    int down_step = 1;
    int last_higher = 0;
    int last_lower = INT_MAX;
    int i, qp = ctx->rc_qp;

    for (;;) {
        for (i = 0; i < ctx->slice_count; i++) {
            SliceContext *slice = &ctx->slices[i];
            slice->qp = qp;
        }

        avctx->execute2(avctx, encode_slice_thread, NULL, NULL, ctx->slice_count);

        size = 0;
        for (i = 0; i < ctx->slice_count; i++) {
            SliceContext *slice = &ctx->slices[i];
            size += slice->data_size;
            if (size > ctx->picture_size)
                break;
        }

        //av_log(avctx, AV_LOG_INFO, "%d, qp %d, size %d, frame %d, higher %d, lower %d\n",
        //        avctx->frame_number, qp, size, ctx->picture_size, last_higher, last_lower);

        if (size < ctx->picture_size) {
            if (qp == 1 || last_higher == qp - 1)
                break;
            last_lower = FFMIN(qp, last_lower);
            if (last_higher != 0)
                qp = (qp+last_higher)>>1;
            else
                qp -= down_step++;
            if (qp < 1)
                qp = 1;
            up_step = 1;
        } else {
            if (last_lower == qp + 1)
                break;
            if (qp == ctx->qmax) {
                av_log(avctx, AV_LOG_WARNING, "warning, maximum quantizer reached\n");
                break;
            }
            last_higher = FFMAX(qp, last_higher);
            if (last_lower != INT_MAX)
                qp = (qp+last_lower)>>1;
            else
                qp += up_step++;
            down_step = 1;
        }
    }
    //av_log(avctx, AV_LOG_DEBUG, "frame %d size %d out qp %d\n", avctx->frame_number, size, qp);
    ctx->rc_qp = qp;
    return qp;
}

static int prores_encode_picture(AVCodecContext *avctx)
{
    ProresEncContext *ctx = avctx->priv_data;
    int i, threads_ret[65535];

    for (i = 0; i < ctx->slice_count; i++) {
        SliceContext *slice = &ctx->slices[i];
        slice->loaded = 0;
        threads_ret[i] = 0;
    }

    if (ctx->qp)
        avctx->execute2(avctx, encode_slice_thread, NULL, threads_ret, ctx->slice_count);
    else
        prores_find_qp(avctx);

    for (i = 0; i < ctx->slice_count; i++)
        if (threads_ret[i] < 0)
            return threads_ret[i];

    return 0;
}

static int prores_write_frame_header(AVCodecContext *avctx, uint8_t *buf)
{
    ProresEncContext *ctx = avctx->priv_data;
    int size = ctx->profile || avctx->intra_matrix ? 148 : 20;

    AV_WB32(buf, 0); // frame size, updated later
    memcpy(buf + 4, "icpf", 4);
    buf += 8;

    AV_WB16(buf, size); // header size
    AV_WB16(buf + 2, 1); // version
    memcpy(buf + 4, "ffm0", 4); // vendor
    AV_WB16(buf + 8,  avctx->width);
    AV_WB16(buf + 10, avctx->height);
    buf[12] = avctx->pix_fmt == PIX_FMT_YUV422P10 ? 0x80 : 0xC0;
    buf[12] |= ctx->frame_type << 2; // frame type
    buf[13] = 0; // unknown
    buf[14] = avctx->color_primaries; // color primaries
    buf[15] = avctx->color_trc; // color transfer
    buf[16] = 2; // color matrix UNSPECIFIED
    buf[17] = avctx->pix_fmt == PIX_FMT_YUV422P10 ? 2<<4 : 6<<4; // src pix fmt | alpha size
    buf[18] = 0; // unknown
    if (ctx->profile || avctx->intra_matrix) {
        buf[19] = 3; // flags
        memcpy(buf + 20, ctx->qmat[0], 64);
        memcpy(buf + 84, ctx->qmat[1], 64);
    } else {
        buf[19] = 0;
    }

    return size + 8;
}

static int prores_write_picture_header(AVCodecContext *avctx, uint8_t *buf)
{
    ProresEncContext *ctx = avctx->priv_data;

    buf[0] = 8 << 3; // header size
    AV_WB32(buf + 1, 0); // pic data size, updated laster
    AV_WB16(buf + 5, ctx->slice_count);
    buf[7] = (3 << 4) | 0; // log2 slice mb width | log2 slice mb height

    return 8;
}

static int prores_load_frame(AVCodecContext *avctx, const AVFrame *frame)
{
    ProresEncContext *ctx = avctx->priv_data;

    if (avctx->height != ctx->height || avctx->width != ctx->width) {
        av_log(avctx, AV_LOG_ERROR, "error, resolution changed\n");
        return -1;
    }

    ctx->coded_frame.interlaced_frame = frame->interlaced_frame;
    ctx->coded_frame.top_field_first = frame->top_field_first;
    ctx->frame = frame;

    if (avctx->flags & CODEC_FLAG_INTERLACED_DCT)
        ctx->frame_type = 1 + !frame->top_field_first;

    return 0;
}

static int prores_encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data)
{
    ProresEncContext *ctx = avctx->priv_data;
    uint8_t *slice_ptr, *pic_hdr_ptr, *p = buf;
    int i, ret, frame_size, qp;

    if (buf_size < avctx->height * avctx->width * 2 * 3) {
        av_log(avctx, AV_LOG_ERROR, "output buffer is too small to compress picture\n");
        return -1;
    }

    if (prores_load_frame(avctx, data) < 0)
        return -1;

    p += prores_write_frame_header(avctx, p);

    ctx->first_field = 1;

 encode_picture:
    ret = prores_encode_picture(avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "error encoding picture\n");
        return -1;
    }

    pic_hdr_ptr = p;
    p += prores_write_picture_header(avctx, p);

    slice_ptr = p;
    p += ctx->slice_count * 2;
    qp = 0;
    for (i = 0; i < ctx->slice_count; i++) {
        SliceContext *slice = &ctx->slices[i];
        bytestream_put_be16(&slice_ptr, slice->data_size);
        memcpy(p, slice->buf, slice->data_size);
        p += slice->data_size;
        qp += slice->qp;
    }

    AV_WB32(pic_hdr_ptr + 1, p - pic_hdr_ptr); // picture size

    if (ctx->frame_type && ctx->first_field) {
        ctx->first_field = 0;
        goto encode_picture;
    }

    frame_size = p - buf;
    AV_WB32(buf, frame_size); // frame size

    ctx->left_size += ctx->frame_size - frame_size;
    ctx->coded_frame.quality = qp*FF_QP2LAMBDA/ctx->slice_count;

    return frame_size;
}

static int prores_encode_frame2(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *pic, int *got_packet)
{
	int ret;
	uint8_t *buf;
	int frame_size = avctx->height * avctx->width * 2 * 3;

	if ((ret = ff_alloc_packet2(avctx, pkt, frame_size + FF_MIN_BUFFER_SIZE)) < 0)
		return ret;

	buf = pkt->data;
	
	ret = prores_encode_frame(avctx,buf,frame_size,(void*)pic);
	
	*got_packet = 0;
	if (ret < 0) {
		return ret;
	} else {
		if (ret > 0) {
			pkt->flags |= AV_PKT_FLAG_KEY;
			pkt->size = ret;
			*got_packet = 1;
		}
		return 0;
	}
}

static int prores_encode_end(AVCodecContext *avctx)
{
    ProresEncContext *ctx = avctx->priv_data;
    int i;

    for (i = 0; i < ctx->slice_count; i++) {
        SliceContext *slice = &ctx->slices[i];
        av_freep(&slice->edge_buf);
    }

    av_freep(&ctx->slices);
    av_freep(&ctx->buf);
    return 0;
}

#define OFFSET(x) offsetof(ProresEncContext,x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    {"cqp", "Set quantization parameter", OFFSET(qp), AV_OPT_TYPE_INT, {.i64=0}, 0, 224, VE},
    {"qmax", "Set maximum quantization parameter", OFFSET(qmax), AV_OPT_TYPE_INT, {.i64=224}, 1, 224, VE},
    {"b", "Set bit rate in (bits/s)", OFFSET(bitrate), AV_OPT_TYPE_INT64, {.i64=0}, 0, INT_MAX, VE},
    {"ratetol", "Set bit rate tolerance in %", OFFSET(bt), AV_OPT_TYPE_FLOAT, {.dbl=5}, 0, INT_MAX, VE},
    {"profile", "Set encoding profile: proxy,lt,std,hq", OFFSET(profile), AV_OPT_TYPE_STRING, {.str=0}, 0, CHAR_MAX, VE},
    { NULL }
};

static const AVClass class = { "prores", av_default_item_name, options, LIBAVUTIL_VERSION_INT };

AVCodec ff_prores_encoder = {
    .name           = "prores",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_PRORES,
    .priv_data_size = sizeof(ProresEncContext),
    .init           = prores_encode_init,
    .encode2         = prores_encode_frame2,
    .close          = prores_encode_end,
    .capabilities = CODEC_CAP_SLICE_THREADS,
    .pix_fmts = (const enum PixelFormat[]){PIX_FMT_YUV422P10, PIX_FMT_YUV444P10, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("ProRes"),
    .priv_class     = &class,
};
