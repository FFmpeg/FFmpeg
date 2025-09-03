/*
 * Apple ProRes encoder
 *
 * Copyright (c) 2011 Anatoliy Wasserman
 * Copyright (c) 2012 Konstantin Shishkov
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

#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "bytestream.h"
#include "proresdata.h"
#include <sys/types.h>
#include "proresenc_kostya_common.h"

static const uint8_t prores_quant_matrices[][64] = {
    { // proxy
         4,  7,  9, 11, 13, 14, 15, 63,
         7,  7, 11, 12, 14, 15, 63, 63,
         9, 11, 13, 14, 15, 63, 63, 63,
        11, 11, 13, 14, 63, 63, 63, 63,
        11, 13, 14, 63, 63, 63, 63, 63,
        13, 14, 63, 63, 63, 63, 63, 63,
        13, 63, 63, 63, 63, 63, 63, 63,
        63, 63, 63, 63, 63, 63, 63, 63,
    },
    { // proxy chromas
        4,  7,  9, 11, 13, 14, 63, 63,
        7,  7, 11, 12, 14, 63, 63, 63,
        9, 11, 13, 14, 63, 63, 63, 63,
        11, 11, 13, 14, 63, 63, 63, 63,
        11, 13, 14, 63, 63, 63, 63, 63,
        13, 14, 63, 63, 63, 63, 63, 63,
        13, 63, 63, 63, 63, 63, 63, 63,
        63, 63, 63, 63, 63, 63, 63, 63
    },
    { // LT
         4,  5,  6,  7,  9, 11, 13, 15,
         5,  5,  7,  8, 11, 13, 15, 17,
         6,  7,  9, 11, 13, 15, 15, 17,
         7,  7,  9, 11, 13, 15, 17, 19,
         7,  9, 11, 13, 14, 16, 19, 23,
         9, 11, 13, 14, 16, 19, 23, 29,
         9, 11, 13, 15, 17, 21, 28, 35,
        11, 13, 16, 17, 21, 28, 35, 41,
    },
    { // standard
         4,  4,  5,  5,  6,  7,  7,  9,
         4,  4,  5,  6,  7,  7,  9,  9,
         5,  5,  6,  7,  7,  9,  9, 10,
         5,  5,  6,  7,  7,  9,  9, 10,
         5,  6,  7,  7,  8,  9, 10, 12,
         6,  7,  7,  8,  9, 10, 12, 15,
         6,  7,  7,  9, 10, 11, 14, 17,
         7,  7,  9, 10, 11, 14, 17, 21,
    },
    { // high quality
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  5,
         4,  4,  4,  4,  4,  4,  5,  5,
         4,  4,  4,  4,  4,  5,  5,  6,
         4,  4,  4,  4,  5,  5,  6,  7,
         4,  4,  4,  4,  5,  6,  7,  7,
    },
    { // XQ luma
        2,  2,  2,  2,  2,  2,  2,  2,
        2,  2,  2,  2,  2,  2,  2,  2,
        2,  2,  2,  2,  2,  2,  2,  2,
        2,  2,  2,  2,  2,  2,  2,  3,
        2,  2,  2,  2,  2,  2,  3,  3,
        2,  2,  2,  2,  2,  3,  3,  3,
        2,  2,  2,  2,  3,  3,  3,  4,
        2,  2,  2,  2,  3,  3,  4,  4,
    },
    { // codec default
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
    },
};

static const int prores_mb_limits[NUM_MB_LIMITS] = {
    1620, // up to 720x576
    2700, // up to 960x720
    6075, // up to 1440x1080
    9216, // up to 2048x1152
};

static const prores_profile prores_profile_info[6] = {
    {
        .full_name = "proxy",
        .tag       = MKTAG('a', 'p', 'c', 'o'),
        .min_quant = 4,
        .max_quant = 8,
        .br_tab    = { 300, 242, 220, 194 },
        .quant     = QUANT_MAT_PROXY,
        .quant_chroma = QUANT_MAT_PROXY_CHROMA,
    },
    {
        .full_name = "LT",
        .tag       = MKTAG('a', 'p', 'c', 's'),
        .min_quant = 1,
        .max_quant = 9,
        .br_tab    = { 720, 560, 490, 440 },
        .quant     = QUANT_MAT_LT,
        .quant_chroma = QUANT_MAT_LT,
    },
    {
        .full_name = "standard",
        .tag       = MKTAG('a', 'p', 'c', 'n'),
        .min_quant = 1,
        .max_quant = 6,
        .br_tab    = { 1050, 808, 710, 632 },
        .quant     = QUANT_MAT_STANDARD,
        .quant_chroma = QUANT_MAT_STANDARD,
    },
    {
        .full_name = "high quality",
        .tag       = MKTAG('a', 'p', 'c', 'h'),
        .min_quant = 1,
        .max_quant = 6,
        .br_tab    = { 1566, 1216, 1070, 950 },
        .quant     = QUANT_MAT_HQ,
        .quant_chroma = QUANT_MAT_HQ,
    },
    {
        .full_name = "4444",
        .tag       = MKTAG('a', 'p', '4', 'h'),
        .min_quant = 1,
        .max_quant = 6,
        .br_tab    = { 2350, 1828, 1600, 1425 },
        .quant     = QUANT_MAT_HQ,
        .quant_chroma = QUANT_MAT_HQ,
    },
    {
        .full_name = "4444XQ",
        .tag       = MKTAG('a', 'p', '4', 'x'),
        .min_quant = 1,
        .max_quant = 6,
        .br_tab    = { 3525, 2742, 2400, 2137 },
        .quant     = QUANT_MAT_HQ, /* Fix me : use QUANT_MAT_XQ_LUMA */
        .quant_chroma = QUANT_MAT_HQ,
    }
};

