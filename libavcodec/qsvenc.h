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
#include "hwconfig.h"
#include "qsv_internal.h"

#define QSV_HAVE_CO2 QSV_VERSION_ATLEAST(1, 6)
#define QSV_HAVE_CO3 QSV_VERSION_ATLEAST(1, 11)
#define QSV_HAVE_CO_VPS  QSV_VERSION_ATLEAST(1, 17)

#define QSV_HAVE_EXT_HEVC_TILES QSV_VERSION_ATLEAST(1, 13)
#define QSV_HAVE_EXT_VP9_PARAM QSV_VERSION_ATLEAST(1, 26)
#define QSV_HAVE_EXT_VP9_TILES QSV_VERSION_ATLEAST(1, 29)

#define QSV_HAVE_TRELLIS QSV_VERSION_ATLEAST(1, 8)
#define QSV_HAVE_MAX_SLICE_SIZE QSV_VERSION_ATLEAST(1, 9)
#define QSV_HAVE_DISABLEDEBLOCKIDC QSV_VERSION_ATLEAST(1, 9)
#define QSV_HAVE_BREF_TYPE      QSV_VERSION_ATLEAST(1, 8)

#define QSV_HAVE_LA     QSV_VERSION_ATLEAST(1, 7)
#define QSV_HAVE_LA_DS  QSV_VERSION_ATLEAST(1, 8)
#define QSV_HAVE_LA_HRD QSV_VERSION_ATLEAST(1, 11)
#define QSV_HAVE_VDENC  QSV_VERSION_ATLEAST(1, 15)
#define QSV_HAVE_PREF   QSV_VERSION_ATLEAST(1, 16)

#define QSV_HAVE_GPB    QSV_VERSION_ATLEAST(1, 18)

#if defined(_WIN32) || defined(__CYGWIN__)
#define QSV_HAVE_AVBR   QSV_VERSION_ATLEAST(1, 3)
#define QSV_HAVE_ICQ    QSV_VERSION_ATLEAST(1, 8)
#define QSV_HAVE_VCM    QSV_VERSION_ATLEAST(1, 8)
#define QSV_HAVE_QVBR   QSV_VERSION_ATLEAST(1, 11)
#define QSV_HAVE_MF     0
#else
#define QSV_HAVE_AVBR   0
#define QSV_HAVE_ICQ    QSV_VERSION_ATLEAST(1, 28)
#define QSV_HAVE_VCM    0
#define QSV_HAVE_QVBR   QSV_VERSION_ATLEAST(1, 28)
#define QSV_HAVE_MF     QSV_VERSION_ATLEAST(1, 25)
#endif

#if !QSV_HAVE_LA_DS
#define MFX_LOOKAHEAD_DS_UNKNOWN 0
#define MFX_LOOKAHEAD_DS_OFF 0
#define MFX_LOOKAHEAD_DS_2x 0
#define MFX_LOOKAHEAD_DS_4x 0
#endif

