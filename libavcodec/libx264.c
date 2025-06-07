/*
 * H.264 encoding using the x264 library
 * Copyright (C) 2005  Mans Rullgard <mans@mansr.com>
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

#include "config_components.h"

#include "libavutil/buffer.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/stereo3d.h"
#include "libavutil/time.h"
#include "libavutil/video_hint.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "internal.h"
#include "packet_internal.h"
#include "atsc_a53.h"
#include "sei.h"
#include "golomb.h"

#include <x264.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// from x264.h, for quant_offsets, Macroblocks are 16x16
// blocks of pixels (with respect to the luma plane)
#define MB_SIZE 16
#define MB_LSIZE 4
#define MB_FLOOR(x)      ((x) >> (MB_LSIZE))
#define MB_CEIL(x)       MB_FLOOR((x) + (MB_SIZE - 1))

typedef struct X264Opaque {
    int64_t wallclock;
    int64_t duration;

    void        *frame_opaque;
    AVBufferRef *frame_opaque_ref;
} X264Opaque;

typedef struct X264Context {
    AVClass        *class;
    x264_param_t    params;
    x264_t         *enc;
    x264_picture_t  pic;
    uint8_t        *sei;
    int             sei_size;
    char *preset;
    char *tune;
    const char *profile;
    char *profile_opt;
    char *level;
    int fastfirstpass;
    char *wpredp;
    char *x264opts;
    float crf;
    float crf_max;
    int cqp;
    int aq_mode;
    float aq_strength;
    char *psy_rd;
    int psy;
    int rc_lookahead;
    int weightp;
    int weightb;
    int ssim;
    int intra_refresh;
    int bluray_compat;
    int b_bias;
    int b_pyramid;
    int mixed_refs;
    int dct8x8;
    int fast_pskip;
    int aud;
    int mbtree;
    char *deblock;
    float cplxblur;
    char *partitions;
    int direct_pred;
    int slice_max_size;
    char *stats;
    int nal_hrd;
    int avcintra_class;
    int motion_est;
    int forced_idr;
    int coder;
    int a53_cc;
    int b_frame_strategy;
    int chroma_offset;
    int scenechange_threshold;
    int noise_reduction;
    int udu_sei;

    AVDictionary *x264_params;

    int nb_reordered_opaque, next_reordered_opaque;
    X264Opaque *reordered_opaque;

    /**
     * If the encoder does not support ROI then warn the first time we
     * encounter a frame with ROI side data.
     */
    int roi_warned;

    int mb_info;
} X264Context;

static void X264_log(void *p, int level, const char *fmt, va_list args)
{
    static const int level_map[] = {
        [X264_LOG_ERROR]   = AV_LOG_ERROR,
        [X264_LOG_WARNING] = AV_LOG_WARNING,
        [X264_LOG_INFO]    = AV_LOG_INFO,
        [X264_LOG_DEBUG]   = AV_LOG_DEBUG
    };

    if (level < 0 || level > X264_LOG_DEBUG)
        return;

    av_vlog(p, level_map[level], fmt, args);
}

static void opaque_uninit(X264Opaque *o)
{
    av_buffer_unref(&o->frame_opaque_ref);
    memset(o, 0, sizeof(*o));
}

static int encode_nals(AVCodecContext *ctx, AVPacket *pkt,
                       const x264_nal_t *nals, int nnal)
{
    X264Context *x4 = ctx->priv_data;
    uint8_t *p;
    uint64_t size = FFMAX(x4->sei_size, 0);
    int ret;

    if (!nnal)
        return 0;

    for (int i = 0; i < nnal; i++) {
        size += nals[i].i_payload;
        /* ff_get_encode_buffer() accepts an int64_t and
         * so we need to make sure that no overflow happens before
         * that. With 32bit ints this is automatically true. */
#if INT_MAX > INT64_MAX / INT_MAX - 1
        if ((int64_t)size < 0)
            return AVERROR(ERANGE);
#endif
    }

    if ((ret = ff_get_encode_buffer(ctx, pkt, size, 0)) < 0)
        return ret;

    p = pkt->data;

    /* Write the SEI as part of the first frame. */
    if (x4->sei_size > 0) {
        memcpy(p, x4->sei, x4->sei_size);
        p += x4->sei_size;
        size -= x4->sei_size;
        /* Keep the value around in case of flush */
        x4->sei_size = -x4->sei_size;
    }

    /* x264 guarantees the payloads of the NALs
     * to be sequential in memory. */
    memcpy(p, nals[0].p_payload, size);

    return 1;
}

static void reconfig_encoder(AVCodecContext *ctx, const AVFrame *frame)
{
    X264Context *x4 = ctx->priv_data;
    AVFrameSideData *side_data;


    if (x4->avcintra_class < 0) {
        if (x4->params.b_interlaced && x4->params.b_tff != !!(frame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST)) {

            x4->params.b_tff = !!(frame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST);
            x264_encoder_reconfig(x4->enc, &x4->params);
        }
        if (x4->params.vui.i_sar_height*ctx->sample_aspect_ratio.num != ctx->sample_aspect_ratio.den * x4->params.vui.i_sar_width) {
            x4->params.vui.i_sar_height = ctx->sample_aspect_ratio.den;
            x4->params.vui.i_sar_width  = ctx->sample_aspect_ratio.num;
            x264_encoder_reconfig(x4->enc, &x4->params);
        }

        if (x4->params.rc.i_vbv_buffer_size != ctx->rc_buffer_size / 1000 ||
            x4->params.rc.i_vbv_max_bitrate != ctx->rc_max_rate    / 1000) {
            x4->params.rc.i_vbv_buffer_size = ctx->rc_buffer_size / 1000;
            x4->params.rc.i_vbv_max_bitrate = ctx->rc_max_rate    / 1000;
            x264_encoder_reconfig(x4->enc, &x4->params);
        }

        if (x4->params.rc.i_rc_method == X264_RC_ABR &&
            x4->params.rc.i_bitrate != ctx->bit_rate / 1000) {
            x4->params.rc.i_bitrate = ctx->bit_rate / 1000;
            x264_encoder_reconfig(x4->enc, &x4->params);
        }

        if (x4->crf >= 0 &&
            x4->params.rc.i_rc_method == X264_RC_CRF &&
            x4->params.rc.f_rf_constant != x4->crf) {
            x4->params.rc.f_rf_constant = x4->crf;
            x264_encoder_reconfig(x4->enc, &x4->params);
        }

        if (x4->params.rc.i_rc_method == X264_RC_CQP &&
            x4->cqp >= 0 &&
            x4->params.rc.i_qp_constant != x4->cqp) {
            x4->params.rc.i_qp_constant = x4->cqp;
            x264_encoder_reconfig(x4->enc, &x4->params);
        }

        if (x4->crf_max >= 0 &&
            x4->params.rc.f_rf_constant_max != x4->crf_max) {
            x4->params.rc.f_rf_constant_max = x4->crf_max;
            x264_encoder_reconfig(x4->enc, &x4->params);
        }
    }

    side_data = av_frame_get_side_data(frame, AV_FRAME_DATA_STEREO3D);
    if (side_data) {
        AVStereo3D *stereo = (AVStereo3D *)side_data->data;
        int fpa_type;

        switch (stereo->type) {
        case AV_STEREO3D_CHECKERBOARD:
            fpa_type = 0;
            break;
        case AV_STEREO3D_COLUMNS:
            fpa_type = 1;
            break;
        case AV_STEREO3D_LINES:
            fpa_type = 2;
            break;
        case AV_STEREO3D_SIDEBYSIDE:
            fpa_type = 3;
            break;
        case AV_STEREO3D_TOPBOTTOM:
            fpa_type = 4;
            break;
        case AV_STEREO3D_FRAMESEQUENCE:
            fpa_type = 5;
            break;
        case AV_STEREO3D_2D:
            fpa_type = 6;
            break;
        default:
            fpa_type = -1;
            break;
        }

        /* Inverted mode is not supported by x264 */
        if (stereo->flags & AV_STEREO3D_FLAG_INVERT) {
            av_log(ctx, AV_LOG_WARNING,
                   "Ignoring unsupported inverted stereo value %d\n", fpa_type);
            fpa_type = -1;
        }

        if (fpa_type != x4->params.i_frame_packing) {
            x4->params.i_frame_packing = fpa_type;
            x264_encoder_reconfig(x4->enc, &x4->params);
        }
    }
}

static void free_picture(x264_picture_t *pic)
{
    for (int i = 0; i < pic->extra_sei.num_payloads; i++)
        av_free(pic->extra_sei.payloads[i].payload);
    av_freep(&pic->extra_sei.payloads);
    av_freep(&pic->prop.quant_offsets);
    av_freep(&pic->prop.mb_info);
    pic->extra_sei.num_payloads = 0;
}

