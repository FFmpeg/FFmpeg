/*
 * Video Decode and Presentation API for UNIX (VDPAU) is used for
 * HW decode acceleration for MPEG-1/2, MPEG-4 ASP, H.264 and VC-1.
 *
 * Copyright (c) 2008 NVIDIA
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

#include <limits.h>

#include "avcodec.h"
#include "internal.h"
#include "h264dec.h"
#include "vc1.h"
#include "vdpau.h"
#include "vdpau_compat.h"
#include "vdpau_internal.h"

// XXX: at the time of adding this ifdefery, av_assert* wasn't use outside.
// When dropping it, make sure other av_assert* were not added since then.
#if FF_API_BUFS_VDPAU
#include "libavutil/avassert.h"
#endif

#if FF_API_VDPAU
#undef NDEBUG
#include <assert.h>
#endif

/**
 * @addtogroup VDPAU_Decoding
 *
 * @{
 */

static int vdpau_error(VdpStatus status)
{
    switch (status) {
    case VDP_STATUS_OK:
        return 0;
    case VDP_STATUS_NO_IMPLEMENTATION:
        return AVERROR(ENOSYS);
    case VDP_STATUS_DISPLAY_PREEMPTED:
        return AVERROR(EIO);
    case VDP_STATUS_INVALID_HANDLE:
        return AVERROR(EBADF);
    case VDP_STATUS_INVALID_POINTER:
        return AVERROR(EFAULT);
    case VDP_STATUS_RESOURCES:
        return AVERROR(ENOBUFS);
    case VDP_STATUS_HANDLE_DEVICE_MISMATCH:
        return AVERROR(EXDEV);
    case VDP_STATUS_ERROR:
        return AVERROR(EIO);
    default:
        return AVERROR(EINVAL);
    }
}

AVVDPAUContext *av_alloc_vdpaucontext(void)
{
    return av_vdpau_alloc_context();
}

MAKE_ACCESSORS(AVVDPAUContext, vdpau_hwaccel, AVVDPAU_Render2, render2)

int av_vdpau_get_surface_parameters(AVCodecContext *avctx,
                                    VdpChromaType *type,
                                    uint32_t *width, uint32_t *height)
{
    VdpChromaType t;
    uint32_t w = avctx->coded_width;
    uint32_t h = avctx->coded_height;

    /* See <vdpau/vdpau.h> for per-type alignment constraints. */
    switch (avctx->sw_pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
        t = VDP_CHROMA_TYPE_420;
        w = (w + 1) & ~1;
        h = (h + 3) & ~3;
        break;
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUVJ422P:
        t = VDP_CHROMA_TYPE_422;
        w = (w + 1) & ~1;
        h = (h + 1) & ~1;
        break;
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVJ444P:
        t = VDP_CHROMA_TYPE_444;
        h = (h + 1) & ~1;
        break;
    default:
        return AVERROR(ENOSYS);
    }

    if (type)
        *type = t;
    if (width)
        *width = w;
    if (height)
        *height = h;
    return 0;
}

