/*
 * MPEGVideo decoders header
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer
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
 * mpegvideo decoder header.
 */

#ifndef AVCODEC_MPEGVIDEODEC_H
#define AVCODEC_MPEGVIDEODEC_H

#include "libavutil/frame.h"
#include "avcodec.h"
#include "get_bits.h"
#include "mpegpicture.h"
#include "mpegvideo.h"
#include "mpegvideodata.h"

#define FF_MPV_QSCALE_TYPE_MPEG1 0
#define FF_MPV_QSCALE_TYPE_MPEG2 1

/**
 * Initialize the given MpegEncContext for decoding.
 * the changed fields will not depend upon
 * the prior state of the MpegEncContext.
 */
void ff_mpv_decode_init(MpegEncContext *s, AVCodecContext *avctx);

int ff_mpv_common_frame_size_change(MpegEncContext *s);

int ff_mpv_frame_start(MpegEncContext *s, AVCodecContext *avctx);
void ff_mpv_report_decode_progress(MpegEncContext *s);
void ff_mpv_frame_end(MpegEncContext *s);

int ff_mpv_export_qp_table(MpegEncContext *s, AVFrame *f, Picture *p, int qp_type);
int ff_mpeg_update_thread_context(AVCodecContext *dst, const AVCodecContext *src);
void ff_mpeg_draw_horiz_band(MpegEncContext *s, int y, int h);
void ff_mpeg_flush(AVCodecContext *avctx);

void ff_print_debug_info(MpegEncContext *s, Picture *p, AVFrame *pict);

static inline int mpeg_get_qscale(MpegEncContext *s)
{
    int qscale = get_bits(&s->gb, 5);
    if (s->q_scale_type)
        return ff_mpeg2_non_linear_qscale[qscale];
    else
        return qscale << 1;
}

#endif /* AVCODEC_MPEGVIDEODEC_H */