static enum AVPixelFormat csp_to_pixfmt(int csp)
{
    switch (csp) {
#ifdef X264_CSP_I400
    case X264_CSP_I400:                         return AV_PIX_FMT_GRAY8;
    case X264_CSP_I400 | X264_CSP_HIGH_DEPTH:   return AV_PIX_FMT_GRAY10;
#endif
    case X264_CSP_I420:                         return AV_PIX_FMT_YUV420P;
    case X264_CSP_I420 | X264_CSP_HIGH_DEPTH:   return AV_PIX_FMT_YUV420P10;
    case X264_CSP_I422:                         return AV_PIX_FMT_YUV422P;
    case X264_CSP_I422 | X264_CSP_HIGH_DEPTH:   return AV_PIX_FMT_YUV422P10;
    case X264_CSP_I444:                         return AV_PIX_FMT_YUV444P;
    case X264_CSP_I444 | X264_CSP_HIGH_DEPTH:   return AV_PIX_FMT_YUV444P10;
    case X264_CSP_NV12:                         return AV_PIX_FMT_NV12;
#ifdef X264_CSP_NV21
    case X264_CSP_NV21:                         return AV_PIX_FMT_NV21;
#endif
    case X264_CSP_NV16:                         return AV_PIX_FMT_NV16;
    };
    return AV_PIX_FMT_NONE;
}

static void av_always_inline mbinfo_compute_changed_coords(const AVVideoRect *rect,
                                                           int *min_x,
                                                           int *max_x,
                                                           int *min_y,
                                                           int *max_y)
{
    *min_y = MB_FLOOR(rect->y);
    *max_y = MB_CEIL(rect->y + rect->height);
    *min_x = MB_FLOOR(rect->x);
    *max_x = MB_CEIL(rect->x + rect->width);
}

static void av_always_inline mbinfo_compute_constant_coords(const AVVideoRect *rect,
                                                            int *min_x,
                                                            int *max_x,
                                                            int *min_y,
                                                            int *max_y)
{
    *min_y = MB_CEIL(rect->y);
    *max_y = MB_FLOOR(rect->y + rect->height);
    *min_x = MB_CEIL(rect->x);
    *max_x = MB_FLOOR(rect->x + rect->width);
}

static int setup_mb_info(AVCodecContext *ctx, x264_picture_t *pic,
                         const AVFrame *frame,
                         const AVVideoHint *info)
{
    int mb_width = (frame->width + MB_SIZE - 1) / MB_SIZE;
    int mb_height = (frame->height + MB_SIZE - 1) / MB_SIZE;

    const AVVideoRect *mbinfo_rects;
    int nb_rects;
    uint8_t *mbinfo;

    mbinfo_rects = (const AVVideoRect *)av_video_hint_rects(info);
    nb_rects = info->nb_rects;

    mbinfo = av_calloc(mb_width * mb_height, sizeof(*mbinfo));
    if (!mbinfo)
        return AVERROR(ENOMEM);

#define COMPUTE_MBINFO(mbinfo_filler_, mbinfo_marker_, compute_coords_fn_) \
    memset(mbinfo, mbinfo_filler_, sizeof(*mbinfo) * mb_width * mb_height); \
                                                                        \
    for (int i = 0; i < nb_rects; i++) {                                \
        int min_x, max_x, min_y, max_y;                                 \
                                                                        \
        compute_coords_fn_(mbinfo_rects, &min_x, &max_x, &min_y, &max_y); \
        for (int mb_y = min_y; mb_y < max_y; ++mb_y) {                  \
            memset(mbinfo + mb_y * mb_width + min_x, mbinfo_marker_, max_x - min_x); \
        }                                                               \
                                                                        \
        mbinfo_rects++;                                                 \
    }                                                                   \

    if (info->type == AV_VIDEO_HINT_TYPE_CHANGED) {
        COMPUTE_MBINFO(X264_MBINFO_CONSTANT, 0, mbinfo_compute_changed_coords);
    } else /* if (info->type == AV_VIDEO_HINT_TYPE_CHANGED) */ {
        COMPUTE_MBINFO(0, X264_MBINFO_CONSTANT, mbinfo_compute_constant_coords);
    }

    pic->prop.mb_info = mbinfo;
    pic->prop.mb_info_free = av_free;

    return 0;
}

static int setup_roi(AVCodecContext *ctx, x264_picture_t *pic,
                     const AVFrame *frame, const uint8_t *data, size_t size)
{
    X264Context *x4 = ctx->priv_data;

    int mbx = (frame->width + MB_SIZE - 1) / MB_SIZE;
    int mby = (frame->height + MB_SIZE - 1) / MB_SIZE;
    int qp_range = 51 + 6 * (x4->params.i_bitdepth - 8);
    int nb_rois;
    const AVRegionOfInterest *roi;
    uint32_t roi_size;
    float *qoffsets;

    if (x4->params.rc.i_aq_mode == X264_AQ_NONE) {
        if (!x4->roi_warned) {
            x4->roi_warned = 1;
            av_log(ctx, AV_LOG_WARNING, "Adaptive quantization must be enabled to use ROI encoding, skipping ROI.\n");
        }
        return 0;
    } else if (frame->flags & AV_FRAME_FLAG_INTERLACED) {
        if (!x4->roi_warned) {
            x4->roi_warned = 1;
            av_log(ctx, AV_LOG_WARNING, "interlaced_frame not supported for ROI encoding yet, skipping ROI.\n");
        }
        return 0;
    }

    roi = (const AVRegionOfInterest*)data;
    roi_size = roi->self_size;
    if (!roi_size || size % roi_size != 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid AVRegionOfInterest.self_size.\n");
        return AVERROR(EINVAL);
    }
    nb_rois = size / roi_size;

    qoffsets = av_calloc(mbx * mby, sizeof(*qoffsets));
    if (!qoffsets)
        return AVERROR(ENOMEM);

    // This list must be iterated in reverse because the first
    // region in the list applies when regions overlap.
    for (int i = nb_rois - 1; i >= 0; i--) {
        int startx, endx, starty, endy;
        float qoffset;

        roi = (const AVRegionOfInterest*)(data + roi_size * i);

        starty = FFMIN(mby, roi->top / MB_SIZE);
        endy   = FFMIN(mby, (roi->bottom + MB_SIZE - 1)/ MB_SIZE);
        startx = FFMIN(mbx, roi->left / MB_SIZE);
        endx   = FFMIN(mbx, (roi->right + MB_SIZE - 1)/ MB_SIZE);

        if (roi->qoffset.den == 0) {
            av_free(qoffsets);
            av_log(ctx, AV_LOG_ERROR, "AVRegionOfInterest.qoffset.den must not be zero.\n");
            return AVERROR(EINVAL);
        }
        qoffset = roi->qoffset.num * 1.0f / roi->qoffset.den;
        qoffset = av_clipf(qoffset * qp_range, -qp_range, +qp_range);

        for (int y = starty; y < endy; y++) {
            for (int x = startx; x < endx; x++) {
                qoffsets[x + y*mbx] = qoffset;
            }
        }
    }

    pic->prop.quant_offsets = qoffsets;
    pic->prop.quant_offsets_free = av_free;

    return 0;
}