int ff_vdpau_common_init(AVCodecContext *avctx, VdpDecoderProfile profile,
                         int level)
{
    VDPAUHWContext *hwctx = avctx->hwaccel_context;
    VDPAUContext *vdctx = avctx->internal->hwaccel_priv_data;
    VdpVideoSurfaceQueryCapabilities *surface_query_caps;
    VdpDecoderQueryCapabilities *decoder_query_caps;
    VdpDecoderCreate *create;
    void *func;
    VdpStatus status;
    VdpBool supported;
    uint32_t max_level, max_mb, max_width, max_height;
    VdpChromaType type;
    uint32_t width;
    uint32_t height;

    vdctx->width            = UINT32_MAX;
    vdctx->height           = UINT32_MAX;

    if (av_vdpau_get_surface_parameters(avctx, &type, &width, &height))
        return AVERROR(ENOSYS);

    if (hwctx) {
        hwctx->reset            = 0;

        if (hwctx->context.decoder != VDP_INVALID_HANDLE) {
            vdctx->decoder = hwctx->context.decoder;
            vdctx->render  = hwctx->context.render;
            vdctx->device  = VDP_INVALID_HANDLE;
            return 0; /* Decoder created by user */
        }

        vdctx->device           = hwctx->device;
        vdctx->get_proc_address = hwctx->get_proc_address;

        if (hwctx->flags & AV_HWACCEL_FLAG_IGNORE_LEVEL)
            level = 0;

        if (!(hwctx->flags & AV_HWACCEL_FLAG_ALLOW_HIGH_DEPTH) &&
            type != VDP_CHROMA_TYPE_420)
            return AVERROR(ENOSYS);
    } else {
        AVHWFramesContext *frames_ctx = NULL;
        AVVDPAUDeviceContext *dev_ctx;

        // We assume the hw_frames_ctx always survives until ff_vdpau_common_uninit
        // is called. This holds true as the user is not allowed to touch
        // hw_device_ctx, or hw_frames_ctx after get_format (and ff_get_format
        // itself also uninits before unreffing hw_frames_ctx).
        if (avctx->hw_frames_ctx) {
            frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
        } else if (avctx->hw_device_ctx) {
            int ret;

            avctx->hw_frames_ctx = av_hwframe_ctx_alloc(avctx->hw_device_ctx);
            if (!avctx->hw_frames_ctx)
                return AVERROR(ENOMEM);

            frames_ctx            = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
            frames_ctx->format    = AV_PIX_FMT_VDPAU;
            frames_ctx->sw_format = avctx->sw_pix_fmt;
            frames_ctx->width     = avctx->coded_width;
            frames_ctx->height    = avctx->coded_height;

            ret = av_hwframe_ctx_init(avctx->hw_frames_ctx);
            if (ret < 0) {
                av_buffer_unref(&avctx->hw_frames_ctx);
                return ret;
            }
        }

        if (!frames_ctx) {
            av_log(avctx, AV_LOG_ERROR, "A hardware frames context is "
                   "required for VDPAU decoding.\n");
            return AVERROR(EINVAL);
        }

        dev_ctx = frames_ctx->device_ctx->hwctx;

        vdctx->device           = dev_ctx->device;
        vdctx->get_proc_address = dev_ctx->get_proc_address;

        if (avctx->hwaccel_flags & AV_HWACCEL_FLAG_IGNORE_LEVEL)
            level = 0;
    }

    if (level < 0)
        return AVERROR(ENOTSUP);

    status = vdctx->get_proc_address(vdctx->device,
                                     VDP_FUNC_ID_VIDEO_SURFACE_QUERY_CAPABILITIES,
                                     &func);
    if (status != VDP_STATUS_OK)
        return vdpau_error(status);
    else
        surface_query_caps = func;

    status = surface_query_caps(vdctx->device, type, &supported,
                                &max_width, &max_height);
    if (status != VDP_STATUS_OK)
        return vdpau_error(status);
    if (supported != VDP_TRUE ||
        max_width < width || max_height < height)
        return AVERROR(ENOTSUP);

    status = vdctx->get_proc_address(vdctx->device,
                                     VDP_FUNC_ID_DECODER_QUERY_CAPABILITIES,
                                     &func);
    if (status != VDP_STATUS_OK)
        return vdpau_error(status);
    else
        decoder_query_caps = func;

    status = decoder_query_caps(vdctx->device, profile, &supported, &max_level,
                                &max_mb, &max_width, &max_height);
#ifdef VDP_DECODER_PROFILE_H264_CONSTRAINED_BASELINE
    if ((status != VDP_STATUS_OK || supported != VDP_TRUE) && profile == VDP_DECODER_PROFILE_H264_CONSTRAINED_BASELINE) {
        profile = VDP_DECODER_PROFILE_H264_MAIN;
        status = decoder_query_caps(vdctx->device, profile, &supported,
                                    &max_level, &max_mb,
                                    &max_width, &max_height);
    }
#endif
    if (status != VDP_STATUS_OK)
        return vdpau_error(status);

    if (supported != VDP_TRUE || max_level < level ||
        max_width < width || max_height < height)
        return AVERROR(ENOTSUP);

    status = vdctx->get_proc_address(vdctx->device, VDP_FUNC_ID_DECODER_CREATE,
                                     &func);
    if (status != VDP_STATUS_OK)
        return vdpau_error(status);
    else
        create = func;

    status = vdctx->get_proc_address(vdctx->device, VDP_FUNC_ID_DECODER_RENDER,
                                     &func);
    if (status != VDP_STATUS_OK)
        return vdpau_error(status);
    else
        vdctx->render = func;

    status = create(vdctx->device, profile, width, height, avctx->refs,
                    &vdctx->decoder);
    if (status == VDP_STATUS_OK) {
        vdctx->width  = avctx->coded_width;
        vdctx->height = avctx->coded_height;
    }

    return vdpau_error(status);
}

