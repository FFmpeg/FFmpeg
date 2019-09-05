/*
 * VBLE Decoder
 * Copyright (c) 2011 Derek Buitenhuis
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
 * @file
 * VBLE Decoder
 */

#include "libavutil/imgutils.h"

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"
#include "lossless_videodsp.h"
#include "mathops.h"
#include "thread.h"

typedef struct VBLEContext {
    AVCodecContext *avctx;
    LLVidDSPContext llviddsp;

    int            size;
    uint8_t        *val; ///< This array first holds the lengths of vlc symbols and then their value.
} VBLEContext;

static int vble_unpack(VBLEContext *ctx, GetBitContext *gb)
{
    int i;
    int allbits = 0;
    static const uint8_t LUT[256] = {
        8,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
        5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
        6,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
        5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
        7,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
        5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
        6,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
        5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
    };

    /* Read all the lengths in first */
    for (i = 0; i < ctx->size; i++) {
        /* At most we need to read 9 bits total to get indices up to 8 */
        int val = show_bits(gb, 8);

        // read reverse unary
        if (val) {
            val = LUT[val];
            skip_bits(gb, val + 1);
            ctx->val[i] = val;
        } else {
            skip_bits(gb, 8);
            if (!get_bits1(gb))
                return -1;
            ctx->val[i] = 8;
        }
        allbits += ctx->val[i];
    }

    /* Check we have enough bits left */
    if (get_bits_left(gb) < allbits)
        return -1;
    return 0;
}

static void vble_restore_plane(VBLEContext *ctx, AVFrame *pic,
                               GetBitContext *gb, int plane,
                               int offset, int width, int height)
{
    uint8_t *dst = pic->data[plane];
    uint8_t *val = ctx->val + offset;
    int stride = pic->linesize[plane];
    int i, j, left, left_top;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            /* get_bits can't take a length of 0 */
            if (val[j]) {
                int v = (1 << val[j]) + get_bits(gb, val[j]) - 1;
                val[j] = (v >> 1) ^ -(v & 1);
            }
        }
        if (i) {
            left = 0;
            left_top = dst[-stride];
            ctx->llviddsp.add_median_pred(dst, dst - stride, val,
                                          width, &left, &left_top);
        } else {
            dst[0] = val[0];
            for (j = 1; j < width; j++)
                dst[j] = val[j] + dst[j - 1];
        }
        dst += stride;
        val += width;
    }
}

static int vble_decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                             AVPacket *avpkt)
{
    VBLEContext *ctx = avctx->priv_data;
    AVFrame *pic     = data;
    GetBitContext gb;
    const uint8_t *src = avpkt->data;
    int version;
    int offset = 0;
    int width_uv = avctx->width / 2, height_uv = avctx->height / 2;
    int ret;
    ThreadFrame frame = { .f = data };

    if (avpkt->size < 4 || avpkt->size - 4 > INT_MAX/8) {
        av_log(avctx, AV_LOG_ERROR, "Invalid packet size\n");
        return AVERROR_INVALIDDATA;
    }

    /* Allocate buffer */
    if ((ret = ff_thread_get_buffer(avctx, &frame, 0)) < 0)
        return ret;

    /* Set flags */
    pic->key_frame = 1;
    pic->pict_type = AV_PICTURE_TYPE_I;

    /* Version should always be 1 */
    version = AV_RL32(src);

    if (version != 1)
        av_log(avctx, AV_LOG_WARNING, "Unsupported VBLE Version: %d\n", version);

    init_get_bits(&gb, src + 4, (avpkt->size - 4) * 8);

    /* Unpack */
    if (vble_unpack(ctx, &gb) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid Code\n");
        return AVERROR_INVALIDDATA;
    }

    /* Restore planes. Should be almost identical to Huffyuv's. */
    vble_restore_plane(ctx, pic, &gb, 0, offset, avctx->width, avctx->height);

    /* Chroma */
    if (!(ctx->avctx->flags & AV_CODEC_FLAG_GRAY)) {
        offset += avctx->width * avctx->height;
        vble_restore_plane(ctx, pic, &gb, 1, offset, width_uv, height_uv);

        offset += width_uv * height_uv;
        vble_restore_plane(ctx, pic, &gb, 2, offset, width_uv, height_uv);
    }

    *got_frame       = 1;

    return avpkt->size;
}

static av_cold int vble_decode_close(AVCodecContext *avctx)
{
    VBLEContext *ctx = avctx->priv_data;
    av_freep(&ctx->val);

    return 0;
}

static av_cold int vble_decode_init(AVCodecContext *avctx)
{
    VBLEContext *ctx = avctx->priv_data;

    /* Stash for later use */
    ctx->avctx = avctx;
    ff_llviddsp_init(&ctx->llviddsp);

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    avctx->bits_per_raw_sample = 8;

    ctx->size = av_image_get_buffer_size(avctx->pix_fmt,
                                         avctx->width, avctx->height, 1);

    ctx->val = av_malloc_array(ctx->size, sizeof(*ctx->val));

    if (!ctx->val) {
        av_log(avctx, AV_LOG_ERROR, "Could not allocate values buffer.\n");
        vble_decode_close(avctx);
        return AVERROR(ENOMEM);
    }

    return 0;
}

AVCodec ff_vble_decoder = {
    .name           = "vble",
    .long_name      = NULL_IF_CONFIG_SMALL("VBLE Lossless Codec"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VBLE,
    .priv_data_size = sizeof(VBLEContext),
    .init           = vble_decode_init,
    .close          = vble_decode_close,
    .decode         = vble_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    .init_thread_copy = ONLY_IF_THREADS_ENABLED(vble_decode_init),
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