av_cold int ff_prores_kostya_encode_init(AVCodecContext *avctx, ProresContext *ctx,
                                         enum AVPixelFormat pix_fmt)
{
    int mps, i, j, min_quant;
    int interlaced = !!(avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT);

    avctx->bits_per_raw_sample = 10;

    ctx->scantable = interlaced ? ff_prores_interlaced_scan
                                : ff_prores_progressive_scan;

    mps = ctx->mbs_per_slice;
    if (mps & (mps - 1)) {
        av_log(avctx, AV_LOG_ERROR,
               "there should be an integer power of two MBs per slice\n");
        return AVERROR(EINVAL);
    }
    if (ctx->profile == PRORES_PROFILE_AUTO) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
        ctx->profile = (desc->flags & AV_PIX_FMT_FLAG_ALPHA ||
                        !(desc->log2_chroma_w + desc->log2_chroma_h))
                     ? PRORES_PROFILE_4444 : PRORES_PROFILE_HQ;
        av_log(avctx, AV_LOG_INFO, "Autoselected %s. It can be overridden "
               "through -profile option.\n", ctx->profile == PRORES_PROFILE_4444
               ? "4:4:4:4 profile because of the used input colorspace"
               : "HQ profile to keep best quality");
    }
    if (av_pix_fmt_desc_get(pix_fmt)->flags & AV_PIX_FMT_FLAG_ALPHA) {
        if (ctx->profile != PRORES_PROFILE_4444 &&
            ctx->profile != PRORES_PROFILE_4444XQ) {
            // force alpha and warn
            av_log(avctx, AV_LOG_WARNING, "Profile selected will not "
                   "encode alpha. Override with -profile if needed.\n");
            ctx->alpha_bits = 0;
        }
        if (ctx->alpha_bits & 7) {
            av_log(avctx, AV_LOG_ERROR, "alpha bits should be 0, 8 or 16\n");
            return AVERROR(EINVAL);
        }
        avctx->bits_per_coded_sample = 32;
    } else {
        ctx->alpha_bits = 0;
    }

    ctx->chroma_factor = pix_fmt == AV_PIX_FMT_YUV422P10
                         ? CFACTOR_Y422
                         : CFACTOR_Y444;
    ctx->profile_info  = prores_profile_info + ctx->profile;
    ctx->num_planes    = 3 + !!ctx->alpha_bits;

    ctx->mb_width      = FFALIGN(avctx->width,  16) >> 4;

    if (interlaced)
        ctx->mb_height = FFALIGN(avctx->height, 32) >> 5;
    else
        ctx->mb_height = FFALIGN(avctx->height, 16) >> 4;

    ctx->slices_width  = ctx->mb_width / mps;
    ctx->slices_width += av_popcount(ctx->mb_width - ctx->slices_width * mps);
    ctx->slices_per_picture = ctx->mb_height * ctx->slices_width;
    ctx->pictures_per_frame = 1 + interlaced;

    if (ctx->quant_sel == -1) {
        ctx->quant_mat = prores_quant_matrices[ctx->profile_info->quant];
        ctx->quant_chroma_mat = prores_quant_matrices[ctx->profile_info->quant_chroma];
    } else {
        ctx->quant_mat = prores_quant_matrices[ctx->quant_sel];
        ctx->quant_chroma_mat = prores_quant_matrices[ctx->quant_sel];
    }

    if (strlen(ctx->vendor) != 4) {
        av_log(avctx, AV_LOG_ERROR, "vendor ID should be 4 bytes\n");
        return AVERROR_INVALIDDATA;
    }

    ctx->force_quant = avctx->global_quality / FF_QP2LAMBDA;
    if (!ctx->force_quant) {
        if (!ctx->bits_per_mb) {
            for (i = 0; i < NUM_MB_LIMITS - 1; i++)
                if (prores_mb_limits[i] >= ctx->mb_width * ctx->mb_height *
                                           ctx->pictures_per_frame)
                    break;
            ctx->bits_per_mb   = ctx->profile_info->br_tab[i];
            if (ctx->alpha_bits)
                ctx->bits_per_mb *= 20;
        } else if (ctx->bits_per_mb < 128) {
            av_log(avctx, AV_LOG_ERROR, "too few bits per MB, please set at least 128\n");
            return AVERROR_INVALIDDATA;
        }

        min_quant = ctx->profile_info->min_quant;
        for (i = min_quant; i < MAX_STORED_Q; i++) {
            for (j = 0; j < 64; j++) {
                ctx->quants[i][j] = ctx->quant_mat[j] * i;
                ctx->quants_chroma[i][j] = ctx->quant_chroma_mat[j] * i;
            }
        }
    } else {
        int ls = 0;
        int ls_chroma = 0;

        if (ctx->force_quant > 64) {
            av_log(avctx, AV_LOG_ERROR, "too large quantiser, maximum is 64\n");
            return AVERROR_INVALIDDATA;
        }

        for (j = 0; j < 64; j++) {
            ctx->quants[0][j] = ctx->quant_mat[j] * ctx->force_quant;
            ctx->quants_chroma[0][j] = ctx->quant_chroma_mat[j] * ctx->force_quant;
            ls += av_log2((1 << 11)  / ctx->quants[0][j]) * 2 + 1;
            ls_chroma += av_log2((1 << 11)  / ctx->quants_chroma[0][j]) * 2 + 1;
        }

        ctx->bits_per_mb = ls * 4 + ls_chroma * 4;
        if (ctx->chroma_factor == CFACTOR_Y444)
            ctx->bits_per_mb += ls_chroma * 4;
    }

    ctx->frame_size_upper_bound = (ctx->pictures_per_frame *
                                   ctx->slices_per_picture + 1) *
                                  (2 + 2 * ctx->num_planes +
                                   (mps * ctx->bits_per_mb) / 8)
                                  + 200;

    if (ctx->alpha_bits) {
         // The alpha plane is run-coded and might exceed the bit budget.
         ctx->frame_size_upper_bound += (ctx->pictures_per_frame *
                                         ctx->slices_per_picture + 1) *
         /* num pixels per slice */     (ctx->mbs_per_slice * 256 *
         /* bits per pixel */            (1 + ctx->alpha_bits + 1) + 7 >> 3);
    }

    avctx->codec_tag   = ctx->profile_info->tag;
    avctx->profile = ctx->profile;

    av_log(avctx, AV_LOG_DEBUG,
           "profile %d, %d slices, interlacing: %s, %d bits per MB\n",
           ctx->profile, ctx->slices_per_picture * ctx->pictures_per_frame,
           interlaced ? "yes" : "no", ctx->bits_per_mb);
    av_log(avctx, AV_LOG_DEBUG, "frame size upper bound: %d\n",
           ctx->frame_size_upper_bound);

    return 0;
}