int ff_vdpau_common_uninit(AVCodecContext *avctx)
{
    VDPAUContext *vdctx = avctx->internal->hwaccel_priv_data;
    VdpDecoderDestroy *destroy;
    void *func;
    VdpStatus status;

    if (vdctx->device == VDP_INVALID_HANDLE)
        return 0; /* Decoder created and destroyed by user */
    if (vdctx->width == UINT32_MAX && vdctx->height == UINT32_MAX)
        return 0;

    status = vdctx->get_proc_address(vdctx->device,
                                     VDP_FUNC_ID_DECODER_DESTROY, &func);
    if (status != VDP_STATUS_OK)
        return vdpau_error(status);
    else
        destroy = func;

    status = destroy(vdctx->decoder);
    return vdpau_error(status);
}

static int ff_vdpau_common_reinit(AVCodecContext *avctx)
{
    VDPAUHWContext *hwctx = avctx->hwaccel_context;
    VDPAUContext *vdctx = avctx->internal->hwaccel_priv_data;

    if (vdctx->device == VDP_INVALID_HANDLE)
        return 0; /* Decoder created by user */
    if (avctx->coded_width == vdctx->width &&
        avctx->coded_height == vdctx->height && (!hwctx || !hwctx->reset))
        return 0;

    avctx->hwaccel->uninit(avctx);
    return avctx->hwaccel->init(avctx);
}

int ff_vdpau_common_start_frame(struct vdpau_picture_context *pic_ctx,
                                av_unused const uint8_t *buffer,
                                av_unused uint32_t size)
{
    pic_ctx->bitstream_buffers_allocated = 0;
    pic_ctx->bitstream_buffers_used      = 0;
    pic_ctx->bitstream_buffers           = NULL;
    return 0;
}

int ff_vdpau_common_end_frame(AVCodecContext *avctx, AVFrame *frame,
                              struct vdpau_picture_context *pic_ctx)
{
    VDPAUContext *vdctx = avctx->internal->hwaccel_priv_data;
    AVVDPAUContext *hwctx = avctx->hwaccel_context;
    VdpVideoSurface surf = ff_vdpau_get_surface_id(frame);
    VdpStatus status;
    int val;

    val = ff_vdpau_common_reinit(avctx);
    if (val < 0)
        return val;

#if FF_API_BUFS_VDPAU
FF_DISABLE_DEPRECATION_WARNINGS
    if (hwctx) {
    av_assert0(sizeof(hwctx->info) <= sizeof(pic_ctx->info));
    memcpy(&hwctx->info, &pic_ctx->info, sizeof(hwctx->info));
    hwctx->bitstream_buffers = pic_ctx->bitstream_buffers;
    hwctx->bitstream_buffers_used = pic_ctx->bitstream_buffers_used;
    hwctx->bitstream_buffers_allocated = pic_ctx->bitstream_buffers_allocated;
    }
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    if (hwctx && !hwctx->render && hwctx->render2) {
        status = hwctx->render2(avctx, frame, (void *)&pic_ctx->info,
                                pic_ctx->bitstream_buffers_used, pic_ctx->bitstream_buffers);
    } else
    status = vdctx->render(vdctx->decoder, surf, &pic_ctx->info,
                           pic_ctx->bitstream_buffers_used,
                           pic_ctx->bitstream_buffers);

    av_freep(&pic_ctx->bitstream_buffers);

#if FF_API_BUFS_VDPAU
FF_DISABLE_DEPRECATION_WARNINGS
    if (hwctx) {
    hwctx->bitstream_buffers = NULL;
    hwctx->bitstream_buffers_used = 0;
    hwctx->bitstream_buffers_allocated = 0;
    }
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    return vdpau_error(status);
}

#if CONFIG_MPEG1_VDPAU_HWACCEL || \
    CONFIG_MPEG2_VDPAU_HWACCEL || CONFIG_MPEG4_VDPAU_HWACCEL || \
    CONFIG_VC1_VDPAU_HWACCEL   || CONFIG_WMV3_VDPAU_HWACCEL
int ff_vdpau_mpeg_end_frame(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;
    Picture *pic = s->current_picture_ptr;
    struct vdpau_picture_context *pic_ctx = pic->hwaccel_picture_private;
    int val;

    val = ff_vdpau_common_end_frame(avctx, pic->f, pic_ctx);
    if (val < 0)
        return val;

    ff_mpeg_draw_horiz_band(s, 0, s->avctx->height);
    return 0;
}
#endif