static int setup_frame(AVCodecContext *ctx, const AVFrame *frame,
                       x264_picture_t **ppic)
{
    X264Context *x4 = ctx->priv_data;
    X264Opaque  *opaque = &x4->reordered_opaque[x4->next_reordered_opaque];
    x264_picture_t *pic = &x4->pic;
    x264_sei_t     *sei = &pic->extra_sei;
    unsigned int sei_data_size = 0;
    int64_t wallclock = 0;
    int ret;
    AVFrameSideData *sd;
    AVFrameSideData *mbinfo_sd;

    *ppic = NULL;
    if (!frame)
        return 0;

    x264_picture_init(pic);
    pic->img.i_csp   = x4->params.i_csp;
    if (x4->params.i_bitdepth > 8)
        pic->img.i_csp |= X264_CSP_HIGH_DEPTH;
    pic->img.i_plane = av_pix_fmt_count_planes(ctx->pix_fmt);

    for (int i = 0; i < pic->img.i_plane; i++) {
        pic->img.plane[i]    = frame->data[i];
        pic->img.i_stride[i] = frame->linesize[i];
    }

    pic->i_pts  = frame->pts;

    opaque_uninit(opaque);

    if (ctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
        opaque->frame_opaque = frame->opaque;
        ret = av_buffer_replace(&opaque->frame_opaque_ref, frame->opaque_ref);
        if (ret < 0)
            goto fail;
    }

    opaque->duration         = frame->duration;
    opaque->wallclock = wallclock;
    if (ctx->export_side_data & AV_CODEC_EXPORT_DATA_PRFT)
        opaque->wallclock = av_gettime();

    pic->opaque = opaque;

    x4->next_reordered_opaque++;
    x4->next_reordered_opaque %= x4->nb_reordered_opaque;

    switch (frame->pict_type) {
    case AV_PICTURE_TYPE_I:
        pic->i_type = x4->forced_idr > 0 ? X264_TYPE_IDR : X264_TYPE_KEYFRAME;
        break;
    case AV_PICTURE_TYPE_P:
        pic->i_type = X264_TYPE_P;
        break;
    case AV_PICTURE_TYPE_B:
        pic->i_type = X264_TYPE_B;
        break;
    default:
        pic->i_type = X264_TYPE_AUTO;
        break;
    }
    reconfig_encoder(ctx, frame);

    if (x4->a53_cc) {
        void *sei_data;
        size_t sei_size;

        ret = ff_alloc_a53_sei(frame, 0, &sei_data, &sei_size);
        if (ret < 0)
            goto fail;

        if (sei_data) {
            sei->payloads = av_mallocz(sizeof(sei->payloads[0]));
            if (!sei->payloads) {
                av_free(sei_data);
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            sei->sei_free = av_free;

            sei->payloads[0].payload_size = sei_size;
            sei->payloads[0].payload      = sei_data;
            sei->payloads[0].payload_type = SEI_TYPE_USER_DATA_REGISTERED_ITU_T_T35;
            sei->num_payloads = 1;
        }
    }

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_REGIONS_OF_INTEREST);
    if (sd) {
        ret = setup_roi(ctx, pic, frame, sd->data, sd->size);
        if (ret < 0)
            goto fail;
    }

    mbinfo_sd = av_frame_get_side_data(frame, AV_FRAME_DATA_VIDEO_HINT);
    if (mbinfo_sd) {
        int err = setup_mb_info(ctx, pic, frame, (const AVVideoHint *)mbinfo_sd->data);
        if (err < 0) {
            /* No need to fail here, this is not fatal. We just proceed with no
             * mb_info and log a message */

            av_log(ctx, AV_LOG_WARNING, "setup_mb_info failed with error: %s\n", av_err2str(err));
        }
    }

    if (x4->udu_sei) {
        for (int j = 0; j < frame->nb_side_data; j++) {
            AVFrameSideData *side_data = frame->side_data[j];
            void *tmp;
            x264_sei_payload_t *sei_payload;
            if (side_data->type != AV_FRAME_DATA_SEI_UNREGISTERED)
                continue;
            tmp = av_fast_realloc(sei->payloads, &sei_data_size, (sei->num_payloads + 1) * sizeof(*sei_payload));
            if (!tmp) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            sei->payloads = tmp;
            sei->sei_free = av_free;
            sei_payload = &sei->payloads[sei->num_payloads];
            sei_payload->payload = av_memdup(side_data->data, side_data->size);
            if (!sei_payload->payload) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            sei_payload->payload_size = side_data->size;
            sei_payload->payload_type = SEI_TYPE_USER_DATA_UNREGISTERED;
            sei->num_payloads++;
        }
    }

    *ppic = pic;
    return 0;

fail:
    free_picture(pic);
    *ppic = NULL;
    return ret;
}

static int X264_frame(AVCodecContext *ctx, AVPacket *pkt, const AVFrame *frame,
                      int *got_packet)
{
    X264Context *x4 = ctx->priv_data;
    x264_nal_t *nal;
    int nnal, ret;
    x264_picture_t pic_out = {0}, *pic_in;
    int pict_type;
    int64_t wallclock = 0;
    X264Opaque *out_opaque;

    ret = setup_frame(ctx, frame, &pic_in);
    if (ret < 0)
        return ret;

    do {
        if (x264_encoder_encode(x4->enc, &nal, &nnal, pic_in, &pic_out) < 0)
            return AVERROR_EXTERNAL;

        if (nnal && (ctx->flags & AV_CODEC_FLAG_RECON_FRAME)) {
            AVCodecInternal *avci = ctx->internal;

            av_frame_unref(avci->recon_frame);

            avci->recon_frame->format = csp_to_pixfmt(pic_out.img.i_csp);
            if (avci->recon_frame->format == AV_PIX_FMT_NONE) {
                av_log(ctx, AV_LOG_ERROR,
                       "Unhandled reconstructed frame colorspace: %d\n",
                       pic_out.img.i_csp);
                return AVERROR(ENOSYS);
            }

            avci->recon_frame->width  = ctx->width;
            avci->recon_frame->height = ctx->height;
            for (int i = 0; i < pic_out.img.i_plane; i++) {
                avci->recon_frame->data[i]     = pic_out.img.plane[i];
                avci->recon_frame->linesize[i] = pic_out.img.i_stride[i];
            }

            ret = av_frame_make_writable(avci->recon_frame);
            if (ret < 0) {
                av_frame_unref(avci->recon_frame);
                return ret;
            }
        }

        ret = encode_nals(ctx, pkt, nal, nnal);
        if (ret < 0)
            return ret;
    } while (!ret && !frame && x264_encoder_delayed_frames(x4->enc));

    if (!ret)
        return 0;

    pkt->pts = pic_out.i_pts;
    pkt->dts = pic_out.i_dts;

    out_opaque = pic_out.opaque;
    if (out_opaque >= x4->reordered_opaque &&
        out_opaque < &x4->reordered_opaque[x4->nb_reordered_opaque]) {
        wallclock = out_opaque->wallclock;
        pkt->duration = out_opaque->duration;

        if (ctx->flags & AV_CODEC_FLAG_COPY_OPAQUE) {
            pkt->opaque                  = out_opaque->frame_opaque;
            pkt->opaque_ref              = out_opaque->frame_opaque_ref;
            out_opaque->frame_opaque_ref = NULL;
        }

        opaque_uninit(out_opaque);
    } else {
        // Unexpected opaque pointer on picture output
        av_log(ctx, AV_LOG_ERROR, "Unexpected opaque pointer; "
               "this is a bug, please report it.\n");
    }

    switch (pic_out.i_type) {
    case X264_TYPE_IDR:
    case X264_TYPE_I:
        pict_type = AV_PICTURE_TYPE_I;
        break;
    case X264_TYPE_P:
        pict_type = AV_PICTURE_TYPE_P;
        break;
    case X264_TYPE_B:
    case X264_TYPE_BREF:
        pict_type = AV_PICTURE_TYPE_B;
        break;
    default:
        av_log(ctx, AV_LOG_ERROR, "Unknown picture type encountered.\n");
        return AVERROR_EXTERNAL;
    }

    pkt->flags |= AV_PKT_FLAG_KEY*pic_out.b_keyframe;
    if (ret) {
        int error_count = 0;
        int64_t *errors = NULL;
        int64_t sse[3] = {0};

        if (ctx->flags & AV_CODEC_FLAG_PSNR) {
            const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(ctx->pix_fmt);
            double scale[3] = { 1,
                (double)(1 << pix_desc->log2_chroma_h) * (1 << pix_desc->log2_chroma_w),
                (double)(1 << pix_desc->log2_chroma_h) * (1 << pix_desc->log2_chroma_w),
            };

            error_count = pix_desc->nb_components;

            for (int i = 0; i < pix_desc->nb_components; ++i) {
                double max_value = (double)(1 << pix_desc->comp[i].depth) - 1.0;
                double plane_size = ctx->width * (double)ctx->height / scale[i];

                /* psnr = 10 * log10(max_value * max_value / mse) */
                double mse = (max_value * max_value) / pow(10, pic_out.prop.f_psnr[i] / 10.0);

                /* SSE = MSE * width * height / scale -> because of possible chroma downsampling */
                sse[i] = (int64_t)floor(mse * plane_size + .5);
            }

            errors = sse;
        }

        ff_side_data_set_encoder_stats(pkt, (pic_out.i_qpplus1 - 1) * FF_QP2LAMBDA,
                                       errors, error_count, pict_type);

        if (wallclock)
            ff_side_data_set_prft(pkt, wallclock);
    }

    *got_packet = ret;
    return 0;
}

static void X264_flush(AVCodecContext *avctx)
{
    X264Context *x4 = avctx->priv_data;
    x264_nal_t *nal;
    int nnal, ret;
    x264_picture_t pic_out = {0};

    do {
        ret = x264_encoder_encode(x4->enc, &nal, &nnal, NULL, &pic_out);
    } while (ret > 0 && x264_encoder_delayed_frames(x4->enc));

    for (int i = 0; i < x4->nb_reordered_opaque; i++)
        opaque_uninit(&x4->reordered_opaque[i]);

    if (x4->sei_size < 0)
        x4->sei_size = -x4->sei_size;
}

static av_cold int X264_close(AVCodecContext *avctx)
{
    X264Context *x4 = avctx->priv_data;

    av_freep(&x4->sei);

    for (int i = 0; i < x4->nb_reordered_opaque; i++)
        opaque_uninit(&x4->reordered_opaque[i]);
    av_freep(&x4->reordered_opaque);

#if X264_BUILD >= 161
    x264_param_cleanup(&x4->params);
#endif

    if (x4->enc) {
        x264_encoder_close(x4->enc);
        x4->enc = NULL;
    }

    return 0;
}

