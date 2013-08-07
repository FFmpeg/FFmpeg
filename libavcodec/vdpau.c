/*
 * Video Decode and Presentation API for UNIX (VDPAU) is used for
 * HW decode acceleration for MPEG-1/2, MPEG-4 ASP, H.264 and VC-1.
 *
 * Copyright (c) 2008 NVIDIA
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

#include <limits.h>
#include "avcodec.h"
#include "h264.h"
#include "vc1.h"

#undef NDEBUG
#include <assert.h>

#include "vdpau.h"
#include "vdpau_internal.h"

/**
 * @addtogroup VDPAU_Decoding
 *
 * @{
 */

AVVDPAUContext *av_alloc_vdpaucontext(void)
{
    return av_mallocz(sizeof(AVVDPAUContext));
}

MAKE_ACCESSORS(AVVDPAUContext, vdpau_hwaccel, AVVDPAU_Render2, render2)

int ff_vdpau_common_start_frame(Picture *pic,
                                av_unused const uint8_t *buffer,
                                av_unused uint32_t size)
{
    struct vdpau_picture_context *pic_ctx = pic->hwaccel_picture_private;

    pic_ctx->bitstream_buffers_allocated = 0;
    pic_ctx->bitstream_buffers_used      = 0;
    pic_ctx->bitstream_buffers           = NULL;
    return 0;
}

#if CONFIG_H263_VDPAU_HWACCEL  || CONFIG_MPEG1_VDPAU_HWACCEL || \
    CONFIG_MPEG2_VDPAU_HWACCEL || CONFIG_MPEG4_VDPAU_HWACCEL || \
    CONFIG_VC1_VDPAU_HWACCEL   || CONFIG_WMV3_VDPAU_HWACCEL
int ff_vdpau_mpeg_end_frame(AVCodecContext *avctx)
{
    int res = 0;
    AVVDPAUContext *hwctx = avctx->hwaccel_context;
    MpegEncContext *s = avctx->priv_data;
    Picture *pic = s->current_picture_ptr;
    struct vdpau_picture_context *pic_ctx = pic->hwaccel_picture_private;
    VdpVideoSurface surf = ff_vdpau_get_surface_id(pic);

    if (!hwctx->render) {
        res = hwctx->render2(avctx, &pic->f, (void *)&pic_ctx->info,
                             pic_ctx->bitstream_buffers_used, pic_ctx->bitstream_buffers);
    } else
    hwctx->render(hwctx->decoder, surf, (void *)&pic_ctx->info,
                  pic_ctx->bitstream_buffers_used, pic_ctx->bitstream_buffers);

    ff_mpeg_draw_horiz_band(s, 0, s->avctx->height);
    av_freep(&pic_ctx->bitstream_buffers);

    return res;
}
#endif

int ff_vdpau_add_buffer(Picture *pic, const uint8_t *buf, uint32_t size)
{
    struct vdpau_picture_context *pic_ctx = pic->hwaccel_picture_private;
    VdpBitstreamBuffer *buffers = pic_ctx->bitstream_buffers;

    buffers = av_fast_realloc(buffers, &pic_ctx->bitstream_buffers_allocated,
                              (pic_ctx->bitstream_buffers_used + 1) * sizeof(*buffers));
    if (!buffers)
        return AVERROR(ENOMEM);

    pic_ctx->bitstream_buffers = buffers;
    buffers += pic_ctx->bitstream_buffers_used++;

    buffers->struct_version  = VDP_BITSTREAM_BUFFER_VERSION;
    buffers->bitstream       = buf;
    buffers->bitstream_bytes = size;
    return 0;
}
/* @}*/