uint8_t *ff_prores_kostya_write_frame_header(AVCodecContext *avctx, ProresContext *ctx,
                                             uint8_t **orig_buf, int flags,
                                             enum AVColorPrimaries color_primaries,
                                             enum AVColorTransferCharacteristic color_trc,
                                             enum AVColorSpace colorspace)
{
    uint8_t *buf, *tmp;
    uint8_t frame_flags;

    // frame atom
    *orig_buf += 4;                              // frame size
    bytestream_put_be32  (orig_buf, FRAME_ID); // frame container ID
    buf = *orig_buf;

    // frame header
    tmp = buf;
    buf += 2;                                   // frame header size will be stored here
    bytestream_put_be16  (&buf, ctx->chroma_factor != CFACTOR_Y422 || ctx->alpha_bits ? 1 : 0);
    bytestream_put_buffer(&buf, (uint8_t*)ctx->vendor, 4);
    bytestream_put_be16  (&buf, avctx->width);
    bytestream_put_be16  (&buf, avctx->height);

    frame_flags = ctx->chroma_factor << 6;
    if (avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT)
        frame_flags |= (flags & AV_FRAME_FLAG_TOP_FIELD_FIRST) ? 0x04 : 0x08;
    bytestream_put_byte  (&buf, frame_flags);

    bytestream_put_byte  (&buf, 0);             // reserved
    bytestream_put_byte  (&buf, color_primaries);
    bytestream_put_byte  (&buf, color_trc);
    bytestream_put_byte  (&buf, colorspace);
    bytestream_put_byte  (&buf, ctx->alpha_bits >> 3);
    bytestream_put_byte  (&buf, 0);             // reserved
    if (ctx->quant_sel != QUANT_MAT_DEFAULT) {
        bytestream_put_byte  (&buf, 0x03);      // matrix flags - both matrices are present
        bytestream_put_buffer(&buf, ctx->quant_mat, 64);        // luma quantisation matrix
        bytestream_put_buffer(&buf, ctx->quant_chroma_mat, 64); // chroma quantisation matrix
    } else {
        bytestream_put_byte  (&buf, 0x00);      // matrix flags - default matrices are used
    }
    bytestream_put_be16  (&tmp, buf - *orig_buf); // write back frame header size
    return buf;
}

uint8_t *ff_prores_kostya_write_picture_header(ProresContext *ctx, uint8_t *buf)
{
    bytestream_put_byte  (&buf, 0x40); // picture header size (in bits)
    buf += 4;                                   // picture data size will be stored here
    bytestream_put_be16  (&buf, ctx->slices_per_picture);
    bytestream_put_byte  (&buf, av_log2(ctx->mbs_per_slice) << 4); // slice width and height in MBs
    return buf;
}