static int parse_opts(AVCodecContext *avctx, const char *opt, const char *param)
{
    X264Context *x4 = avctx->priv_data;
    int ret;

    if ((ret = x264_param_parse(&x4->params, opt, param)) < 0) {
        if (ret == X264_PARAM_BAD_NAME) {
            av_log(avctx, AV_LOG_ERROR,
                   "bad option '%s': '%s'\n", opt, param);
            ret = AVERROR(EINVAL);
#if X264_BUILD >= 161
        } else if (ret == X264_PARAM_ALLOC_FAILED) {
            av_log(avctx, AV_LOG_ERROR,
                   "out of memory parsing option '%s': '%s'\n", opt, param);
            ret = AVERROR(ENOMEM);
#endif
        } else {
            av_log(avctx, AV_LOG_ERROR,
                   "bad value for '%s': '%s'\n", opt, param);
            ret = AVERROR(EINVAL);
        }
    }

    return ret;
}

static int convert_pix_fmt(enum AVPixelFormat pix_fmt)
{
    switch (pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUV420P9:
    case AV_PIX_FMT_YUV420P10: return X264_CSP_I420;
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUV422P10: return X264_CSP_I422;
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVJ444P:
    case AV_PIX_FMT_YUV444P9:
    case AV_PIX_FMT_YUV444P10: return X264_CSP_I444;
    case AV_PIX_FMT_BGR0:
        return X264_CSP_BGRA;
    case AV_PIX_FMT_BGR24:
        return X264_CSP_BGR;

    case AV_PIX_FMT_RGB24:
        return X264_CSP_RGB;
    case AV_PIX_FMT_NV12:      return X264_CSP_NV12;
    case AV_PIX_FMT_NV16:
    case AV_PIX_FMT_NV20:      return X264_CSP_NV16;
#ifdef X264_CSP_NV21
    case AV_PIX_FMT_NV21:      return X264_CSP_NV21;
#endif
#ifdef X264_CSP_I400
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_GRAY10:    return X264_CSP_I400;
#endif
    };
    return 0;
}

static int save_sei(AVCodecContext *avctx, x264_nal_t *nal)
{
    X264Context *x4 = avctx->priv_data;

    av_log(avctx, AV_LOG_INFO, "%s\n", nal->p_payload + 25);
    x4->sei_size = nal->i_payload;
    x4->sei = av_malloc(x4->sei_size);
    if (!x4->sei)
        return AVERROR(ENOMEM);

    memcpy(x4->sei, nal->p_payload, nal->i_payload);

    return 0;
}

#if CONFIG_LIBX264_ENCODER
static int set_avcc_extradata(AVCodecContext *avctx, x264_nal_t *nal, int nnal)
{
    x264_nal_t *sps_nal = NULL;
    x264_nal_t *pps_nal = NULL;
    uint8_t *p, *sps;
    int ret;

    /* We know it's in the order of SPS/PPS/SEI, but it's not documented in x264 API.
     * The x264 param i_sps_id implies there is a single pair of SPS/PPS.
     */
    for (int i = 0; i < nnal; i++) {
        switch (nal[i].i_type) {
        case NAL_SPS:
            sps_nal = &nal[i];
            break;
        case NAL_PPS:
            pps_nal = &nal[i];
            break;
        case NAL_SEI:
            ret = save_sei(avctx, &nal[i]);
            if (ret < 0)
                return ret;
            break;
        }
    }
    if (!sps_nal || !pps_nal)
        return AVERROR_EXTERNAL;

    avctx->extradata_size = sps_nal->i_payload + pps_nal->i_payload + 7;
    avctx->extradata = av_mallocz(avctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);

    // Now create AVCDecoderConfigurationRecord
    p = avctx->extradata;
    // Skip size part
    sps = sps_nal->p_payload + 4;
    *p++ = 1; // version
    *p++ = sps[1]; // AVCProfileIndication
    *p++ = sps[2]; // profile_compatibility
    *p++ = sps[3]; // AVCLevelIndication
    *p++ = 0xFF;
    *p++ = 0xE0 | 0x01; // 3 bits reserved (111) + 5 bits number of sps
    memcpy(p, sps_nal->p_payload + 2, sps_nal->i_payload - 2);
    // Make sps has AV_INPUT_BUFFER_PADDING_SIZE padding, so it can be used
    // with GetBitContext
    sps = p + 2;
    p += sps_nal->i_payload - 2;
    *p++ = 1;
    memcpy(p, pps_nal->p_payload + 2, pps_nal->i_payload - 2);
    p += pps_nal->i_payload - 2;

    if (sps[3] != 66 && sps[3] != 77 && sps[3] != 88) {
        GetBitContext gbc;
        int chroma_format_idc;
        int bit_depth_luma_minus8, bit_depth_chroma_minus8;

        /* It's not possible to have emulation prevention byte before
         * bit_depth_chroma_minus8 due to the range of sps id, chroma_format_idc
         * and so on. So we can read directly without need to escape emulation
         * prevention byte.
         *
         * +4 to skip until sps id.
         */
        ret = init_get_bits8(&gbc, sps + 4, sps_nal->i_payload - 4 - 4);
        if (ret < 0)
            return ret;
        // Skip sps id
        get_ue_golomb_31(&gbc);
        chroma_format_idc = get_ue_golomb_31(&gbc);
        if (chroma_format_idc == 3)
            skip_bits1(&gbc);
        bit_depth_luma_minus8 = get_ue_golomb_31(&gbc);
        bit_depth_chroma_minus8 = get_ue_golomb_31(&gbc);

        *p++ = 0xFC | chroma_format_idc;
        *p++ = 0xF8 | bit_depth_luma_minus8;
        *p++ = 0xF8 | bit_depth_chroma_minus8;
        *p++ = 0;
    }
    av_assert2(avctx->extradata + avctx->extradata_size >= p);
    avctx->extradata_size = p - avctx->extradata;

    return 0;
}
#endif

static int set_extradata(AVCodecContext *avctx)
{
    X264Context *x4 = avctx->priv_data;
    x264_nal_t *nal;
    uint8_t *p;
    int nnal, s;

    s = x264_encoder_headers(x4->enc, &nal, &nnal);
    if (s < 0)
        return AVERROR_EXTERNAL;

#if CONFIG_LIBX264_ENCODER
    if (!x4->params.b_annexb)
        return set_avcc_extradata(avctx, nal, nnal);
#endif

    avctx->extradata = p = av_mallocz(s + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!p)
        return AVERROR(ENOMEM);

    for (int i = 0; i < nnal; i++) {
        /* Don't put the SEI in extradata. */
        if (nal[i].i_type == NAL_SEI) {
            s = save_sei(avctx, &nal[i]);
            if (s < 0)
                return s;
            continue;
        }
        memcpy(p, nal[i].p_payload, nal[i].i_payload);
        p += nal[i].i_payload;
    }
    avctx->extradata_size = p - avctx->extradata;

    return 0;
}

#define PARSE_X264_OPT(name, var)\
    if (x4->var && x264_param_parse(&x4->params, name, x4->var) < 0) {\
        av_log(avctx, AV_LOG_ERROR, "Error parsing option '%s' with value '%s'.\n", name, x4->var);\
        return AVERROR(EINVAL);\
    }

#if CONFIG_LIBX264_HDR10
static void handle_mdcv(x264_param_t *params,
                        const AVMasteringDisplayMetadata *mdcv)
{
    if (!mdcv->has_primaries && !mdcv->has_luminance)
        return;

    params->mastering_display.b_mastering_display = 1;

    if (mdcv->has_primaries) {
        int *const points[][2] = {
            {
                &params->mastering_display.i_red_x,
                &params->mastering_display.i_red_y
            },
            {
                &params->mastering_display.i_green_x,
                &params->mastering_display.i_green_y
            },
            {
                &params->mastering_display.i_blue_x,
                &params->mastering_display.i_blue_y
            },
        };

        for (int i = 0; i < 3; i++) {
            const AVRational *src = mdcv->display_primaries[i];
            int *dst[2] = { points[i][0], points[i][1] };

            *dst[0] = av_rescale_q(1, src[0], (AVRational){ 1, 50000 });
            *dst[1] = av_rescale_q(1, src[1], (AVRational){ 1, 50000 });
        }

        params->mastering_display.i_white_x =
            av_rescale_q(1, mdcv->white_point[0], (AVRational){ 1, 50000 });
        params->mastering_display.i_white_y =
            av_rescale_q(1, mdcv->white_point[1], (AVRational){ 1, 50000 });
    }

    if (mdcv->has_luminance) {
        params->mastering_display.i_display_max =
            av_rescale_q(1, mdcv->max_luminance, (AVRational){ 1, 10000 });
        params->mastering_display.i_display_min =
            av_rescale_q(1, mdcv->min_luminance, (AVRational){ 1, 10000 });
    }
}
#endif // CONFIG_LIBX264_HDR10

