/*
 * Apple Intermediate Codec decoder
 *
 * Copyright (c) 2013 Konstantin Shishkov
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

#include <inttypes.h>

#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "get_bits.h"
#include "golomb.h"
#include "idctdsp.h"
#include "thread.h"
#include "unary.h"

#define AIC_HDR_SIZE    24
#define AIC_BAND_COEFFS (64 + 32 + 192 + 96)

enum AICBands {
    COEFF_LUMA = 0,
    COEFF_CHROMA,
    COEFF_LUMA_EXT,
    COEFF_CHROMA_EXT,
    NUM_BANDS
};

static const uint8_t aic_num_band_coeffs[NUM_BANDS] = { 64, 32, 192, 96 };

static const uint16_t aic_band_off[NUM_BANDS] = { 0, 64, 96, 288 };

static const uint8_t aic_quant_matrix[64] = {
     8, 16, 19, 22, 22, 26, 26, 27,
    16, 16, 22, 22, 26, 27, 27, 29,
    19, 22, 26, 26, 27, 29, 29, 35,
    22, 24, 27, 27, 29, 32, 34, 38,
    26, 27, 29, 29, 32, 35, 38, 46,
    27, 29, 34, 34, 35, 40, 46, 56,
    29, 34, 34, 37, 40, 48, 56, 69,
    34, 37, 38, 40, 48, 58, 69, 83,
};

static const uint8_t aic_y_scan[64] = {
     0,  4,  1,  2,  5,  8, 12,  9,
     6,  3,  7, 10, 13, 14, 11, 15,
    47, 43, 46, 45, 42, 39, 35, 38,
    41, 44, 40, 37, 34, 33, 36, 32,
    16, 20, 17, 18, 21, 24, 28, 25,
    22, 19, 23, 26, 29, 30, 27, 31,
    63, 59, 62, 61, 58, 55, 51, 54,
    57, 60, 56, 53, 50, 49, 52, 48,
};

static const uint8_t aic_y_ext_scan[192] = {
     64,  72,  65,  66,  73,  80,  88,  81,
     74,  67,  75,  82,  89,  90,  83,  91,
      0,   4,   1,   2,   5,   8,  12,   9,
      6,   3,   7,  10,  13,  14,  11,  15,
     16,  20,  17,  18,  21,  24,  28,  25,
     22,  19,  23,  26,  29,  30,  27,  31,
    155, 147, 154, 153, 146, 139, 131, 138,
    145, 152, 144, 137, 130, 129, 136, 128,
     47,  43,  46,  45,  42,  39,  35,  38,
     41,  44,  40,  37,  34,  33,  36,  32,
     63,  59,  62,  61,  58,  55,  51,  54,
     57,  60,  56,  53,  50,  49,  52,  48,
     96, 104,  97,  98, 105, 112, 120, 113,
    106,  99, 107, 114, 121, 122, 115, 123,
     68,  76,  69,  70,  77,  84,  92,  85,
     78,  71,  79,  86,  93,  94,  87,  95,
    100, 108, 101, 102, 109, 116, 124, 117,
    110, 103, 111, 118, 125, 126, 119, 127,
    187, 179, 186, 185, 178, 171, 163, 170,
    177, 184, 176, 169, 162, 161, 168, 160,
    159, 151, 158, 157, 150, 143, 135, 142,
    149, 156, 148, 141, 134, 133, 140, 132,
    191, 183, 190, 189, 182, 175, 167, 174,
    181, 188, 180, 173, 166, 165, 172, 164,
};

static const uint8_t aic_c_scan[64] = {
     0,  4,  1,  2,  5,  8, 12,  9,
     6,  3,  7, 10, 13, 14, 11, 15,
    31, 27, 30, 29, 26, 23, 19, 22,
    25, 28, 24, 21, 18, 17, 20, 16,
    32, 36, 33, 34, 37, 40, 44, 41,
    38, 35, 39, 42, 45, 46, 43, 47,
    63, 59, 62, 61, 58, 55, 51, 54,
    57, 60, 56, 53, 50, 49, 52, 48,
};

static const uint8_t aic_c_ext_scan[192] = {
     16,  24,  17,  18,  25,  32,  40,  33,
     26,  19,  27,  34,  41,  42,  35,  43,
      0,   4,   1,   2,   5,   8,  12,   9,
      6,   3,   7,  10,  13,  14,  11,  15,
     20,  28,  21,  22,  29,  36,  44,  37,
     30,  23,  31,  38,  45,  46,  39,  47,
     95,  87,  94,  93,  86,  79,  71,  78,
     85,  92,  84,  77,  70,  69,  76,  68,
     63,  59,  62,  61,  58,  55,  51,  54,
     57,  60,  56,  53,  50,  49,  52,  48,
     91,  83,  90,  89,  82,  75,  67,  74,
     81,  88,  80,  73,  66,  65,  72,  64,
    112, 120, 113, 114, 121, 128, 136, 129,
    122, 115, 123, 130, 137, 138, 131, 139,
     96, 100,  97,  98, 101, 104, 108, 105,
    102,  99, 103, 106, 109, 110, 107, 111,
    116, 124, 117, 118, 125, 132, 140, 133,
    126, 119, 127, 134, 141, 142, 135, 143,
    191, 183, 190, 189, 182, 175, 167, 174,
    181, 188, 180, 173, 166, 165, 172, 164,
    159, 155, 158, 157, 154, 151, 147, 150,
    153, 156, 152, 149, 146, 145, 148, 144,
    187, 179, 186, 185, 178, 171, 163, 170,
    177, 184, 176, 169, 162, 161, 168, 160,
};

static const uint8_t * const aic_scan[NUM_BANDS] = {
    aic_y_scan, aic_c_scan, aic_y_ext_scan, aic_c_ext_scan
};

typedef struct AICContext {
    AVCodecContext *avctx;
    AVFrame        *frame;
    IDCTDSPContext idsp;

    int            num_x_slices;
    int            slice_width;
    int            mb_width, mb_height;
    int            quant;
    int            interlaced;

    int16_t        *slice_data;
    int16_t        *data_ptr[NUM_BANDS];

    DECLARE_ALIGNED(16, int16_t, block)[64];
    DECLARE_ALIGNED(16, uint8_t, quant_matrix)[64];
} AICContext;

static int aic_decode_header(AICContext *ctx, const uint8_t *src, int size)
{
    uint32_t frame_size;
    int width, height;

    if (src[0] != 1) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Invalid version %d\n", src[0]);
        return AVERROR_INVALIDDATA;
    }
    if (src[1] != AIC_HDR_SIZE - 2) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Invalid header size %d\n", src[1]);
        return AVERROR_INVALIDDATA;
    }
    frame_size = AV_RB32(src + 2);
    width      = AV_RB16(src + 6);
    height     = AV_RB16(src + 8);
    if (frame_size > size) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Frame size should be %"PRIu32" got %d\n",
               frame_size, size);
        return AVERROR_INVALIDDATA;
    }
    if (width != ctx->avctx->width || height != ctx->avctx->height) {
        av_log(ctx->avctx, AV_LOG_ERROR,
               "Picture dimension changed: old: %d x %d, new: %d x %d\n",
               ctx->avctx->width, ctx->avctx->height, width, height);
        return AVERROR_INVALIDDATA;
    }
    ctx->quant      = src[15];
    ctx->interlaced = ((src[16] >> 4) == 3);

    return 0;
}

#define GET_CODE(val, type, add_bits)                         \
    do {                                                      \
        if (type)                                             \
            val = get_ue_golomb(gb);                          \
        else                                                  \
            val = get_unary(gb, 1, 31);                       \
        if (add_bits)                                         \
            val = (val << add_bits) + get_bits(gb, add_bits); \
    } while (0)

static int aic_decode_coeffs(GetBitContext *gb, int16_t *dst,
                             int band, int slice_width, int force_chroma)
{
    int has_skips, coeff_type, coeff_bits, skip_type, skip_bits;
    const int num_coeffs = aic_num_band_coeffs[band];
    const uint8_t *scan = aic_scan[band | force_chroma];
    int mb, idx;
    unsigned val;

    if (get_bits_left(gb) < 5)
        return AVERROR_INVALIDDATA;

    has_skips  = get_bits1(gb);
    coeff_type = get_bits1(gb);
    coeff_bits = get_bits(gb, 3);

    if (has_skips) {
        skip_type = get_bits1(gb);
        skip_bits = get_bits(gb, 3);

        for (mb = 0; mb < slice_width; mb++) {
            idx = -1;
            do {
                GET_CODE(val, skip_type, skip_bits);
                if (val >= 0x10000)
                    return AVERROR_INVALIDDATA;
                idx += val + 1;
                if (idx >= num_coeffs)
                    break;
                GET_CODE(val, coeff_type, coeff_bits);
                val++;
                if (val >= 0x10000)
                    return AVERROR_INVALIDDATA;
                dst[scan[idx]] = val;
            } while (idx < num_coeffs - 1);
            dst += num_coeffs;
        }
    } else {
        for (mb = 0; mb < slice_width; mb++) {
            for (idx = 0; idx < num_coeffs; idx++) {
                GET_CODE(val, coeff_type, coeff_bits);
                if (val >= 0x10000)
                    return AVERROR_INVALIDDATA;
                dst[scan[idx]] = val;
            }
            dst += num_coeffs;
        }
    }
    return 0;
}

static void recombine_block(int16_t *dst, const uint8_t *scan,
                            int16_t **base, int16_t **ext)
{
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++)
            dst[scan[i * 8 + j]]     = (*base)[j];
        for (j = 0; j < 4; j++)
            dst[scan[i * 8 + j + 4]] = (*ext)[j];
        *base += 4;
        *ext  += 4;
    }
    for (; i < 8; i++) {
        for (j = 0; j < 8; j++)
            dst[scan[i * 8 + j]] = (*ext)[j];
        *ext  += 8;
    }
}

static void recombine_block_il(int16_t *dst, const uint8_t *scan,
                               int16_t **base, int16_t **ext,
                               int block_no)
{
    int i, j;

    if (block_no < 2) {
        for (i = 0; i < 8; i++) {
            for (j = 0; j < 4; j++)
                dst[scan[i * 8 + j]]     = (*base)[j];
            for (j = 0; j < 4; j++)
                dst[scan[i * 8 + j + 4]] = (*ext)[j];
            *base += 4;
            *ext  += 4;
        }
    } else {
        for (i = 0; i < 64; i++)
            dst[scan[i]] = (*ext)[i];
        *ext += 64;
    }
}

static void unquant_block(int16_t *block, int q, uint8_t *quant_matrix)
{
    int i;

    for (i = 0; i < 64; i++) {
        int val  = (uint16_t)block[i];
        int sign = val & 1;

        block[i] = (((val >> 1) ^ -sign) * q * quant_matrix[i] >> 4)
                   + sign;
    }
}

static int aic_decode_slice(AICContext *ctx, int mb_x, int mb_y,
                            const uint8_t *src, int src_size)
{
    GetBitContext gb;
    int ret, i, mb, blk;
    int slice_width = FFMIN(ctx->slice_width, ctx->mb_width - mb_x);
    int last_row = mb_y && mb_y == ctx->mb_height - 1;
    int y_pos, c_pos;
    uint8_t *Y, *C[2];
    uint8_t *dst;
    int16_t *base_y = ctx->data_ptr[COEFF_LUMA];
    int16_t *base_c = ctx->data_ptr[COEFF_CHROMA];
    int16_t *ext_y  = ctx->data_ptr[COEFF_LUMA_EXT];
    int16_t *ext_c  = ctx->data_ptr[COEFF_CHROMA_EXT];
    const int ystride = ctx->frame->linesize[0];

    if (last_row) {
        y_pos = (ctx->avctx->height - 16);
        c_pos = ((ctx->avctx->height+1)/2 - 8);
    } else {
        y_pos = mb_y * 16;
        c_pos = mb_y * 8;
    }

    Y = ctx->frame->data[0] + mb_x * 16 + y_pos * ystride;
    for (i = 0; i < 2; i++)
        C[i] = ctx->frame->data[i + 1] + mb_x * 8
               + c_pos * ctx->frame->linesize[i + 1];
    init_get_bits(&gb, src, src_size * 8);

    memset(ctx->slice_data, 0,
           sizeof(*ctx->slice_data) * slice_width * AIC_BAND_COEFFS);
    for (i = 0; i < NUM_BANDS; i++)
        if ((ret = aic_decode_coeffs(&gb, ctx->data_ptr[i],
                                     i, slice_width,
                                     !ctx->interlaced)) < 0)
            return ret;

    for (mb = 0; mb < slice_width; mb++) {
        for (blk = 0; blk < 4; blk++) {
            if (!ctx->interlaced)
                recombine_block(ctx->block, ctx->idsp.idct_permutation,
                                &base_y, &ext_y);
            else
                recombine_block_il(ctx->block, ctx->idsp.idct_permutation,
                                   &base_y, &ext_y, blk);
            unquant_block(ctx->block, ctx->quant, ctx->quant_matrix);
            ctx->idsp.idct(ctx->block);

            if (!ctx->interlaced) {
                dst = Y + (blk >> 1) * 8 * ystride + (blk & 1) * 8;
                ctx->idsp.put_signed_pixels_clamped(ctx->block, dst, ystride);
            } else {
                dst = Y + (blk & 1) * 8 + (blk >> 1) * ystride;
                ctx->idsp.put_signed_pixels_clamped(ctx->block, dst,
                                                    ystride * 2);
            }
        }
        Y += 16;

        for (blk = 0; blk < 2; blk++) {
            recombine_block(ctx->block, ctx->idsp.idct_permutation,
                            &base_c, &ext_c);
            unquant_block(ctx->block, ctx->quant, ctx->quant_matrix);
            ctx->idsp.idct(ctx->block);
            ctx->idsp.put_signed_pixels_clamped(ctx->block, C[blk],
                                                ctx->frame->linesize[blk + 1]);
            C[blk] += 8;
        }
    }

    return 0;
}

static int aic_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame, AVPacket *avpkt)
{
    AICContext *ctx    = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    GetByteContext gb;
    uint32_t off;
    int x, y, ret;
    int slice_size;

    ctx->frame            = frame;

    off = FFALIGN(AIC_HDR_SIZE + ctx->num_x_slices * ctx->mb_height * 2, 4);

    if (buf_size < off) {
        av_log(avctx, AV_LOG_ERROR, "Too small frame\n");
        return AVERROR_INVALIDDATA;
    }

    ret = aic_decode_header(ctx, buf, buf_size);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid header\n");
        return ret;
    }

    if ((ret = ff_thread_get_buffer(avctx, ctx->frame, 0)) < 0)
        return ret;

    bytestream2_init(&gb, buf + AIC_HDR_SIZE,
                     ctx->num_x_slices * ctx->mb_height * 2);

    for (y = 0; y < ctx->mb_height; y++) {
        for (x = 0; x < ctx->mb_width; x += ctx->slice_width) {
            slice_size = bytestream2_get_le16(&gb) * 4;
            if (slice_size + off > buf_size || !slice_size) {
                av_log(avctx, AV_LOG_ERROR,
                       "Incorrect slice size %d at %d.%d\n", slice_size, x, y);
                return AVERROR_INVALIDDATA;
            }

            ret = aic_decode_slice(ctx, x, y, buf + off, slice_size);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR,
                       "Error decoding slice at %d.%d\n", x, y);
                return ret;
            }

            off += slice_size;
        }
    }

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int aic_decode_init(AVCodecContext *avctx)
{
    AICContext *ctx = avctx->priv_data;
    int i;

    ctx->avctx = avctx;

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    ff_idctdsp_init(&ctx->idsp, avctx);

    for (i = 0; i < 64; i++)
        ctx->quant_matrix[ctx->idsp.idct_permutation[i]] = aic_quant_matrix[i];

    ctx->mb_width  = FFALIGN(avctx->width,  16) >> 4;
    ctx->mb_height = FFALIGN(avctx->height, 16) >> 4;

    ctx->num_x_slices = (ctx->mb_width + 15) >> 4;
    ctx->slice_width  = 16;
    for (i = 1; i < ctx->mb_width; i++) {
        if (!(ctx->mb_width % i) && (ctx->mb_width / i <= 32)) {
            ctx->slice_width  = ctx->mb_width / i;
            ctx->num_x_slices = i;
            break;
        }
    }

    ctx->slice_data = av_calloc(ctx->slice_width, AIC_BAND_COEFFS * sizeof(*ctx->slice_data));
    if (!ctx->slice_data) {
        av_log(avctx, AV_LOG_ERROR, "Error allocating slice buffer\n");

        return AVERROR(ENOMEM);
    }

    for (i = 0; i < NUM_BANDS; i++)
        ctx->data_ptr[i] = ctx->slice_data + ctx->slice_width
                                             * aic_band_off[i];

    return 0;
}

static av_cold int aic_decode_close(AVCodecContext *avctx)
{
    AICContext *ctx = avctx->priv_data;

    av_freep(&ctx->slice_data);

    return 0;
}

const FFCodec ff_aic_decoder = {
    .p.name         = "aic",
    CODEC_LONG_NAME("Apple Intermediate Codec"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AIC,
    .priv_data_size = sizeof(AICContext),
    .init           = aic_decode_init,
    .close          = aic_decode_close,
    FF_CODEC_DECODE_CB(aic_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
};
