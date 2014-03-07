/*
 * Mirillis FIC decoder
 *
 * Copyright (c) 2014 Konstantin Shishkov
 * Copyright (c) 2014 Derek Buitenhuis
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

#include "libavutil/common.h"
#include "avcodec.h"
#include "internal.h"
#include "dsputil.h"
#include "get_bits.h"
#include "golomb.h"

typedef struct FICThreadContext {
    DECLARE_ALIGNED(16, int16_t, block)[64];
    uint8_t *src;
    int slice_h;
    int src_size;
    int y_off;
} FICThreadContext;

typedef struct FICContext {
    AVCodecContext *avctx;
    AVFrame *frame;

    DSPContext dsp;
    ScanTable scantable;

    FICThreadContext *slice_data;
    int slice_data_size;

    const uint8_t *qmat;

    enum AVPictureType cur_frame_type;

    int aligned_width, aligned_height;
    int num_slices, slice_h;
} FICContext;

static const uint8_t fic_qmat_hq[64] = {
    1, 2, 2, 2, 3, 3, 3, 4,
    2, 2, 2, 3, 3, 3, 4, 4,
    2, 2, 3, 3, 3, 4, 4, 4,
    2, 2, 3, 3, 3, 4, 4, 5,
    2, 3, 3, 3, 4, 4, 5, 6,
    3, 3, 3, 4, 4, 5, 6, 7,
    3, 3, 3, 4, 4, 5, 7, 7,
    3, 3, 4, 4, 5, 7, 7, 7,
};

static const uint8_t fic_qmat_lq[64] = {
    1,  5,  6,  7,  8,  9,  9, 11,
    5,  5,  7,  8,  9,  9, 11, 12,
    6,  7,  8,  9,  9, 11, 11, 12,
    7,  7,  8,  9,  9, 11, 12, 13,
    7,  8,  9,  9, 10, 11, 13, 16,
    8,  9,  9, 10, 11, 13, 16, 19,
    8,  9,  9, 11, 12, 15, 18, 23,
    9,  9, 11, 12, 15, 18, 23, 27
};

static const uint8_t fic_header[7] = { 0, 0, 1, 'F', 'I', 'C', 'V' };

#define FIC_HEADER_SIZE 27

static int fic_decode_block(FICContext *ctx, GetBitContext *gb,
                            uint8_t *dst, int stride, int16_t *block)
{
    int i, num_coeff;

    /* Is it a skip block? */
    if (get_bits1(gb)) {
        /* This is a P-frame. */
        ctx->frame->key_frame = 0;
        ctx->frame->pict_type = AV_PICTURE_TYPE_P;

        return 0;
    }

    ctx->dsp.clear_block(block);

    num_coeff = get_bits(gb, 7);
    if (num_coeff > 64)
        return AVERROR_INVALIDDATA;

    for (i = 0; i < num_coeff; i++)
        block[ctx->scantable.permutated[i]] = get_se_golomb(gb) * ctx->qmat[i];

    ctx->dsp.idct_put(dst, stride, block);

    return 0;
}

static int fic_decode_slice(AVCodecContext *avctx, void *tdata)
{
    FICContext *ctx        = avctx->priv_data;
    FICThreadContext *tctx = tdata;
    GetBitContext gb;
    uint8_t *src = tctx->src;
    int slice_h  = tctx->slice_h;
    int src_size = tctx->src_size;
    int y_off    = tctx->y_off;
    int x, y, p;

    init_get_bits(&gb, src, src_size * 8);

    for (p = 0; p < 3; p++) {
        int stride   = ctx->frame->linesize[p];
        uint8_t* dst = ctx->frame->data[p] + (y_off >> !!p) * stride;

        for (y = 0; y < (slice_h >> !!p); y += 8) {
            for (x = 0; x < (ctx->aligned_width >> !!p); x += 8) {
                int ret;

                if ((ret = fic_decode_block(ctx, &gb, dst + x, stride, tctx->block)) != 0)
                    return ret;
            }

            dst += 8 * stride;
        }
    }

    return 0;
}