static void handle_side_data(AVCodecContext *avctx, x264_param_t *params)
{
#if CONFIG_LIBX264_HDR10
    const AVFrameSideData *cll_sd =
        av_frame_side_data_get(avctx->decoded_side_data,
            avctx->nb_decoded_side_data, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    const AVFrameSideData *mdcv_sd =
        av_frame_side_data_get(avctx->decoded_side_data,
            avctx->nb_decoded_side_data,
            AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);

    if (cll_sd) {
        const AVContentLightMetadata *cll =
            (AVContentLightMetadata *)cll_sd->data;

        params->content_light_level.i_max_cll  = cll->MaxCLL;
        params->content_light_level.i_max_fall = cll->MaxFALL;

        params->content_light_level.b_cll = 1;
    }

    if (mdcv_sd) {
        handle_mdcv(params, (AVMasteringDisplayMetadata *)mdcv_sd->data);
    }
#endif // CONFIG_LIBX264_HDR10
}

static av_cold int X264_init(AVCodecContext *avctx)
{
    X264Context *x4 = avctx->priv_data;
    AVCPBProperties *cpb_props;
    int sw,sh;
    int ret;

    if (avctx->global_quality > 0)
        av_log(avctx, AV_LOG_WARNING, "-qscale is ignored, -crf is recommended.\n");

#if CONFIG_LIBX262_ENCODER
    if (avctx->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        x4->params.b_mpeg2 = 1;
        x264_param_default_mpeg2(&x4->params);
    } else
#endif
    x264_param_default(&x4->params);

    x4->params.b_deblocking_filter         = avctx->flags & AV_CODEC_FLAG_LOOP_FILTER;

    if (x4->preset || x4->tune)
        if (x264_param_default_preset(&x4->params, x4->preset, x4->tune) < 0) {
            int i;
            av_log(avctx, AV_LOG_ERROR, "Error setting preset/tune %s/%s.\n", x4->preset, x4->tune);
            av_log(avctx, AV_LOG_INFO, "Possible presets:");
            for (i = 0; x264_preset_names[i]; i++)
                av_log(avctx, AV_LOG_INFO, " %s", x264_preset_names[i]);
            av_log(avctx, AV_LOG_INFO, "\n");
            av_log(avctx, AV_LOG_INFO, "Possible tunes:");
            for (i = 0; x264_tune_names[i]; i++)
                av_log(avctx, AV_LOG_INFO, " %s", x264_tune_names[i]);
            av_log(avctx, AV_LOG_INFO, "\n");
            return AVERROR(EINVAL);
        }

    if (avctx->level > 0)
        x4->params.i_level_idc = avctx->level;

    x4->params.pf_log               = X264_log;
    x4->params.p_log_private        = avctx;
    x4->params.i_log_level          = X264_LOG_DEBUG;
    x4->params.i_csp                = convert_pix_fmt(avctx->pix_fmt);
    x4->params.i_bitdepth           = av_pix_fmt_desc_get(avctx->pix_fmt)->comp[0].depth;

    PARSE_X264_OPT("weightp", wpredp);

    if (avctx->bit_rate) {
        if (avctx->bit_rate / 1000 > INT_MAX || avctx->rc_max_rate / 1000 > INT_MAX) {
            av_log(avctx, AV_LOG_ERROR, "bit_rate and rc_max_rate > %d000 not supported by libx264\n", INT_MAX);
            return AVERROR(EINVAL);
        }
        x4->params.rc.i_bitrate   = avctx->bit_rate / 1000;
        x4->params.rc.i_rc_method = X264_RC_ABR;
    }
    x4->params.rc.i_vbv_buffer_size = avctx->rc_buffer_size / 1000;
    x4->params.rc.i_vbv_max_bitrate = avctx->rc_max_rate    / 1000;
    x4->params.rc.b_stat_write      = avctx->flags & AV_CODEC_FLAG_PASS1;
    if (avctx->flags & AV_CODEC_FLAG_PASS2) {
        x4->params.rc.b_stat_read = 1;
    } else {
        if (x4->crf >= 0) {
            x4->params.rc.i_rc_method   = X264_RC_CRF;
            x4->params.rc.f_rf_constant = x4->crf;
        } else if (x4->cqp >= 0) {
            x4->params.rc.i_rc_method   = X264_RC_CQP;
            x4->params.rc.i_qp_constant = x4->cqp;
        }

        if (x4->crf_max >= 0)
            x4->params.rc.f_rf_constant_max = x4->crf_max;
    }

    if (avctx->rc_buffer_size && avctx->rc_initial_buffer_occupancy > 0 &&
        (avctx->rc_initial_buffer_occupancy <= avctx->rc_buffer_size)) {
        x4->params.rc.f_vbv_buffer_init =
            (float)avctx->rc_initial_buffer_occupancy / avctx->rc_buffer_size;
    }

    PARSE_X264_OPT("level", level);

    if (avctx->i_quant_factor > 0)
        x4->params.rc.f_ip_factor         = 1 / fabs(avctx->i_quant_factor);
    if (avctx->b_quant_factor > 0)
        x4->params.rc.f_pb_factor         = avctx->b_quant_factor;

    if (x4->chroma_offset)
        x4->params.analyse.i_chroma_qp_offset = x4->chroma_offset;

    if (avctx->gop_size >= 0)
        x4->params.i_keyint_max         = avctx->gop_size;
    if (avctx->max_b_frames >= 0)
        x4->params.i_bframe             = avctx->max_b_frames;

    if (x4->scenechange_threshold >= 0)
        x4->params.i_scenecut_threshold = x4->scenechange_threshold;

    if (avctx->qmin >= 0)
        x4->params.rc.i_qp_min          = avctx->qmin;
    if (avctx->qmax >= 0)
        x4->params.rc.i_qp_max          = avctx->qmax;
    if (avctx->max_qdiff >= 0)
        x4->params.rc.i_qp_step         = avctx->max_qdiff;
    if (avctx->qblur >= 0)
        x4->params.rc.f_qblur           = avctx->qblur;     /* temporally blur quants */
    if (avctx->qcompress >= 0)
        x4->params.rc.f_qcompress       = avctx->qcompress; /* 0.0 => cbr, 1.0 => constant qp */
    if (avctx->refs >= 0)
        x4->params.i_frame_reference    = avctx->refs;
    else if (x4->params.i_level_idc > 0) {
        int i;
        int mbn = AV_CEIL_RSHIFT(avctx->width, 4) * AV_CEIL_RSHIFT(avctx->height, 4);

        for (i = 0; i<x264_levels[i].level_idc; i++)
            if (x264_levels[i].level_idc == x4->params.i_level_idc)
                x4->params.i_frame_reference = av_clip(x264_levels[i].dpb / mbn, 1, x4->params.i_frame_reference);
    }

    if (avctx->trellis >= 0)
        x4->params.analyse.i_trellis    = avctx->trellis;
    if (avctx->me_range >= 0)
        x4->params.analyse.i_me_range   = avctx->me_range;
    if (x4->noise_reduction >= 0)
        x4->params.analyse.i_noise_reduction = x4->noise_reduction;
    if (avctx->me_subpel_quality >= 0)
        x4->params.analyse.i_subpel_refine   = avctx->me_subpel_quality;
    if (avctx->keyint_min >= 0)
        x4->params.i_keyint_min = avctx->keyint_min;
    if (avctx->me_cmp >= 0)
        x4->params.analyse.b_chroma_me = avctx->me_cmp & FF_CMP_CHROMA;

    if (x4->aq_mode >= 0)
        x4->params.rc.i_aq_mode = x4->aq_mode;
    if (x4->aq_strength >= 0)
        x4->params.rc.f_aq_strength = x4->aq_strength;
    PARSE_X264_OPT("psy-rd", psy_rd);
    PARSE_X264_OPT("deblock", deblock);
    PARSE_X264_OPT("partitions", partitions);
    PARSE_X264_OPT("stats", stats);
    if (x4->psy >= 0)
        x4->params.analyse.b_psy  = x4->psy;
    if (x4->rc_lookahead >= 0)
        x4->params.rc.i_lookahead = x4->rc_lookahead;
    if (x4->weightp >= 0)
        x4->params.analyse.i_weighted_pred = x4->weightp;
    if (x4->weightb >= 0)
        x4->params.analyse.b_weighted_bipred = x4->weightb;
    if (x4->cplxblur >= 0)
        x4->params.rc.f_complexity_blur = x4->cplxblur;

    if (x4->ssim >= 0)
        x4->params.analyse.b_ssim = x4->ssim;
    if (x4->intra_refresh >= 0)
        x4->params.b_intra_refresh = x4->intra_refresh;
    if (x4->bluray_compat >= 0) {
        x4->params.b_bluray_compat = x4->bluray_compat;
        x4->params.b_vfr_input = 0;
    }
    if (x4->avcintra_class >= 0)
        x4->params.i_avcintra_class = x4->avcintra_class;

    if (x4->avcintra_class > 200) {
#if X264_BUILD < 164
        av_log(avctx, AV_LOG_ERROR,
                "x264 too old for AVC Intra 300/480, at least version 164 needed\n");
        return AVERROR(EINVAL);
#else
        /* AVC-Intra 300/480 only supported by Sony XAVC flavor */
        x4->params.i_avcintra_flavor = X264_AVCINTRA_FLAVOR_SONY;
#endif
    }

    if (x4->b_bias != INT_MIN)
        x4->params.i_bframe_bias              = x4->b_bias;
    if (x4->b_pyramid >= 0)
        x4->params.i_bframe_pyramid = x4->b_pyramid;
    if (x4->mixed_refs >= 0)
        x4->params.analyse.b_mixed_references = x4->mixed_refs;
    if (x4->dct8x8 >= 0)
        x4->params.analyse.b_transform_8x8    = x4->dct8x8;
    if (x4->fast_pskip >= 0)
        x4->params.analyse.b_fast_pskip       = x4->fast_pskip;
    if (x4->aud >= 0)
        x4->params.b_aud                      = x4->aud;
    if (x4->mbtree >= 0)
        x4->params.rc.b_mb_tree               = x4->mbtree;
    if (x4->direct_pred >= 0)
        x4->params.analyse.i_direct_mv_pred   = x4->direct_pred;

    if (x4->slice_max_size >= 0)
        x4->params.i_slice_max_size =  x4->slice_max_size;

    if (x4->fastfirstpass)
        x264_param_apply_fastfirstpass(&x4->params);

    x4->profile = x4->profile_opt;
    /* Allow specifying the x264 profile through AVCodecContext. */
    if (!x4->profile)
        switch (avctx->profile) {
        case AV_PROFILE_H264_BASELINE:
            x4->profile = "baseline";
            break;
        case AV_PROFILE_H264_HIGH:
            x4->profile = "high";
            break;
        case AV_PROFILE_H264_HIGH_10:
            x4->profile = "high10";
            break;
        case AV_PROFILE_H264_HIGH_422:
            x4->profile = "high422";
            break;
        case AV_PROFILE_H264_HIGH_444:
            x4->profile = "high444";
            break;
        case AV_PROFILE_H264_MAIN:
            x4->profile = "main";
            break;
        default:
            break;
        }

    if (x4->nal_hrd >= 0)
        x4->params.i_nal_hrd = x4->nal_hrd;

    if (x4->motion_est >= 0)
        x4->params.analyse.i_me_method = x4->motion_est;

    if (x4->coder >= 0)
        x4->params.b_cabac = x4->coder;

    if (x4->b_frame_strategy >= 0)
        x4->params.i_bframe_adaptive = x4->b_frame_strategy;

    if (x4->profile)
        if (x264_param_apply_profile(&x4->params, x4->profile) < 0) {
            int i;
            av_log(avctx, AV_LOG_ERROR, "Error setting profile %s.\n", x4->profile);
            av_log(avctx, AV_LOG_INFO, "Possible profiles:");
            for (i = 0; x264_profile_names[i]; i++)
                av_log(avctx, AV_LOG_INFO, " %s", x264_profile_names[i]);
            av_log(avctx, AV_LOG_INFO, "\n");
            return AVERROR(EINVAL);
        }

    x4->params.i_width          = avctx->width;
    x4->params.i_height         = avctx->height;
    av_reduce(&sw, &sh, avctx->sample_aspect_ratio.num, avctx->sample_aspect_ratio.den, 4096);
    x4->params.vui.i_sar_width  = sw;
    x4->params.vui.i_sar_height = sh;
    x4->params.i_timebase_den = avctx->time_base.den;
    x4->params.i_timebase_num = avctx->time_base.num;
    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        x4->params.i_fps_num = avctx->framerate.num;
        x4->params.i_fps_den = avctx->framerate.den;
    } else {
        x4->params.i_fps_num = avctx->time_base.den;
        x4->params.i_fps_den = avctx->time_base.num;
    }

    x4->params.analyse.b_psnr = avctx->flags & AV_CODEC_FLAG_PSNR;

    x4->params.i_threads      = avctx->thread_count;
    if (avctx->thread_type)
        x4->params.b_sliced_threads = avctx->thread_type == FF_THREAD_SLICE;

    x4->params.b_interlaced   = avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT;

    x4->params.b_open_gop     = !(avctx->flags & AV_CODEC_FLAG_CLOSED_GOP);

    x4->params.i_slice_count  = avctx->slices;

    if (avctx->color_range != AVCOL_RANGE_UNSPECIFIED)
        x4->params.vui.b_fullrange = avctx->color_range == AVCOL_RANGE_JPEG;
    else if (avctx->pix_fmt == AV_PIX_FMT_YUVJ420P ||
             avctx->pix_fmt == AV_PIX_FMT_YUVJ422P ||
             avctx->pix_fmt == AV_PIX_FMT_YUVJ444P)
        x4->params.vui.b_fullrange = 1;

    if (avctx->colorspace != AVCOL_SPC_UNSPECIFIED)
        x4->params.vui.i_colmatrix = avctx->colorspace;
    if (avctx->color_primaries != AVCOL_PRI_UNSPECIFIED)
        x4->params.vui.i_colorprim = avctx->color_primaries;
    if (avctx->color_trc != AVCOL_TRC_UNSPECIFIED)
        x4->params.vui.i_transfer  = avctx->color_trc;
    if (avctx->chroma_sample_location != AVCHROMA_LOC_UNSPECIFIED)
        x4->params.vui.i_chroma_loc = avctx->chroma_sample_location - 1;

    handle_side_data(avctx, &x4->params);

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER)
        x4->params.b_repeat_headers = 0;

    if (avctx->flags & AV_CODEC_FLAG_RECON_FRAME)
        x4->params.b_full_recon = 1;

    if(x4->x264opts){
        const char *p= x4->x264opts;
        while(p){
            char param[4096]={0}, val[4096]={0};
            if(sscanf(p, "%4095[^:=]=%4095[^:]", param, val) == 1){
                ret = parse_opts(avctx, param, "1");
                if (ret < 0)
                    return ret;
            } else {
                ret = parse_opts(avctx, param, val);
                if (ret < 0)
                    return ret;
            }
            p= strchr(p, ':');
            if (p) {
                ++p;
            }
        }
    }

    /* Separate headers not supported in AVC-Intra mode */
    if (x4->avcintra_class >= 0)
        x4->params.b_repeat_headers = 1;

    {
        const AVDictionaryEntry *en = NULL;
        while (en = av_dict_iterate(x4->x264_params, en)) {
           if ((ret = x264_param_parse(&x4->params, en->key, en->value)) < 0) {
               av_log(avctx, AV_LOG_WARNING,
                      "Error parsing option '%s = %s'.\n",
                       en->key, en->value);
#if X264_BUILD >= 161
               if (ret == X264_PARAM_ALLOC_FAILED)
                   return AVERROR(ENOMEM);
#endif
           }
        }
    }

    x4->params.analyse.b_mb_info = x4->mb_info;

    // update AVCodecContext with x264 parameters
    avctx->has_b_frames = x4->params.i_bframe ?
        x4->params.i_bframe_pyramid ? 2 : 1 : 0;
    if (avctx->max_b_frames < 0)
        avctx->max_b_frames = 0;

    avctx->bit_rate = x4->params.rc.i_bitrate*1000LL;

    x4->enc = x264_encoder_open(&x4->params);
    if (!x4->enc)
        return AVERROR_EXTERNAL;

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        ret = set_extradata(avctx);
        if (ret < 0)
            return ret;
    }

    cpb_props = ff_encode_add_cpb_side_data(avctx);
    if (!cpb_props)
        return AVERROR(ENOMEM);
    cpb_props->buffer_size = x4->params.rc.i_vbv_buffer_size * 1000;
    cpb_props->max_bitrate = x4->params.rc.i_vbv_max_bitrate * 1000LL;
    cpb_props->avg_bitrate = x4->params.rc.i_bitrate         * 1000LL;

    // Overestimate the reordered opaque buffer size, in case a runtime
    // reconfigure would increase the delay (which it shouldn't).
    x4->nb_reordered_opaque = x264_encoder_maximum_delayed_frames(x4->enc) + 17;
    x4->reordered_opaque    = av_calloc(x4->nb_reordered_opaque,
                                        sizeof(*x4->reordered_opaque));
    if (!x4->reordered_opaque) {
        x4->nb_reordered_opaque = 0;
        return AVERROR(ENOMEM);
    }

    return 0;
}

