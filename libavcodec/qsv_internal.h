/*
 * Intel MediaSDK QSV encoder/decoder shared code
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

#ifndef AVCODEC_QSV_INTERNAL_H
#define AVCODEC_QSV_INTERNAL_H

#include <mfx/mfxvideo.h>

#include "libavutil/frame.h"

#include "avcodec.h"

#define QSV_VERSION_MAJOR 1
#define QSV_VERSION_MINOR 1

#define ASYNC_DEPTH_DEFAULT 4       // internal parallelism

#define QSV_MAX_ENC_PAYLOAD 2       // # of mfxEncodeCtrl payloads supported

#define QSV_VERSION_ATLEAST(MAJOR, MINOR)   \
    (MFX_VERSION_MAJOR > (MAJOR) ||         \
     MFX_VERSION_MAJOR == (MAJOR) && MFX_VERSION_MINOR >= (MINOR))

#define QSV_RUNTIME_VERSION_ATLEAST(MFX_VERSION, MAJOR, MINOR) \
    ((MFX_VERSION.Major > (MAJOR)) ||                           \
    (MFX_VERSION.Major == (MAJOR) && MFX_VERSION.Minor >= (MINOR)))

typedef struct QSVMid {
    AVBufferRef *hw_frames_ref;
    mfxHDL handle;

    AVFrame *locked_frame;
    AVFrame *hw_frame;
    mfxFrameSurface1 surf;
} QSVMid;

typedef struct QSVFrame {
    AVFrame *frame;
    mfxFrameSurface1 surface;
    mfxEncodeCtrl enc_ctrl;
    mfxExtDecodedFrameInfo dec_info;
    mfxExtBuffer *ext_param;

    int queued;
    int used;

    struct QSVFrame *next;
} QSVFrame;

typedef struct QSVFramesContext {
    AVBufferRef *hw_frames_ctx;
    void *logctx;

    /* The memory ids for the external frames.
     * Refcounted, since we need one reference owned by the QSVFramesContext
     * (i.e. by the encoder/decoder) and another one given to the MFX session
     * from the frame allocator. */
    AVBufferRef *mids_buf;
    QSVMid *mids;
    int  nb_mids;
} QSVFramesContext;

/**
 * Convert a libmfx error code into an ffmpeg error code.
 */
int ff_qsv_map_error(mfxStatus mfx_err, const char **desc);

int ff_qsv_print_error(void *log_ctx, mfxStatus err,
                       const char *error_string);

int ff_qsv_print_warning(void *log_ctx, mfxStatus err,
                         const char *warning_string);

int ff_qsv_codec_id_to_mfx(enum AVCodecID codec_id);
int ff_qsv_profile_to_mfx(enum AVCodecID codec_id, int profile);
int ff_qsv_level_to_mfx(enum AVCodecID codec_id, int level);

enum AVPixelFormat ff_qsv_map_fourcc(uint32_t fourcc);

int ff_qsv_map_pixfmt(enum AVPixelFormat format, uint32_t *fourcc);
enum AVPictureType ff_qsv_map_pictype(int mfx_pic_type);

enum AVFieldOrder ff_qsv_map_picstruct(int mfx_pic_struct);

int ff_qsv_init_internal_session(AVCodecContext *avctx, mfxSession *session,
                                 const char *load_plugins);

int ff_qsv_init_session_device(AVCodecContext *avctx, mfxSession *psession,
                               AVBufferRef *device_ref, const char *load_plugins);

int ff_qsv_init_session_frames(AVCodecContext *avctx, mfxSession *session,
                               QSVFramesContext *qsv_frames_ctx,
                               const char *load_plugins, int opaque);

int ff_qsv_find_surface_idx(QSVFramesContext *ctx, QSVFrame *frame);

#endif /* AVCODEC_QSV_INTERNAL_H */
