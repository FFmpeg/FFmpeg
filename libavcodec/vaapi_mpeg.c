/*
 * Video Acceleration API (video decoding)
 * HW decode acceleration for MPEG-2, MPEG-4, H.264 and VC-1
 *
 * Copyright (C) 2013 Anton Khirnov
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

#include "avcodec.h"
#include "vaapi_internal.h"

int ff_vaapi_mpeg_end_frame(AVCodecContext *avctx)
{
    struct vaapi_context * const vactx = avctx->hwaccel_context;
    MpegEncContext *s = avctx->priv_data;
    int ret;

    ret = ff_vaapi_commit_slices(vactx);
    if (ret < 0)
        goto finish;

    ret = ff_vaapi_render_picture(vactx,
                                  ff_vaapi_get_surface_id(s->current_picture_ptr));
    if (ret < 0)
        goto finish;

    ff_mpeg_draw_horiz_band(s, 0, s->avctx->height);

finish:
    ff_vaapi_common_end_frame(avctx);
    return ret;
}

