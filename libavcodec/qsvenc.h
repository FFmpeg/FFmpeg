/*
 * Intel MediaSDK QSV encoder utility functions
 *
 * copyright (c) 2013 Yukinori Yamazoe
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

#ifndef AVCODEC_QSVENC_H
#define AVCODEC_QSVENC_H

#include <stdint.h>
#include <sys/types.h>

#include <mfx/mfxvideo.h>

#include "libavutil/avutil.h"
#include "libavutil/fifo.h"

#include "avcodec.h"
#include "qsv_internal.h"

#define QSV_HAVE_CO2 QSV_VERSION_ATLEAST(1, 6)
#define QSV_HAVE_CO3 QSV_VERSION_ATLEAST(1, 11)

#define QSV_HAVE_TRELLIS QSV_VERSION_ATLEAST(1, 8)

#define QSV_HAVE_LA     QSV_VERSION_ATLEAST(1, 7)
#define QSV_HAVE_LA_HRD QSV_VERSION_ATLEAST(1, 11)
#define QSV_HAVE_ICQ    QSV_VERSION_ATLEAST(1, 8)
#define QSV_HAVE_VCM    QSV_VERSION_ATLEAST(1, 8)
#define QSV_HAVE_QVBR   QSV_VERSION_ATLEAST(1, 11)

typedef struct QSVEncContext {
    AVCodecContext *avctx;

    QSVFrame *work_frames;

    mfxSession session;
    QSVSession internal_qs;

    int packet_size;
    int width_align;
    int height_align;

    mfxVideoParam param;
    mfxFrameAllocRequest req;

    mfxExtCodingOption  extco;
#if QSV_VERSION_ATLEAST(1,6)
    mfxExtCodingOption2 extco2;
#endif

    mfxExtOpaqueSurfaceAlloc opaque_alloc;
    mfxFrameSurface1       **opaque_surfaces;
    AVBufferRef             *opaque_alloc_buf;

    mfxExtBuffer  *extparam_internal[3];
    int         nb_extparam_internal;

    mfxExtBuffer **extparam;

    AVFifoBuffer *async_fifo;

    // options set by the caller
    int async_depth;
    int idr_interval;
    int profile;
    int preset;
    int avbr_accuracy;
    int avbr_convergence;
    int pic_timing_sei;
    int look_ahead;
    int look_ahead_depth;
    int look_ahead_downsampling;

    char *load_plugins;
} QSVEncContext;

int ff_qsv_enc_init(AVCodecContext *avctx, QSVEncContext *q);

int ff_qsv_encode(AVCodecContext *avctx, QSVEncContext *q,
                  AVPacket *pkt, const AVFrame *frame, int *got_packet);

int ff_qsv_enc_close(AVCodecContext *avctx, QSVEncContext *q);

#endif /* AVCODEC_QSVENC_H */