int ff_vdpau_add_buffer(struct vdpau_picture_context *pic_ctx,
                        const uint8_t *buf, uint32_t size)
{
    VdpBitstreamBuffer *buffers = pic_ctx->bitstream_buffers;

    buffers = av_fast_realloc(buffers, &pic_ctx->bitstream_buffers_allocated,
                              (pic_ctx->bitstream_buffers_used + 1) * sizeof(*buffers));
    if (!buffers)
        return AVERROR(ENOMEM);

    pic_ctx->bitstream_buffers = buffers;
    buffers += pic_ctx->bitstream_buffers_used++;

    buffers->struct_version  = VDP_BITSTREAM_BUFFER_VERSION;
    buffers->bitstream       = buf;
    buffers->bitstream_bytes = size;
    return 0;
}

/* Obsolete non-hwaccel VDPAU support below... */

#if FF_API_VDPAU
void ff_vdpau_add_data_chunk(uint8_t *data, const uint8_t *buf, int buf_size)
{
    struct vdpau_render_state *render = (struct vdpau_render_state*)data;
    assert(render);

    render->bitstream_buffers= av_fast_realloc(
        render->bitstream_buffers,
        &render->bitstream_buffers_allocated,
        sizeof(*render->bitstream_buffers)*(render->bitstream_buffers_used + 1)
    );

    render->bitstream_buffers[render->bitstream_buffers_used].struct_version  = VDP_BITSTREAM_BUFFER_VERSION;
    render->bitstream_buffers[render->bitstream_buffers_used].bitstream       = buf;
    render->bitstream_buffers[render->bitstream_buffers_used].bitstream_bytes = buf_size;
    render->bitstream_buffers_used++;
}

#if CONFIG_H264_VDPAU_DECODER
void ff_vdpau_h264_set_reference_frames(H264Context *h)
{
    struct vdpau_render_state *render, *render_ref;
    VdpReferenceFrameH264 *rf, *rf2;
    H264Picture *pic;
    int i, list, pic_frame_idx;

    render = (struct vdpau_render_state *)h->cur_pic_ptr->f->data[0];
    assert(render);

    rf = &render->info.h264.referenceFrames[0];
#define H264_RF_COUNT FF_ARRAY_ELEMS(render->info.h264.referenceFrames)

    for (list = 0; list < 2; ++list) {
        H264Picture **lp = list ? h->long_ref : h->short_ref;
        int ls = list ? 16 : h->short_ref_count;

        for (i = 0; i < ls; ++i) {
            pic = lp[i];
            if (!pic || !pic->reference)
                continue;
            pic_frame_idx = pic->long_ref ? pic->pic_id : pic->frame_num;

            render_ref = (struct vdpau_render_state *)pic->f->data[0];
            assert(render_ref);

            rf2 = &render->info.h264.referenceFrames[0];
            while (rf2 != rf) {
                if (
                    (rf2->surface == render_ref->surface)
                    && (rf2->is_long_term == pic->long_ref)
                    && (rf2->frame_idx == pic_frame_idx)
                )
                    break;
                ++rf2;
            }
            if (rf2 != rf) {
                rf2->top_is_reference    |= (pic->reference & PICT_TOP_FIELD)    ? VDP_TRUE : VDP_FALSE;
                rf2->bottom_is_reference |= (pic->reference & PICT_BOTTOM_FIELD) ? VDP_TRUE : VDP_FALSE;
                continue;
            }

            if (rf >= &render->info.h264.referenceFrames[H264_RF_COUNT])
                continue;

            rf->surface             = render_ref->surface;
            rf->is_long_term        = pic->long_ref;
            rf->top_is_reference    = (pic->reference & PICT_TOP_FIELD)    ? VDP_TRUE : VDP_FALSE;
            rf->bottom_is_reference = (pic->reference & PICT_BOTTOM_FIELD) ? VDP_TRUE : VDP_FALSE;
            rf->field_order_cnt[0]  = pic->field_poc[0];
            rf->field_order_cnt[1]  = pic->field_poc[1];
            rf->frame_idx           = pic_frame_idx;

            ++rf;
        }
    }

    for (; rf < &render->info.h264.referenceFrames[H264_RF_COUNT]; ++rf) {
        rf->surface             = VDP_INVALID_HANDLE;
        rf->is_long_term        = 0;
        rf->top_is_reference    = 0;
        rf->bottom_is_reference = 0;
        rf->field_order_cnt[0]  = 0;
        rf->field_order_cnt[1]  = 0;
        rf->frame_idx           = 0;
    }
}