#define QSV_COMMON_OPTS \
{ "async_depth", "Maximum processing parallelism", OFFSET(qsv.async_depth), AV_OPT_TYPE_INT, { .i64 = ASYNC_DEPTH_DEFAULT }, 1, INT_MAX, VE },                          \
{ "avbr_accuracy",    "Accuracy of the AVBR ratecontrol (unit of tenth of percent)",    OFFSET(qsv.avbr_accuracy),    AV_OPT_TYPE_INT, { .i64 = 1 }, 1, UINT16_MAX, VE }, \
{ "avbr_convergence", "Convergence of the AVBR ratecontrol (unit of 100 frames)", OFFSET(qsv.avbr_convergence), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, UINT16_MAX, VE },     \
{ "preset", NULL, OFFSET(qsv.preset), AV_OPT_TYPE_INT, { .i64 = MFX_TARGETUSAGE_BALANCED }, MFX_TARGETUSAGE_BEST_QUALITY, MFX_TARGETUSAGE_BEST_SPEED,   VE, "preset" }, \
{ "veryfast",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_BEST_SPEED  },   INT_MIN, INT_MAX, VE, "preset" },                                                \
{ "faster",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_6  },            INT_MIN, INT_MAX, VE, "preset" },                                                \
{ "fast",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_5  },            INT_MIN, INT_MAX, VE, "preset" },                                                \
{ "medium",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_BALANCED  },     INT_MIN, INT_MAX, VE, "preset" },                                                \
{ "slow",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_3  },            INT_MIN, INT_MAX, VE, "preset" },                                                \
{ "slower",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_2  },            INT_MIN, INT_MAX, VE, "preset" },                                                \
{ "veryslow",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = MFX_TARGETUSAGE_BEST_QUALITY  }, INT_MIN, INT_MAX, VE, "preset" },                                                \
{ "rdo",            "Enable rate distortion optimization",    OFFSET(qsv.rdo),            AV_OPT_TYPE_INT, { .i64 = -1 }, -1,          1, VE },                         \
{ "max_frame_size", "Maximum encoded frame size in bytes",    OFFSET(qsv.max_frame_size), AV_OPT_TYPE_INT, { .i64 = -1 }, -1,    INT_MAX, VE },                         \
{ "max_frame_size_i", "Maximum encoded I frame size in bytes",OFFSET(qsv.max_frame_size_i), AV_OPT_TYPE_INT, { .i64 = -1 }, -1,  INT_MAX, VE },                         \
{ "max_frame_size_p", "Maximum encoded P frame size in bytes",OFFSET(qsv.max_frame_size_p), AV_OPT_TYPE_INT, { .i64 = -1 }, -1,  INT_MAX, VE },                         \
{ "max_slice_size", "Maximum encoded slice size in bytes",    OFFSET(qsv.max_slice_size), AV_OPT_TYPE_INT, { .i64 = -1 }, -1,    INT_MAX, VE },                         \
{ "bitrate_limit",  "Toggle bitrate limitations",             OFFSET(qsv.bitrate_limit),  AV_OPT_TYPE_INT, { .i64 = -1 }, -1,          1, VE },                         \
{ "mbbrc",          "MB level bitrate control",               OFFSET(qsv.mbbrc),          AV_OPT_TYPE_INT, { .i64 = -1 }, -1,          1, VE },                         \
{ "extbrc",         "Extended bitrate control",               OFFSET(qsv.extbrc),         AV_OPT_TYPE_INT, { .i64 = -1 }, -1,          1, VE },                         \
{ "adaptive_i",     "Adaptive I-frame placement",             OFFSET(qsv.adaptive_i),     AV_OPT_TYPE_INT, { .i64 = -1 }, -1,          1, VE },                         \
{ "adaptive_b",     "Adaptive B-frame placement",             OFFSET(qsv.adaptive_b),     AV_OPT_TYPE_INT, { .i64 = -1 }, -1,          1, VE },                         \
{ "p_strategy",     "Enable P-pyramid: 0-default 1-simple 2-pyramid(bf need to be set to 0).",    OFFSET(qsv.p_strategy), AV_OPT_TYPE_INT,    { .i64 = 0}, 0,    2, VE },                         \
{ "b_strategy",     "Strategy to choose between I/P/B-frames", OFFSET(qsv.b_strategy),    AV_OPT_TYPE_INT, { .i64 = -1 }, -1,          1, VE },                         \
{ "forced_idr",     "Forcing I frames as IDR frames",         OFFSET(qsv.forced_idr),     AV_OPT_TYPE_BOOL,{ .i64 = 0  },  0,          1, VE },                         \
{ "low_power", "enable low power mode(experimental: many limitations by mfx version, BRC modes, etc.)", OFFSET(qsv.low_power), AV_OPT_TYPE_BOOL, { .i64 = -1}, -1, 1, VE},\
{ "dblk_idc", "This option disable deblocking. It has value in range 0~2.",   OFFSET(qsv.dblk_idc),   AV_OPT_TYPE_INT,    { .i64 = 0 },   0,  2,  VE},    \
{ "low_delay_brc",   "Allow to strictly obey avg frame size", OFFSET(qsv.low_delay_brc),  AV_OPT_TYPE_BOOL,{ .i64 = -1 }, -1,          1, VE },                         \

extern const AVCodecHWConfigInternal *const ff_qsv_enc_hw_configs[];

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
#if QSV_HAVE_CO3
    mfxExtCodingOption3 extco3;
#endif
#if QSV_HAVE_MF
    mfxExtMultiFrameParam   extmfp;
    mfxExtMultiFrameControl extmfc;
#endif
#if QSV_HAVE_EXT_HEVC_TILES
    mfxExtHEVCTiles exthevctiles;
#endif
#if QSV_HAVE_EXT_VP9_PARAM
    mfxExtVP9Param  extvp9param;
#endif

    mfxExtOpaqueSurfaceAlloc opaque_alloc;
    mfxFrameSurface1       **opaque_surfaces;
    AVBufferRef             *opaque_alloc_buf;

    mfxExtVideoSignalInfo extvsi;

    mfxExtBuffer  *extparam_internal[3 + QSV_HAVE_CO2 + QSV_HAVE_CO3 + (QSV_HAVE_MF * 2)];
    int         nb_extparam_internal;

    mfxExtBuffer **extparam;

    AVFifo *async_fifo;

    QSVFramesContext frames_ctx;

    mfxVersion          ver;

    int hevc_vps;

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
    int max_frame_size_i;
    int max_frame_size_p;
    int max_slice_size;
    int dblk_idc;

    int tile_cols;
    int tile_rows;

    int aud;

    int single_sei_nal_unit;
    int max_dec_frame_buffering;

    int bitrate_limit;
    int mbbrc;
    int extbrc;
    int adaptive_i;
    int adaptive_b;
    int b_strategy;
    int p_strategy;
    int cavlc;

    int int_ref_type;
    int int_ref_cycle_size;
    int int_ref_qp_delta;
    int int_ref_cycle_dist;
    int recovery_point_sei;

    int repeat_pps;
    int low_power;
    int gpb;
    int transform_skip;

    int a53_cc;

#if QSV_HAVE_MF
    int mfmode;
#endif
    char *load_plugins;
    SetEncodeCtrlCB *set_encode_ctrl_cb;
    int forced_idr;
    int low_delay_brc;
} QSVEncContext;

int ff_qsv_enc_init(AVCodecContext *avctx, QSVEncContext *q);

int ff_qsv_encode(AVCodecContext *avctx, QSVEncContext *q,
                  AVPacket *pkt, const AVFrame *frame, int *got_packet);

int ff_qsv_enc_close(AVCodecContext *avctx, QSVEncContext *q);

#endif /* AVCODEC_QSVENC_H */
