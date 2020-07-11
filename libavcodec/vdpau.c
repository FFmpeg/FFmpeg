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
#include "decode.h"
#include "internal.h"
#include "h264dec.h"
#include "vc1.h"
#include "vdpau.h"
#include "vdpau_internal.h"

// XXX: at the time of adding this ifdefery, av_assert* wasn't use outside.
// When dropping it, make sure other av_assert* were not added since then.

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
    case AV_PIX_FMT_YUV420P10:
    case AV_PIX_FMT_YUV420P12:
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
    case AV_PIX_FMT_YUV444P10:
    case AV_PIX_FMT_YUV444P12:
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

int ff_vdpau_common_frame_params(AVCodecContext *avctx,
                                 AVBufferRef *hw_frames_ctx)
{
    AVHWFramesContext *hw_frames = (AVHWFramesContext*)hw_frames_ctx->data;
    VdpChromaType type;
    uint32_t width;
    uint32_t height;

    if (av_vdpau_get_surface_parameters(avctx, &type, &width, &height))
        return AVERROR(EINVAL);

    hw_frames->format    = AV_PIX_FMT_VDPAU;
    hw_frames->sw_format = avctx->sw_pix_fmt;
    hw_frames->width     = width;
    hw_frames->height    = height;

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
    VdpGetInformationString *info;
    const char *info_string;
    void *func;
    VdpStatus status;
    VdpBool supported;
    uint32_t max_level, max_mb, max_width, max_height;
    VdpChromaType type;
    uint32_t width;
    uint32_t height;
    int ret;

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
        AVHWFramesContext *frames_ctx;
        AVVDPAUDeviceContext *dev_ctx;

        ret = ff_decode_get_hw_frames_ctx(avctx, AV_HWDEVICE_TYPE_VDPAU);
        if (ret < 0)
            return ret;

        frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
        dev_ctx = frames_ctx->device_ctx->hwctx;

        vdctx->device           = dev_ctx->device;
        vdctx->get_proc_address = dev_ctx->get_proc_address;

        if (avctx->hwaccel_flags & AV_HWACCEL_FLAG_IGNORE_LEVEL)
            level = 0;
    }

    if (level < 0)
        return AVERROR(ENOTSUP);

    status = vdctx->get_proc_address(vdctx->device,
                                     VDP_FUNC_ID_GET_INFORMATION_STRING,
                                     &func);
    if (status != VDP_STATUS_OK)
        return vdpau_error(status);
    else
        info = func;

    status = info(&info_string);
    if (status != VDP_STATUS_OK)
        return vdpau_error(status);
    if (avctx->codec_id == AV_CODEC_ID_HEVC && strncmp(info_string, "NVIDIA ", 7) == 0 &&
        !(avctx->hwaccel_flags & AV_HWACCEL_FLAG_ALLOW_PROFILE_MISMATCH)) {
        int driver_version = 0;
        sscanf(info_string, "NVIDIA VDPAU Driver Shared Library  %d", &driver_version);
        if (driver_version < 410) {
            av_log(avctx, AV_LOG_VERBOSE, "HEVC with NVIDIA VDPAU drivers is buggy, skipping.\n");
            return AVERROR(ENOTSUP);
        }
    }

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

    if (hwctx && !hwctx->render && hwctx->render2) {
        status = hwctx->render2(avctx, frame, (void *)&pic_ctx->info,
                                pic_ctx->bitstream_buffers_used, pic_ctx->bitstream_buffers);
    } else
    status = vdctx->render(vdctx->decoder, surf, &pic_ctx->info,
                           pic_ctx->bitstream_buffers_used,
                           pic_ctx->bitstream_buffers);

    av_freep(&pic_ctx->bitstream_buffers);

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
    return av_mallocz(sizeof(VDPAUHWContext));
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