void ff_vdpau_h264_picture_start(H264Context *h)
{
    struct vdpau_render_state *render;
    int i;

    render = (struct vdpau_render_state *)h->cur_pic_ptr->f->data[0];
    assert(render);

    for (i = 0; i < 2; ++i) {
        int foc = h->cur_pic_ptr->field_poc[i];
        if (foc == INT_MAX)
            foc = 0;
        render->info.h264.field_order_cnt[i] = foc;
    }

    render->info.h264.frame_num = h->poc.frame_num;
}

void ff_vdpau_h264_picture_complete(H264Context *h)
{
    struct vdpau_render_state *render;

    render = (struct vdpau_render_state *)h->cur_pic_ptr->f->data[0];
    assert(render);

    render->info.h264.slice_count = h->current_slice;
    if (render->info.h264.slice_count < 1)
        return;

    render->info.h264.is_reference                           = (h->cur_pic_ptr->reference & 3) ? VDP_TRUE : VDP_FALSE;
    render->info.h264.field_pic_flag                         = h->picture_structure != PICT_FRAME;
    render->info.h264.bottom_field_flag                      = h->picture_structure == PICT_BOTTOM_FIELD;
    render->info.h264.num_ref_frames                         = h->ps.sps->ref_frame_count;
    render->info.h264.mb_adaptive_frame_field_flag           = h->ps.sps->mb_aff && !render->info.h264.field_pic_flag;
    render->info.h264.constrained_intra_pred_flag            = h->ps.pps->constrained_intra_pred;
    render->info.h264.weighted_pred_flag                     = h->ps.pps->weighted_pred;
    render->info.h264.weighted_bipred_idc                    = h->ps.pps->weighted_bipred_idc;
    render->info.h264.frame_mbs_only_flag                    = h->ps.sps->frame_mbs_only_flag;
    render->info.h264.transform_8x8_mode_flag                = h->ps.pps->transform_8x8_mode;
    render->info.h264.chroma_qp_index_offset                 = h->ps.pps->chroma_qp_index_offset[0];
    render->info.h264.second_chroma_qp_index_offset          = h->ps.pps->chroma_qp_index_offset[1];
    render->info.h264.pic_init_qp_minus26                    = h->ps.pps->init_qp - 26;
    render->info.h264.num_ref_idx_l0_active_minus1           = h->ps.pps->ref_count[0] - 1;
    render->info.h264.num_ref_idx_l1_active_minus1           = h->ps.pps->ref_count[1] - 1;
    render->info.h264.log2_max_frame_num_minus4              = h->ps.sps->log2_max_frame_num - 4;
    render->info.h264.pic_order_cnt_type                     = h->ps.sps->poc_type;
    render->info.h264.log2_max_pic_order_cnt_lsb_minus4      = h->ps.sps->poc_type ? 0 : h->ps.sps->log2_max_poc_lsb - 4;
    render->info.h264.delta_pic_order_always_zero_flag       = h->ps.sps->delta_pic_order_always_zero_flag;
    render->info.h264.direct_8x8_inference_flag              = h->ps.sps->direct_8x8_inference_flag;
    render->info.h264.entropy_coding_mode_flag               = h->ps.pps->cabac;
    render->info.h264.pic_order_present_flag                 = h->ps.pps->pic_order_present;
    render->info.h264.deblocking_filter_control_present_flag = h->ps.pps->deblocking_filter_parameters_present;
    render->info.h264.redundant_pic_cnt_present_flag         = h->ps.pps->redundant_pic_cnt_present;
    memcpy(render->info.h264.scaling_lists_4x4, h->ps.pps->scaling_matrix4, sizeof(render->info.h264.scaling_lists_4x4));
    memcpy(render->info.h264.scaling_lists_8x8[0], h->ps.pps->scaling_matrix8[0], sizeof(render->info.h264.scaling_lists_8x8[0]));
    memcpy(render->info.h264.scaling_lists_8x8[1], h->ps.pps->scaling_matrix8[3], sizeof(render->info.h264.scaling_lists_8x8[0]));

    ff_h264_draw_horiz_band(h, &h->slice_ctx[0], 0, h->avctx->height);
    render->bitstream_buffers_used = 0;
}
#endif /* CONFIG_H264_VDPAU_DECODER */