static const enum AVPixelFormat pix_fmts_8bit[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_NV16,
#ifdef X264_CSP_NV21
    AV_PIX_FMT_NV21,
#endif
    AV_PIX_FMT_NONE
};
static const enum AVPixelFormat pix_fmts_9bit[] = {
    AV_PIX_FMT_YUV420P9,
    AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_NONE
};
static const enum AVPixelFormat pix_fmts_10bit[] = {
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_NV20,
    AV_PIX_FMT_NONE
};
static const enum AVPixelFormat pix_fmts_all[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_NV16,
#ifdef X264_CSP_NV21
    AV_PIX_FMT_NV21,
#endif
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_NV20,
#ifdef X264_CSP_I400
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_GRAY10,
#endif
    AV_PIX_FMT_NONE
};
#if CONFIG_LIBX264RGB_ENCODER
static const enum AVPixelFormat pix_fmts_8bit_rgb[] = {
    AV_PIX_FMT_BGR0,
    AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGB24,
    AV_PIX_FMT_NONE
};
#endif

#define OFFSET(x) offsetof(X264Context, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "preset",        "Set the encoding preset (cf. x264 --fullhelp)",   OFFSET(preset),        AV_OPT_TYPE_STRING, { .str = "medium" }, 0, 0, VE},
    { "tune",          "Tune the encoding params (cf. x264 --fullhelp)",  OFFSET(tune),          AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE},
    { "profile",       "Set profile restrictions (cf. x264 --fullhelp)",  OFFSET(profile_opt),       AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE},
    { "fastfirstpass", "Use fast settings when encoding first pass",      OFFSET(fastfirstpass), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, VE},
    {"level", "Specify level (as defined by Annex A)", OFFSET(level), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, VE},
    {"passlogfile", "Filename for 2 pass stats", OFFSET(stats), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, VE},
    {"wpredp", "Weighted prediction for P-frames", OFFSET(wpredp), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, VE},
    {"a53cc",          "Use A53 Closed Captions (if available)",          OFFSET(a53_cc),        AV_OPT_TYPE_BOOL,   {.i64 = 1}, 0, 1, VE},
    {"x264opts", "x264 options", OFFSET(x264opts), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, VE},
    { "crf",           "Select the quality for constant quality mode",    OFFSET(crf),           AV_OPT_TYPE_FLOAT,  {.dbl = -1 }, -1, FLT_MAX, VE },
    { "crf_max",       "In CRF mode, prevents VBV from lowering quality beyond this point.",OFFSET(crf_max), AV_OPT_TYPE_FLOAT, {.dbl = -1 }, -1, FLT_MAX, VE },
    { "qp",            "Constant quantization parameter rate control method",OFFSET(cqp),        AV_OPT_TYPE_INT,    { .i64 = -1 }, -1, INT_MAX, VE },
    { "aq-mode",       "AQ method",                                       OFFSET(aq_mode),       AV_OPT_TYPE_INT,    { .i64 = -1 }, -1, INT_MAX, VE, .unit = "aq_mode"},
    { "none",          NULL,                              0, AV_OPT_TYPE_CONST, {.i64 = X264_AQ_NONE},         INT_MIN, INT_MAX, VE, .unit = "aq_mode" },
    { "variance",      "Variance AQ (complexity mask)",   0, AV_OPT_TYPE_CONST, {.i64 = X264_AQ_VARIANCE},     INT_MIN, INT_MAX, VE, .unit = "aq_mode" },
    { "autovariance",  "Auto-variance AQ",                0, AV_OPT_TYPE_CONST, {.i64 = X264_AQ_AUTOVARIANCE}, INT_MIN, INT_MAX, VE, .unit = "aq_mode" },
    { "autovariance-biased", "Auto-variance AQ with bias to dark scenes", 0, AV_OPT_TYPE_CONST, {.i64 = X264_AQ_AUTOVARIANCE_BIASED}, INT_MIN, INT_MAX, VE, .unit = "aq_mode" },
    { "aq-strength",   "AQ strength. Reduces blocking and blurring in flat and textured areas.", OFFSET(aq_strength), AV_OPT_TYPE_FLOAT, {.dbl = -1}, -1, FLT_MAX, VE},
    { "psy",           "Use psychovisual optimizations.",                 OFFSET(psy),           AV_OPT_TYPE_BOOL,   { .i64 = -1 }, -1, 1, VE },
    { "psy-rd",        "Strength of psychovisual optimization, in <psy-rd>:<psy-trellis> format.", OFFSET(psy_rd), AV_OPT_TYPE_STRING,  {0 }, 0, 0, VE},
    { "rc-lookahead",  "Number of frames to look ahead for frametype and ratecontrol", OFFSET(rc_lookahead), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, VE },
    { "weightb",       "Weighted prediction for B-frames.",               OFFSET(weightb),       AV_OPT_TYPE_BOOL,   { .i64 = -1 }, -1, 1, VE },
    { "weightp",       "Weighted prediction analysis method.",            OFFSET(weightp),       AV_OPT_TYPE_INT,    { .i64 = -1 }, -1, INT_MAX, VE, .unit = "weightp" },
    { "none",          NULL, 0, AV_OPT_TYPE_CONST, {.i64 = X264_WEIGHTP_NONE},   INT_MIN, INT_MAX, VE, .unit = "weightp" },
    { "simple",        NULL, 0, AV_OPT_TYPE_CONST, {.i64 = X264_WEIGHTP_SIMPLE}, INT_MIN, INT_MAX, VE, .unit = "weightp" },
    { "smart",         NULL, 0, AV_OPT_TYPE_CONST, {.i64 = X264_WEIGHTP_SMART},  INT_MIN, INT_MAX, VE, .unit = "weightp" },
    { "ssim",          "Calculate and print SSIM stats.",                 OFFSET(ssim),          AV_OPT_TYPE_BOOL,   { .i64 = -1 }, -1, 1, VE },
    { "intra-refresh", "Use Periodic Intra Refresh instead of IDR frames.",OFFSET(intra_refresh),AV_OPT_TYPE_BOOL,   { .i64 = -1 }, -1, 1, VE },
    { "bluray-compat", "Bluray compatibility workarounds.",               OFFSET(bluray_compat) ,AV_OPT_TYPE_BOOL,   { .i64 = -1 }, -1, 1, VE },
    { "b-bias",        "Influences how often B-frames are used",          OFFSET(b_bias),        AV_OPT_TYPE_INT,    { .i64 = INT_MIN}, INT_MIN, INT_MAX, VE },
    { "b-pyramid",     "Keep some B-frames as references.",               OFFSET(b_pyramid),     AV_OPT_TYPE_INT,    { .i64 = -1 }, -1, INT_MAX, VE, .unit = "b_pyramid" },
    { "none",          NULL,                                  0, AV_OPT_TYPE_CONST, {.i64 = X264_B_PYRAMID_NONE},   INT_MIN, INT_MAX, VE, .unit = "b_pyramid" },
    { "strict",        "Strictly hierarchical pyramid",       0, AV_OPT_TYPE_CONST, {.i64 = X264_B_PYRAMID_STRICT}, INT_MIN, INT_MAX, VE, .unit = "b_pyramid" },
    { "normal",        "Non-strict (not Blu-ray compatible)", 0, AV_OPT_TYPE_CONST, {.i64 = X264_B_PYRAMID_NORMAL}, INT_MIN, INT_MAX, VE, .unit = "b_pyramid" },
    { "mixed-refs",    "One reference per partition, as opposed to one reference per macroblock", OFFSET(mixed_refs), AV_OPT_TYPE_BOOL, { .i64 = -1}, -1, 1, VE },
    { "8x8dct",        "High profile 8x8 transform.",                     OFFSET(dct8x8),        AV_OPT_TYPE_BOOL,   { .i64 = -1 }, -1, 1, VE},
    { "fast-pskip",    NULL,                                              OFFSET(fast_pskip),    AV_OPT_TYPE_BOOL,   { .i64 = -1 }, -1, 1, VE},
    { "aud",           "Use access unit delimiters.",                     OFFSET(aud),           AV_OPT_TYPE_BOOL,   { .i64 = -1 }, -1, 1, VE},
    { "mbtree",        "Use macroblock tree ratecontrol.",                OFFSET(mbtree),        AV_OPT_TYPE_BOOL,   { .i64 = -1 }, -1, 1, VE},
    { "deblock",       "Loop filter parameters, in <alpha:beta> form.",   OFFSET(deblock),       AV_OPT_TYPE_STRING, { 0 },  0, 0, VE},
    { "cplxblur",      "Reduce fluctuations in QP (before curve compression)", OFFSET(cplxblur), AV_OPT_TYPE_FLOAT,  {.dbl = -1 }, -1, FLT_MAX, VE},
    { "partitions",    "A comma-separated list of partitions to consider. "
                       "Possible values: p8x8, p4x4, b8x8, i8x8, i4x4, none, all", OFFSET(partitions), AV_OPT_TYPE_STRING, { 0 }, 0, 0, VE},
    { "direct-pred",   "Direct MV prediction mode",                       OFFSET(direct_pred),   AV_OPT_TYPE_INT,    { .i64 = -1 }, -1, INT_MAX, VE, .unit = "direct-pred" },
    { "none",          NULL,      0,    AV_OPT_TYPE_CONST, { .i64 = X264_DIRECT_PRED_NONE },     0, 0, VE, .unit = "direct-pred" },
    { "spatial",       NULL,      0,    AV_OPT_TYPE_CONST, { .i64 = X264_DIRECT_PRED_SPATIAL },  0, 0, VE, .unit = "direct-pred" },
    { "temporal",      NULL,      0,    AV_OPT_TYPE_CONST, { .i64 = X264_DIRECT_PRED_TEMPORAL }, 0, 0, VE, .unit = "direct-pred" },
    { "auto",          NULL,      0,    AV_OPT_TYPE_CONST, { .i64 = X264_DIRECT_PRED_AUTO },     0, 0, VE, .unit = "direct-pred" },
    { "slice-max-size","Limit the size of each slice in bytes",           OFFSET(slice_max_size),AV_OPT_TYPE_INT,    { .i64 = -1 }, -1, INT_MAX, VE },
    { "stats",         "Filename for 2 pass stats",                       OFFSET(stats),         AV_OPT_TYPE_STRING, { 0 },  0,       0, VE },
    { "nal-hrd",       "Signal HRD information (requires vbv-bufsize; "
                       "cbr not allowed in .mp4)",                        OFFSET(nal_hrd),       AV_OPT_TYPE_INT,    { .i64 = -1 }, -1, INT_MAX, VE, .unit = "nal-hrd" },
    { "none",          NULL, 0, AV_OPT_TYPE_CONST, {.i64 = X264_NAL_HRD_NONE}, INT_MIN, INT_MAX, VE, .unit = "nal-hrd" },
    { "vbr",           NULL, 0, AV_OPT_TYPE_CONST, {.i64 = X264_NAL_HRD_VBR},  INT_MIN, INT_MAX, VE, .unit = "nal-hrd" },
    { "cbr",           NULL, 0, AV_OPT_TYPE_CONST, {.i64 = X264_NAL_HRD_CBR},  INT_MIN, INT_MAX, VE, .unit = "nal-hrd" },
    { "avcintra-class","AVC-Intra class 50/100/200/300/480",              OFFSET(avcintra_class),AV_OPT_TYPE_INT,    { .i64 = -1 }, -1, 480   , VE},
    { "me_method",    "Set motion estimation method",                     OFFSET(motion_est),    AV_OPT_TYPE_INT,    { .i64 = -1 }, -1, X264_ME_TESA, VE, .unit = "motion-est"},
    { "motion-est",   "Set motion estimation method",                     OFFSET(motion_est),    AV_OPT_TYPE_INT,    { .i64 = -1 }, -1, X264_ME_TESA, VE, .unit = "motion-est"},
    { "dia",           NULL, 0, AV_OPT_TYPE_CONST, { .i64 = X264_ME_DIA },  INT_MIN, INT_MAX, VE, .unit = "motion-est" },
    { "hex",           NULL, 0, AV_OPT_TYPE_CONST, { .i64 = X264_ME_HEX },  INT_MIN, INT_MAX, VE, .unit = "motion-est" },
    { "umh",           NULL, 0, AV_OPT_TYPE_CONST, { .i64 = X264_ME_UMH },  INT_MIN, INT_MAX, VE, .unit = "motion-est" },
    { "esa",           NULL, 0, AV_OPT_TYPE_CONST, { .i64 = X264_ME_ESA },  INT_MIN, INT_MAX, VE, .unit = "motion-est" },
    { "tesa",          NULL, 0, AV_OPT_TYPE_CONST, { .i64 = X264_ME_TESA }, INT_MIN, INT_MAX, VE, .unit = "motion-est" },
    { "forced-idr",   "If forcing keyframes, force them as IDR frames.",                                  OFFSET(forced_idr),  AV_OPT_TYPE_BOOL,   { .i64 = 0 }, -1, 1, VE },
    { "coder",    "Coder type",                                           OFFSET(coder), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 1, VE, .unit = "coder" },
    { "default",          NULL, 0, AV_OPT_TYPE_CONST, { .i64 = -1 }, INT_MIN, INT_MAX, VE, .unit = "coder" },
    { "cavlc",            NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 },  INT_MIN, INT_MAX, VE, .unit = "coder" },
    { "cabac",            NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 },  INT_MIN, INT_MAX, VE, .unit = "coder" },
    { "vlc",              NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 },  INT_MIN, INT_MAX, VE, .unit = "coder" },
    { "ac",               NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 },  INT_MIN, INT_MAX, VE, .unit = "coder" },
    { "b_strategy",   "Strategy to choose between I/P/B-frames",          OFFSET(b_frame_strategy), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 2, VE },
    { "chromaoffset", "QP difference between chroma and luma",            OFFSET(chroma_offset), AV_OPT_TYPE_INT, { .i64 = 0 }, INT_MIN, INT_MAX, VE },
    { "sc_threshold", "Scene change threshold",                           OFFSET(scenechange_threshold), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX, VE },
    { "noise_reduction", "Noise reduction",                               OFFSET(noise_reduction), AV_OPT_TYPE_INT, { .i64 = -1 }, INT_MIN, INT_MAX, VE },
    { "udu_sei",      "Use user data unregistered SEI if available",      OFFSET(udu_sei),  AV_OPT_TYPE_BOOL,   { .i64 = 0 }, 0, 1, VE },
    { "x264-params",  "Override the x264 configuration using a :-separated list of key=value parameters", OFFSET(x264_params), AV_OPT_TYPE_DICT, { 0 }, 0, 0, VE },
    { "mb_info",      "Set mb_info data through AVSideData, only useful when used from the API", OFFSET(mb_info), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { NULL },
};

