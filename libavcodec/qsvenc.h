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
#define QSV_HAVE_MAX_SLICE_SIZE QSV_VERSION_ATLEAST(1, 9)
#define QSV_HAVE_BREF_TYPE      QSV_VERSION_ATLEAST(1, 8)

#define QSV_HAVE_LA     QSV_VERSION_ATLEAST(1, 7)
#define QSV_HAVE_LA_HRD QSV_VERSION_ATLEAST(1, 11)
#define QSV_HAVE_ICQ    QSV_VERSION_ATLEAST(1, 8)
#define QSV_HAVE_VCM    QSV_VERSION_ATLEAST(1, 8)
#define QSV_HAVE_QVBR   QSV_VERSION_ATLEAST(1, 11)

#define QSV_COMMON_OPTS \
{ "async_depth", "Maximum processing parallelism", OFFSET(qsv.async_depth), AV_OPT_TYPE_INT, { .i64 = ASYNC_DEPTH_DEFAULT }, 0, INT_MAX, VE },                          \
{ "avbr_accuracy",    "Accuracy of the AVBR ratecontrol",    OFFSET(qsv.avbr_accuracy),    AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VE },                             \
{ "avbr_convergence", "Convergence of the AVBR ratecontrol", OFFSET(qsv.avbr_convergence), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VE },                             \
{ "preset", NULL, OFFSET(qsv.preset), AV_OPT_TYPE_INT, { .i64 = MFX_TARGETUSAGE_BALANCED }, MFX_TARGETUSAGE_BEST_QUALITY, MFX_TARGETUSAGE_BEST_SPEED,   VE, "preset" }, \
{ "veryfast",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_BEST_SPEED  },   INT_MIN, INT_MAX, VE, "preset" },                                                \
{ "faster",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_6  },            INT_MIN, INT_MAX, VE, "preset" },                                                \
{ "fast",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_5  },            INT_MIN, INT_MAX, VE, "preset" },                                                \
{ "medium",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_BALANCED  },     INT_MIN, INT_MAX, VE, "preset" },                                                \
{ "slow",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_3  },            INT_MIN, INT_MAX, VE, "preset" },                                                \
{ "slower",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_2  },            INT_MIN, INT_MAX, VE, "preset" },                                                \
{ "veryslow",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_BEST_QUALITY  }, INT_MIN, INT_MAX, VE, "preset" },                                                \
{ "vcm",      "Use the video conferencing mode ratecontrol",  OFFSET(qsv.vcm),      AV_OPT_TYPE_INT, { .i64 = 0  },  0, 1,         VE },                                \
{ "rdo",            "Enable rate distortion optimization",    OFFSET(qsv.rdo),            AV_OPT_TYPE_INT, { .i64 = -1 }, -1,          1, VE },                         \
{ "max_frame_size", "Maximum encoded frame size in bytes",    OFFSET(qsv.max_frame_size), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, UINT16_MAX, VE },                         \
{ "max_slice_size", "Maximum encoded slice size in bytes",    OFFSET(qsv.max_slice_size), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, UINT16_MAX, VE },                         \
{ "bitrate_limit",  "Toggle bitrate limitations",             OFFSET(qsv.bitrate_limit),  AV_OPT_TYPE_INT, { .i64 = -1 }, -1,          1, VE },                         \
{ "mbbrc",          "MB level bitrate control",               OFFSET(qsv.mbbrc),          AV_OPT_TYPE_INT, { .i64 = -1 }, -1,          1, VE },                         \
{ "extbrc",         "Extended bitrate control",               OFFSET(qsv.extbrc),         AV_OPT_TYPE_INT, { .i64 = -1 }, -1,          1, VE },                         \
{ "adaptive_i",     "Adaptive I-frame placement",             OFFSET(qsv.adaptive_i),     AV_OPT_TYPE_INT, { .i64 = -1 }, -1,          1, VE },                         \
{ "adaptive_b",     "Adaptive B-frame placement",             OFFSET(qsv.adaptive_b),     AV_OPT_TYPE_INT, { .i64 = -1 }, -1,          1, VE },                         \

typedef int SetEncodeCtrlCB (AVCodecContext *avctx,
                             const AVFrame *frame, mfxEncodeCtrl* enc_ctrl);
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
#if QSV_HAVE_CO2
    mfxExtCodingOption2 extco2;
#endif

    mfxExtOpaqueSurfaceAlloc opaque_alloc;
    mfxFrameSurface1       **opaque_surfaces;
    AVBufferRef             *opaque_alloc_buf;

    mfxExtBuffer  *extparam_internal[2 + QSV_HAVE_CO2];
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
    int vcm;
    int rdo;
    int max_frame_size;
    int max_slice_size;

    int single_sei_nal_unit;
    int max_dec_frame_buffering;
    int trellis;

    int bitrate_limit;
    int mbbrc;
    int extbrc;
    int adaptive_i;
    int adaptive_b;

    int int_ref_type;
    int int_ref_cycle_size;
    int int_ref_qp_delta;
    int recovery_point_sei;

    int a53_cc;
    char *load_plugins;
    SetEncodeCtrlCB *set_encode_ctrl_cb;
} QSVEncContext;

int ff_qsv_enc_init(AVCodecContext *avctx, QSVEncContext *q);

int ff_qsv_encode(AVCodecContext *avctx, QSVEncContext *q,
                  AVPacket *pkt, const AVFrame *frame, int *got_packet);

int ff_qsv_enc_close(AVCodecContext *avctx, QSVEncContext *q);

#endif /* AVCODEC_QSVENC_H */