#if CONFIG_MPEG_VDPAU_DECODER || CONFIG_MPEG1_VDPAU_DECODER
void ff_vdpau_mpeg_picture_complete(MpegEncContext *s, const uint8_t *buf,
                                    int buf_size, int slice_count)
{
    struct vdpau_render_state *render, *last, *next;
    int i;

    if (!s->current_picture_ptr) return;

    render = (struct vdpau_render_state *)s->current_picture_ptr->f->data[0];
    assert(render);

    /* fill VdpPictureInfoMPEG1Or2 struct */
    render->info.mpeg.picture_structure          = s->picture_structure;
    render->info.mpeg.picture_coding_type        = s->pict_type;
    render->info.mpeg.intra_dc_precision         = s->intra_dc_precision;
    render->info.mpeg.frame_pred_frame_dct       = s->frame_pred_frame_dct;
    render->info.mpeg.concealment_motion_vectors = s->concealment_motion_vectors;
    render->info.mpeg.intra_vlc_format           = s->intra_vlc_format;
    render->info.mpeg.alternate_scan             = s->alternate_scan;
    render->info.mpeg.q_scale_type               = s->q_scale_type;
    render->info.mpeg.top_field_first            = s->top_field_first;
    render->info.mpeg.full_pel_forward_vector    = s->full_pel[0]; // MPEG-1 only.  Set 0 for MPEG-2
    render->info.mpeg.full_pel_backward_vector   = s->full_pel[1]; // MPEG-1 only.  Set 0 for MPEG-2
    render->info.mpeg.f_code[0][0]               = s->mpeg_f_code[0][0]; // For MPEG-1 fill both horiz. & vert.
    render->info.mpeg.f_code[0][1]               = s->mpeg_f_code[0][1];
    render->info.mpeg.f_code[1][0]               = s->mpeg_f_code[1][0];
    render->info.mpeg.f_code[1][1]               = s->mpeg_f_code[1][1];
    for (i = 0; i < 64; ++i) {
        render->info.mpeg.intra_quantizer_matrix[i]     = s->intra_matrix[i];
        render->info.mpeg.non_intra_quantizer_matrix[i] = s->inter_matrix[i];
    }

    render->info.mpeg.forward_reference          = VDP_INVALID_HANDLE;
    render->info.mpeg.backward_reference         = VDP_INVALID_HANDLE;

    switch(s->pict_type){
    case  AV_PICTURE_TYPE_B:
        next = (struct vdpau_render_state *)s->next_picture.f->data[0];
        assert(next);
        render->info.mpeg.backward_reference     = next->surface;
        // no return here, going to set forward prediction
    case  AV_PICTURE_TYPE_P:
        last = (struct vdpau_render_state *)s->last_picture.f->data[0];
        if (!last) // FIXME: Does this test make sense?
            last = render; // predict second field from the first
        render->info.mpeg.forward_reference      = last->surface;
    }

    ff_vdpau_add_data_chunk(s->current_picture_ptr->f->data[0], buf, buf_size);

    render->info.mpeg.slice_count                = slice_count;

    if (slice_count)
        ff_mpeg_draw_horiz_band(s, 0, s->avctx->height);
    render->bitstream_buffers_used               = 0;
}
#endif /* CONFIG_MPEG_VDPAU_DECODER || CONFIG_MPEG1_VDPAU_DECODER */

