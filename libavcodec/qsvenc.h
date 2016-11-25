/*
 * Intel MediaSDK QSV encoder utility functions
 *
 * copyright (c) 2013 Yukinori Yamazoe
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

#ifndef AVCODEC_QSVENC_H
#define AVCODEC_QSVENC_H

#include <stdint.h>
#include <sys/types.h>
#include <mfx/mfxvideo.h>

#include "libavutil/avutil.h"


typedef struct QSVEncSurfaceList {
    mfxFrameSurface1 surface;
    int pending;
    struct QSVEncSurfaceList *next;
} QSVEncSurfaceList;

typedef struct QSVEncBuffer {
    uint8_t *data;
    mfxBitstream bs;
    mfxSyncPoint sync;
    struct QSVEncBuffer *next;
} QSVEncBuffer;

typedef struct QSVEncOptions {
    int async_depth;
    int timeout;
    int qpi;
    int qpp;
    int qpb;
    int idr_interval;
    int profile;
    int level;
    int preset;
    int open_gop;
} QSVEncOptions;

typedef struct QSVEncContext {
    AVClass *class;
    mfxSession session;
    mfxIMPL impl;
    mfxVersion ver;
    mfxVideoParam param;
    mfxFrameAllocRequest req;
    mfxExtCodingOption extco;
    mfxExtCodingOption2 extco2;
    mfxExtCodingOptionSPSPPS extcospspps;
    mfxExtBuffer *extparam[3];
    uint8_t spspps[2][256];
    QSVEncSurfaceList **surf;
    int nb_surf;
    QSVEncSurfaceList *pending_enc, *pending_enc_end;
    QSVEncBuffer **buf;
    int nb_buf;
    QSVEncBuffer *pending_sync, *pending_sync_end;
    int nb_sync;
    QSVEncOptions options;
} QSVEncContext;

int ff_qsv_enc_init(AVCodecContext *avctx, QSVEncContext *q);

int ff_qsv_enc_frame(AVCodecContext *avctx, QSVEncContext *q,
                     AVPacket *pkt, const AVFrame *frame, int *got_packet);

int ff_qsv_enc_close(AVCodecContext *avctx, QSVEncContext *q);

#endif /* AVCODEC_QSVENC_H */
