/*
 * Intel MediaSDK QSV utility functions
 *
 * copyright (c) 2013 Luca Barbato
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

#ifndef AVCODEC_QSVDEC_H
#define AVCODEC_QSVDEC_H

#include <stdint.h>
#include <sys/types.h>

#include <mfx/mfxvideo.h>

#include "libavutil/fifo.h"
#include "libavutil/frame.h"
#include "libavutil/pixfmt.h"

#include "avcodec.h"
#include "qsv_internal.h"

typedef struct QSVContext {
    // the session used for decoding
    mfxSession session;

    // the session we allocated internally, in case the caller did not provide
    // one
    QSVSession internal_qs;

    /**
     * a linked list of frames currently being used by QSV
     */
    QSVFrame *work_frames;

    AVFifoBuffer *async_fifo;
    AVFifoBuffer *input_fifo;

    // we should to buffer input packets at some cases
    // else it is not possible to handle dynamic stream changes correctly
    // this fifo uses for input packets buffering
    AVFifoBuffer *pkt_fifo;

    // this flag indicates that header parsed,
    // decoder instance created and ready to general decoding
    int engine_ready;

    // we can not just re-init decoder if different sequence header arrived
    // we should to deliver all buffered frames but we can not decode new packets
    // this time. So when reinit_pending is non-zero we flushing decoder and
    // accumulate new arrived packets into pkt_fifo
    int reinit_pending;

    // options set by the caller
    int async_depth;
    int iopattern;

    char *load_plugins;

    mfxExtBuffer **ext_buffers;
    int         nb_ext_buffers;
} QSVContext;

int ff_qsv_map_pixfmt(enum AVPixelFormat format);

int ff_qsv_decode(AVCodecContext *s, QSVContext *q,
                  AVFrame *frame, int *got_frame,
                  AVPacket *avpkt);

void ff_qsv_decode_reset(AVCodecContext *avctx, QSVContext *q);

int ff_qsv_decode_close(QSVContext *q);

#endif /* AVCODEC_QSVDEC_H */