#if CONFIG_VC1_VDPAU_DECODER
void ff_vdpau_vc1_decode_picture(MpegEncContext *s, const uint8_t *buf,
                                 int buf_size)
{
    VC1Context *v = s->avctx->priv_data;
    struct vdpau_render_state *render, *last, *next;

    render = (struct vdpau_render_state *)s->current_picture.f->data[0];
    assert(render);

    /*  fill LvPictureInfoVC1 struct */
    render->info.vc1.frame_coding_mode  = v->fcm ? v->fcm + 1 : 0;
    render->info.vc1.postprocflag       = v->postprocflag;
    render->info.vc1.pulldown           = v->broadcast;
    render->info.vc1.interlace          = v->interlace;
    render->info.vc1.tfcntrflag         = v->tfcntrflag;
    render->info.vc1.finterpflag        = v->finterpflag;
    render->info.vc1.psf                = v->psf;
    render->info.vc1.dquant             = v->dquant;
    render->info.vc1.panscan_flag       = v->panscanflag;
    render->info.vc1.refdist_flag       = v->refdist_flag;
    render->info.vc1.quantizer          = v->quantizer_mode;
    render->info.vc1.extended_mv        = v->extended_mv;
    render->info.vc1.extended_dmv       = v->extended_dmv;
    render->info.vc1.overlap            = v->overlap;
    render->info.vc1.vstransform        = v->vstransform;
    render->info.vc1.loopfilter         = v->s.loop_filter;
    render->info.vc1.fastuvmc           = v->fastuvmc;
    render->info.vc1.range_mapy_flag    = v->range_mapy_flag;
    render->info.vc1.range_mapy         = v->range_mapy;
    render->info.vc1.range_mapuv_flag   = v->range_mapuv_flag;
    render->info.vc1.range_mapuv        = v->range_mapuv;
    /* Specific to simple/main profile only */
    render->info.vc1.multires           = v->multires;
    render->info.vc1.syncmarker         = v->resync_marker;
    render->info.vc1.rangered           = v->rangered | (v->rangeredfrm << 1);
    render->info.vc1.maxbframes         = v->s.max_b_frames;

    render->info.vc1.deblockEnable      = v->postprocflag & 1;
    render->info.vc1.pquant             = v->pq;

    render->info.vc1.forward_reference  = VDP_INVALID_HANDLE;
    render->info.vc1.backward_reference = VDP_INVALID_HANDLE;

    if (v->bi_type)
        render->info.vc1.picture_type = 4;
    else
        render->info.vc1.picture_type = s->pict_type - 1 + s->pict_type / 3;

    switch(s->pict_type){
    case  AV_PICTURE_TYPE_B:
        next = (struct vdpau_render_state *)s->next_picture.f->data[0];
        assert(next);
        render->info.vc1.backward_reference = next->surface;
        // no break here, going to set forward prediction
    case  AV_PICTURE_TYPE_P:
        last = (struct vdpau_render_state *)s->last_picture.f->data[0];
        if (!last) // FIXME: Does this test make sense?
            last = render; // predict second field from the first
        render->info.vc1.forward_reference = last->surface;
    }

    ff_vdpau_add_data_chunk(s->current_picture_ptr->f->data[0], buf, buf_size);

    render->info.vc1.slice_count          = 1;

    ff_mpeg_draw_horiz_band(s, 0, s->avctx->height);
    render->bitstream_buffers_used        = 0;
}
#endif /* (CONFIG_VC1_VDPAU_DECODER */

#if CONFIG_MPEG4_VDPAU_DECODER
void ff_vdpau_mpeg4_decode_picture(Mpeg4DecContext *ctx, const uint8_t *buf,
                                   int buf_size)
{
    MpegEncContext *s = &ctx->m;
    struct vdpau_render_state *render, *last, *next;
    int i;

    if (!s->current_picture_ptr) return;

    render = (struct vdpau_render_state *)s->current_picture_ptr->f->data[0];
    assert(render);

    /* fill VdpPictureInfoMPEG4Part2 struct */
    render->info.mpeg4.trd[0]                            = s->pp_time;
    render->info.mpeg4.trb[0]                            = s->pb_time;
    render->info.mpeg4.trd[1]                            = s->pp_field_time >> 1;
    render->info.mpeg4.trb[1]                            = s->pb_field_time >> 1;
    render->info.mpeg4.vop_time_increment_resolution     = s->avctx->time_base.den;
    render->info.mpeg4.vop_coding_type                   = 0;
    render->info.mpeg4.vop_fcode_forward                 = s->f_code;
    render->info.mpeg4.vop_fcode_backward                = s->b_code;
    render->info.mpeg4.resync_marker_disable             = !ctx->resync_marker;
    render->info.mpeg4.interlaced                        = !s->progressive_sequence;
    render->info.mpeg4.quant_type                        = s->mpeg_quant;
    render->info.mpeg4.quarter_sample                    = s->quarter_sample;
    render->info.mpeg4.short_video_header                = s->avctx->codec->id == AV_CODEC_ID_H263;
    render->info.mpeg4.rounding_control                  = s->no_rounding;
    render->info.mpeg4.alternate_vertical_scan_flag      = s->alternate_scan;
    render->info.mpeg4.top_field_first                   = s->top_field_first;
    for (i = 0; i < 64; ++i) {
        render->info.mpeg4.intra_quantizer_matrix[i]     = s->intra_matrix[i];
        render->info.mpeg4.non_intra_quantizer_matrix[i] = s->inter_matrix[i];
    }
    render->info.mpeg4.forward_reference                 = VDP_INVALID_HANDLE;
    render->info.mpeg4.backward_reference                = VDP_INVALID_HANDLE;

    switch (s->pict_type) {
    case AV_PICTURE_TYPE_B:
        next = (struct vdpau_render_state *)s->next_picture.f->data[0];
        assert(next);
        render->info.mpeg4.backward_reference     = next->surface;
        render->info.mpeg4.vop_coding_type        = 2;
        // no break here, going to set forward prediction
    case AV_PICTURE_TYPE_P:
        last = (struct vdpau_render_state *)s->last_picture.f->data[0];
        assert(last);
        render->info.mpeg4.forward_reference      = last->surface;
    }

    ff_vdpau_add_data_chunk(s->current_picture_ptr->f->data[0], buf, buf_size);

    ff_mpeg_draw_horiz_band(s, 0, s->avctx->height);
    render->bitstream_buffers_used = 0;
}
#endif /* CONFIG_MPEG4_VDPAU_DECODER */
#endif /* FF_API_VDPAU */