static const FFCodecDefault x264_defaults[] = {
    { "b",                "0" },
    { "bf",               "-1" },
    { "flags2",           "0" },
    { "g",                "-1" },
    { "i_qfactor",        "-1" },
    { "b_qfactor",        "-1" },
    { "qmin",             "-1" },
    { "qmax",             "-1" },
    { "qdiff",            "-1" },
    { "qblur",            "-1" },
    { "qcomp",            "-1" },
//     { "rc_lookahead",     "-1" },
    { "refs",             "-1" },
    { "trellis",          "-1" },
    { "me_range",         "-1" },
    { "subq",             "-1" },
    { "keyint_min",       "-1" },
    { "cmp",              "-1" },
    { "threads",          AV_STRINGIFY(X264_THREADS_AUTO) },
    { "thread_type",      "0" },
    { "flags",            "+cgop" },
    { "rc_init_occupancy","-1" },
    { NULL },
};

#if CONFIG_LIBX264_ENCODER
static const AVClass x264_class = {
    .class_name = "libx264",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_libx264_encoder = {
    .p.name           = "libx264",
    CODEC_LONG_NAME("libx264 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_H264,
    .p.capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                        AV_CODEC_CAP_OTHER_THREADS |
                        AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE |
                        AV_CODEC_CAP_ENCODER_FLUSH |
                        AV_CODEC_CAP_ENCODER_RECON_FRAME,
    .p.priv_class     = &x264_class,
    .p.wrapper_name   = "libx264",
    .priv_data_size   = sizeof(X264Context),
    .init             = X264_init,
    FF_CODEC_ENCODE_CB(X264_frame),
    .flush            = X264_flush,
    .close            = X264_close,
    .defaults         = x264_defaults,
    CODEC_PIXFMTS_ARRAY(pix_fmts_all),
    .color_ranges     = AVCOL_RANGE_MPEG | AVCOL_RANGE_JPEG,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_AUTO_THREADS
#if X264_BUILD < 158
                      | FF_CODEC_CAP_NOT_INIT_THREADSAFE
#endif
                      ,
};
#endif

#if CONFIG_LIBX264RGB_ENCODER
static const AVClass rgbclass = {
    .class_name = "libx264rgb",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_libx264rgb_encoder = {
    .p.name         = "libx264rgb",
    CODEC_LONG_NAME("libx264 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 RGB"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_H264,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                      AV_CODEC_CAP_OTHER_THREADS |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    CODEC_PIXFMTS_ARRAY(pix_fmts_8bit_rgb),
    .p.priv_class   = &rgbclass,
    .p.wrapper_name = "libx264",
    .priv_data_size = sizeof(X264Context),
    .init           = X264_init,
    FF_CODEC_ENCODE_CB(X264_frame),
    .close          = X264_close,
    .defaults       = x264_defaults,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_AUTO_THREADS
#if X264_BUILD < 158
                      | FF_CODEC_CAP_NOT_INIT_THREADSAFE
#endif
                      ,
};
#endif

#if CONFIG_LIBX262_ENCODER
static const AVClass X262_class = {
    .class_name = "libx262",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_libx262_encoder = {
    .p.name           = "libx262",
    CODEC_LONG_NAME("libx262 MPEG2VIDEO"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_MPEG2VIDEO,
    .p.capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                        AV_CODEC_CAP_OTHER_THREADS |
                        AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    CODEC_PIXFMTS_ARRAY(pix_fmts_8bit),
    .color_ranges     = AVCOL_RANGE_MPEG,
    .p.priv_class     = &X262_class,
    .p.wrapper_name   = "libx264",
    .priv_data_size   = sizeof(X264Context),
    .init             = X264_init,
    FF_CODEC_ENCODE_CB(X264_frame),
    .close            = X264_close,
    .defaults         = x264_defaults,
    .caps_internal    = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                        FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_AUTO_THREADS,
};
#endif