static int fic_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *avpkt)
{
    FICContext *ctx = avctx->priv_data;
    uint8_t *src = avpkt->data;
    int ret;
    int slice, nslices;
    int msize;
    int tsize;
    uint8_t *sdata;

    if ((ret = ff_reget_buffer(avctx, ctx->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return ret;
    }

    /* Header + at least one slice (4) */
    if (avpkt->size < FIC_HEADER_SIZE + 4) {
        av_log(avctx, AV_LOG_ERROR, "Frame data is too small.\n");
        return AVERROR_INVALIDDATA;
    }

    /* Check for header. */
    if (memcmp(src, fic_header, 7))
        av_log(avctx, AV_LOG_WARNING, "Invalid FIC Header.\n");

    /* Is it a skip frame? */
    if (src[17])
        goto skip;

    nslices = src[13];
    if (!nslices) {
        av_log(avctx, AV_LOG_ERROR, "Zero slices found.\n");
        return AVERROR_INVALIDDATA;
    }

    /* High or Low Quality Matrix? */
    ctx->qmat = src[23] ? fic_qmat_hq : fic_qmat_lq;

    /* Skip cursor data. */
    tsize = AV_RB24(src + 24);
    if (tsize > avpkt->size - FIC_HEADER_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "Invalid cursor data size.\n");
        return AVERROR_INVALIDDATA;
    }

    /* Slice height for all but the last slice. */
    ctx->slice_h = 16 * (ctx->aligned_height >> 4) / nslices;
    if (ctx->slice_h % 16)
        ctx->slice_h = FFALIGN(ctx->slice_h - 16, 16);

    /* First slice offset and remaining data. */
    sdata = src + tsize + FIC_HEADER_SIZE + 4 * nslices;
    msize = avpkt->size - nslices * 4 - tsize - FIC_HEADER_SIZE;

    if (msize <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Not enough frame data to decode.\n");
        return AVERROR_INVALIDDATA;
    }

    /*
     * Set the frametype to I initially. It will be set to P if the frame
     * has any dependencies (skip blocks). There will be a race condition
     * inside the slice decode function to set these, but we do not care.
     * since they will only ever be set to 0/P.
     */
    ctx->frame->key_frame = 1;
    ctx->frame->pict_type = AV_PICTURE_TYPE_I;

    /* Allocate slice data. */
    av_fast_malloc(&ctx->slice_data, &ctx->slice_data_size,
                   nslices * sizeof(ctx->slice_data[0]));
    if (!ctx->slice_data_size) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate slice data.\n");
        return AVERROR(ENOMEM);
    }
    memset(ctx->slice_data, 0, nslices * sizeof(ctx->slice_data[0]));

    for (slice = 0; slice < nslices; slice++) {
        unsigned slice_off = AV_RB32(src + tsize + FIC_HEADER_SIZE + slice * 4);
        unsigned slice_size;
        int y_off   = ctx->slice_h * slice;
        int slice_h = ctx->slice_h;

        /*
         * Either read the slice size, or consume all data left.
         * Also, special case the last slight height.
         */
        if (slice == nslices - 1) {
            slice_size   = msize;
            slice_h      = FFALIGN(avctx->height - ctx->slice_h * (nslices - 1), 16);
        } else {
            slice_size = AV_RB32(src + tsize + FIC_HEADER_SIZE + slice * 4 + 4);
        }

        if (slice_size < slice_off || slice_size > msize)
            continue;

        slice_size -= slice_off;

        ctx->slice_data[slice].src      = sdata + slice_off;
        ctx->slice_data[slice].src_size = slice_size;
        ctx->slice_data[slice].slice_h  = slice_h;
        ctx->slice_data[slice].y_off    = y_off;
    }

    if (ret = avctx->execute(avctx, fic_decode_slice, ctx->slice_data,
                             NULL, nslices, sizeof(ctx->slice_data[0])) < 0)
        return ret;

skip:
    *got_frame = 1;
    if ((ret = av_frame_ref(data, ctx->frame)) < 0)
        return ret;

    return avpkt->size;
}

static av_cold int fic_decode_close(AVCodecContext *avctx)
{
    FICContext *ctx = avctx->priv_data;

    av_freep(&ctx->slice_data);
    av_frame_free(&ctx->frame);

    return 0;
}

static av_cold int fic_decode_init(AVCodecContext *avctx)
{
    FICContext *ctx = avctx->priv_data;

    /* Initialize various context values */
    ctx->avctx            = avctx;
    ctx->aligned_width    = FFALIGN(avctx->width,  16);
    ctx->aligned_height   = FFALIGN(avctx->height, 16);

    avctx->pix_fmt             = AV_PIX_FMT_YUV420P;
    avctx->bits_per_raw_sample = 8;

    ctx->frame = av_frame_alloc();
    if (!ctx->frame)
        return AVERROR(ENOMEM);

    ff_dsputil_init(&ctx->dsp, avctx);

    ff_init_scantable(ctx->dsp.idct_permutation, &ctx->scantable, ff_zigzag_direct);

    return 0;
}

AVCodec ff_fic_decoder = {
    .name           = "fic",
    .long_name      = NULL_IF_CONFIG_SMALL("Mirillis FIC"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_FIC,
    .priv_data_size = sizeof(FICContext),
    .init           = fic_decode_init,
    .decode         = fic_decode_frame,
    .close          = fic_decode_close,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_SLICE_THREADS,
};