#if FF_API_VDPAU_PROFILE
int av_vdpau_get_profile(AVCodecContext *avctx, VdpDecoderProfile *profile)
{
#define PROFILE(prof)                      \
do {                                       \
    *profile = VDP_DECODER_PROFILE_##prof; \
    return 0;                              \
} while (0)

    switch (avctx->codec_id) {
    case AV_CODEC_ID_MPEG1VIDEO:               PROFILE(MPEG1);
    case AV_CODEC_ID_MPEG2VIDEO:
        switch (avctx->profile) {
        case FF_PROFILE_MPEG2_MAIN:            PROFILE(MPEG2_MAIN);
        case FF_PROFILE_MPEG2_SIMPLE:          PROFILE(MPEG2_SIMPLE);
        default:                               return AVERROR(EINVAL);
        }
    case AV_CODEC_ID_H263:                     PROFILE(MPEG4_PART2_ASP);
    case AV_CODEC_ID_MPEG4:
        switch (avctx->profile) {
        case FF_PROFILE_MPEG4_SIMPLE:          PROFILE(MPEG4_PART2_SP);
        case FF_PROFILE_MPEG4_ADVANCED_SIMPLE: PROFILE(MPEG4_PART2_ASP);
        default:                               return AVERROR(EINVAL);
        }
    case AV_CODEC_ID_H264:
        switch (avctx->profile & ~FF_PROFILE_H264_INTRA) {
        case FF_PROFILE_H264_BASELINE:         PROFILE(H264_BASELINE);
        case FF_PROFILE_H264_CONSTRAINED_BASELINE:
        case FF_PROFILE_H264_MAIN:             PROFILE(H264_MAIN);
        case FF_PROFILE_H264_HIGH:             PROFILE(H264_HIGH);
#ifdef VDP_DECODER_PROFILE_H264_EXTENDED
        case FF_PROFILE_H264_EXTENDED:         PROFILE(H264_EXTENDED);
#endif
        default:                               return AVERROR(EINVAL);
        }
    case AV_CODEC_ID_WMV3:
    case AV_CODEC_ID_VC1:
        switch (avctx->profile) {
        case FF_PROFILE_VC1_SIMPLE:            PROFILE(VC1_SIMPLE);
        case FF_PROFILE_VC1_MAIN:              PROFILE(VC1_MAIN);
        case FF_PROFILE_VC1_ADVANCED:          PROFILE(VC1_ADVANCED);
        default:                               return AVERROR(EINVAL);
        }
    }
    return AVERROR(EINVAL);
#undef PROFILE
}
#endif /* FF_API_VDPAU_PROFILE */

AVVDPAUContext *av_vdpau_alloc_context(void)
{
    return av_mallocz(sizeof(AVVDPAUContext));
}

int av_vdpau_bind_context(AVCodecContext *avctx, VdpDevice device,
                          VdpGetProcAddress *get_proc, unsigned flags)
{
    VDPAUHWContext *hwctx;

    if (flags & ~(AV_HWACCEL_FLAG_IGNORE_LEVEL|AV_HWACCEL_FLAG_ALLOW_HIGH_DEPTH))
        return AVERROR(EINVAL);

    if (av_reallocp(&avctx->hwaccel_context, sizeof(*hwctx)))
        return AVERROR(ENOMEM);

    hwctx = avctx->hwaccel_context;

    memset(hwctx, 0, sizeof(*hwctx));
    hwctx->context.decoder  = VDP_INVALID_HANDLE;
    hwctx->device           = device;
    hwctx->get_proc_address = get_proc;
    hwctx->flags            = flags;
    hwctx->reset            = 1;
    return 0;
}

/* @}*/
