/*
 * VDA H.264 hardware acceleration
 *
 * copyright (c) 2011 Sebastien Zwickert
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

#include "h264.h"
#include "h264data.h"

#include "vda_internal.h"

/* This structure is used to store the bitstream of the current frame. */
struct vda_picture_context {
    uint8_t *bitstream;
    int      bitstream_size;
};

static int start_frame(AVCodecContext *avctx,
                       av_unused const uint8_t *buffer,
                       av_unused uint32_t size)
{
    const H264Context *h                = avctx->priv_data;
    struct vda_context *vda_ctx         = avctx->hwaccel_context;
    struct vda_picture_context *pic_ctx = h->s.current_picture_ptr->f.hwaccel_picture_private;

    if (!vda_ctx->decoder)
        return -1;

    pic_ctx->bitstream      = NULL;
    pic_ctx->bitstream_size = 0;

    return 0;
}

static int decode_slice(AVCodecContext *avctx,
                        const uint8_t *buffer,
                        uint32_t size)
{
    H264Context *h                      = avctx->priv_data;
    struct vda_context *vda_ctx         = avctx->hwaccel_context;
    struct vda_picture_context *pic_ctx = h->s.current_picture_ptr->f.hwaccel_picture_private;
    void *tmp;

    if (!vda_ctx->decoder)
        return -1;

    tmp = av_realloc(pic_ctx->bitstream, pic_ctx->bitstream_size+size+4);
    if (!tmp)
        return AVERROR(ENOMEM);

    pic_ctx->bitstream = tmp;

    AV_WB32(pic_ctx->bitstream + pic_ctx->bitstream_size, size);
    memcpy(pic_ctx->bitstream + pic_ctx->bitstream_size + 4, buffer, size);

    pic_ctx->bitstream_size += size + 4;

    return 0;
}

static int end_frame(AVCodecContext *avctx)
{
    H264Context *h                      = avctx->priv_data;
    struct vda_context *vda_ctx         = avctx->hwaccel_context;
    struct vda_picture_context *pic_ctx = h->s.current_picture_ptr->f.hwaccel_picture_private;
    AVFrame *frame                      = &h->s.current_picture_ptr->f;
    int status;

    if (!vda_ctx->decoder || !pic_ctx->bitstream)
        return -1;

    status = ff_vda_decoder_decode(vda_ctx, pic_ctx->bitstream,
                                   pic_ctx->bitstream_size,
                                   frame->reordered_opaque);

    if (status)
        av_log(avctx, AV_LOG_ERROR, "Failed to decode frame (%d)\n", status);

    av_freep(&pic_ctx->bitstream);

    return status;
}

AVHWAccel ff_h264_vda_hwaccel = {
    .name           = "h264_vda",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_H264,
    .pix_fmt        = PIX_FMT_VDA_VLD,
    .start_frame    = start_frame,
    .decode_slice   = decode_slice,
    .end_frame      = end_frame,
    .priv_data_size = sizeof(struct vda_picture_context),
};
