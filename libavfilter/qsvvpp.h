/*
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
 * Intel Quick Sync Video VPP base function
 */

#ifndef AVFILTER_QSVVPP_H
#define AVFILTER_QSVVPP_H

#include <mfx/mfxvideo.h>

#include "avfilter.h"
#include "libavutil/fifo.h"

#define FF_INLINK_IDX(link)  ((int)((link)->dstpad - (link)->dst->input_pads))
#define FF_OUTLINK_IDX(link) ((int)((link)->srcpad - (link)->src->output_pads))

#define QSV_VERSION_ATLEAST(MAJOR, MINOR)   \
    (MFX_VERSION_MAJOR > (MAJOR) ||         \
     MFX_VERSION_MAJOR == (MAJOR) && MFX_VERSION_MINOR >= (MINOR))

#define QSV_RUNTIME_VERSION_ATLEAST(MFX_VERSION, MAJOR, MINOR) \
    ((MFX_VERSION.Major > (MAJOR)) ||                           \
    (MFX_VERSION.Major == (MAJOR) && MFX_VERSION.Minor >= (MINOR)))

typedef struct QSVFrame {
    AVFrame          *frame;
    mfxFrameSurface1 surface;
    struct QSVFrame  *next;
    int queued;
} QSVFrame;

typedef struct QSVVPPContext {
    mfxSession          session;
    int (*filter_frame) (AVFilterLink *outlink, AVFrame *frame); /**< callback */
    enum AVPixelFormat  out_sw_format;   /**< Real output format */
    mfxVideoParam       vpp_param;
    mfxFrameInfo       *frame_infos;     /**< frame info for each input */

    /** members related to the input/output surface */
    int                 in_mem_mode;
    int                 out_mem_mode;
    QSVFrame           *in_frame_list;
    QSVFrame           *out_frame_list;
    int                 nb_surface_ptrs_in;
    int                 nb_surface_ptrs_out;
    mfxFrameSurface1  **surface_ptrs_in;
    mfxFrameSurface1  **surface_ptrs_out;

    /** MFXVPP extern parameters */
    mfxExtOpaqueSurfaceAlloc opaque_alloc;
    mfxExtBuffer      **ext_buffers;
    int                 nb_ext_buffers;

    int got_frame;
    int async_depth;
    int eof;
    /** order with frame_out, sync */
    AVFifo *async_fifo;
} QSVVPPContext;

typedef struct QSVVPPCrop {
    int in_idx;        ///< Input index
    int x, y, w, h;    ///< Crop rectangle
} QSVVPPCrop;

typedef struct QSVVPPParam {
    /* default is ff_filter_frame */
    int (*filter_frame)(AVFilterLink *outlink, AVFrame *frame);

    /* To fill with MFX enhanced filter configurations */
    int num_ext_buf;
    mfxExtBuffer **ext_buf;

    /* Real output format */
    enum AVPixelFormat out_sw_format;

    /* Crop information for each input, if needed */
    int num_crop;
    QSVVPPCrop *crop;

   int async_depth;
} QSVVPPParam;

/* create and initialize the QSV session */
int ff_qsvvpp_create(AVFilterContext *avctx, QSVVPPContext **vpp, QSVVPPParam *param);

/* release the resources (eg.surfaces) */
int ff_qsvvpp_free(QSVVPPContext **vpp);

/* vpp filter frame and call the cb if needed */
int ff_qsvvpp_filter_frame(QSVVPPContext *vpp, AVFilterLink *inlink, AVFrame *frame);

int ff_qsvvpp_print_iopattern(void *log_ctx, int mfx_iopattern,
                              const char *extra_string);

int ff_qsvvpp_print_error(void *log_ctx, mfxStatus err,
                          const char *error_string);

int ff_qsvvpp_print_warning(void *log_ctx, mfxStatus err,
                            const char *warning_string);

#endif /* AVFILTER_QSVVPP_H */
