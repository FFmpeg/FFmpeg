/*
 * VDA H264 HW acceleration.
 *
 * copyright (c) 2011 Sebastien Zwickert
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

#include "h264.h"
#include "vda_internal.h"

static int start_frame(AVCodecContext *avctx,
                       av_unused const uint8_t *buffer,
                       av_unused uint32_t size)
{
    struct vda_context *vda_ctx = avctx->hwaccel_context;

    if (!vda_ctx->decoder)
        return -1;

    vda_ctx->bitstream_size = 0;

    return 0;
}

static int decode_slice(AVCodecContext *avctx,
                        const uint8_t *buffer,
                        uint32_t size)
{
    struct vda_context *vda_ctx = avctx->hwaccel_context;
    void *tmp;

    if (!vda_ctx->decoder)
        return -1;

    tmp = av_fast_realloc(vda_ctx->bitstream, &vda_ctx->ref_size, vda_ctx->bitstream_size+size+4);
    if (!tmp)
        return AVERROR(ENOMEM);

    vda_ctx->bitstream = tmp;

    AV_WB32(vda_ctx->bitstream+vda_ctx->bitstream_size, size);
    memcpy(vda_ctx->bitstream+vda_ctx->bitstream_size+4, buffer, size);

    vda_ctx->bitstream_size += size + 4;

    return 0;
}

static int end_frame(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    struct vda_context *vda_ctx = avctx->hwaccel_context;
    AVFrame *frame = &h->s.current_picture_ptr->f;
    int status;

    if (!vda_ctx->decoder || !vda_ctx->bitstream)
        return -1;

    status = ff_vda_decoder_decode(vda_ctx, vda_ctx->bitstream,
                                   vda_ctx->bitstream_size,
                                   frame->reordered_opaque);

    if (status)
        av_log(avctx, AV_LOG_ERROR, "Failed to decode frame (%d)\n", status);

    return status;
}

AVHWAccel ff_h264_vda_hwaccel = {
    .name           = "h264_vda",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_H264,
    .pix_fmt        = PIX_FMT_VDA_VLD,
    .capabilities   = 0,
    .start_frame    = start_frame,
    .decode_slice   = decode_slice,
    .end_frame      = end_frame,
    .priv_data_size = 0,
};
