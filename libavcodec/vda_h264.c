/*
 * VDA H264 HW acceleration.
 *
 * copyright (c) 2011 Sebastien Zwickert
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

#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFData.h>

#include "vda.h"
#include "libavutil/avutil.h"
#include "h264.h"

struct vda_buffer {
    CVPixelBufferRef cv_buffer;
};
#include "internal.h"
#include "vda_vt_internal.h"

/* Decoder callback that adds the vda frame to the queue in display order. */
static void vda_decoder_callback(void *vda_hw_ctx,
                                 CFDictionaryRef user_info,
                                 OSStatus status,
                                 uint32_t infoFlags,
                                 CVImageBufferRef image_buffer)
{
    struct vda_context *vda_ctx = vda_hw_ctx;

    if (infoFlags & kVDADecodeInfo_FrameDropped)
        vda_ctx->cv_buffer = NULL;

    if (!image_buffer)
        return;

    if (vda_ctx->cv_pix_fmt_type != CVPixelBufferGetPixelFormatType(image_buffer))
        return;

    vda_ctx->cv_buffer = CVPixelBufferRetain(image_buffer);
}

static int vda_sync_decode(VTContext *ctx, struct vda_context *vda_ctx)
{
    OSStatus status;
    CFDataRef coded_frame;
    uint32_t flush_flags = 1 << 0; ///< kVDADecoderFlush_emitFrames

    coded_frame = CFDataCreate(kCFAllocatorDefault,
                               ctx->bitstream,
                               ctx->bitstream_size);

    status = VDADecoderDecode(vda_ctx->decoder, 0, coded_frame, NULL);

    if (kVDADecoderNoErr == status)
        status = VDADecoderFlush(vda_ctx->decoder, flush_flags);

    CFRelease(coded_frame);

    return status;
}


static int vda_old_h264_start_frame(AVCodecContext *avctx,
                                av_unused const uint8_t *buffer,
                                av_unused uint32_t size)
{
    VTContext *vda = avctx->internal->hwaccel_priv_data;
    struct vda_context *vda_ctx = avctx->hwaccel_context;

    if (!vda_ctx->decoder)
        return -1;

    vda->bitstream_size = 0;

    return 0;
}

static int vda_old_h264_decode_slice(AVCodecContext *avctx,
                                 const uint8_t *buffer,
                                 uint32_t size)
{
    VTContext *vda              = avctx->internal->hwaccel_priv_data;
    struct vda_context *vda_ctx = avctx->hwaccel_context;
    void *tmp;

    if (!vda_ctx->decoder)
        return -1;

    tmp = av_fast_realloc(vda->bitstream,
                          &vda->allocated_size,
                          vda->bitstream_size + size + 4);
    if (!tmp)
        return AVERROR(ENOMEM);

    vda->bitstream = tmp;

    AV_WB32(vda->bitstream + vda->bitstream_size, size);
    memcpy(vda->bitstream + vda->bitstream_size + 4, buffer, size);

    vda->bitstream_size += size + 4;

    return 0;
}

static void vda_h264_release_buffer(void *opaque, uint8_t *data)
{
    struct vda_buffer *context = opaque;
    CVPixelBufferRelease(context->cv_buffer);
    av_free(context);
}

static int vda_old_h264_end_frame(AVCodecContext *avctx)
{
    H264Context *h                      = avctx->priv_data;
    VTContext *vda                      = avctx->internal->hwaccel_priv_data;
    struct vda_context *vda_ctx         = avctx->hwaccel_context;
    AVFrame *frame                      = h->cur_pic_ptr->f;
    struct vda_buffer *context;
    AVBufferRef *buffer;
    int status;

    if (!vda_ctx->decoder || !vda->bitstream)
        return -1;

    status = vda_sync_decode(vda, vda_ctx);
    frame->data[3] = (void*)vda_ctx->cv_buffer;

    if (status)
        av_log(avctx, AV_LOG_ERROR, "Failed to decode frame (%d)\n", status);

    if (!vda_ctx->use_ref_buffer || status)
        return status;

    context = av_mallocz(sizeof(*context));
    buffer = av_buffer_create(NULL, 0, vda_h264_release_buffer, context, 0);
    if (!context || !buffer) {
        CVPixelBufferRelease(vda_ctx->cv_buffer);
        av_free(context);
        return -1;
    }

    context->cv_buffer = vda_ctx->cv_buffer;
    frame->buf[3] = buffer;

    return status;
}

int ff_vda_create_decoder(struct vda_context *vda_ctx,
                          uint8_t *extradata,
                          int extradata_size)
{
    OSStatus status;
    CFNumberRef height;
    CFNumberRef width;
    CFNumberRef format;
    CFDataRef avc_data;
    CFMutableDictionaryRef config_info;
    CFMutableDictionaryRef buffer_attributes;
    CFMutableDictionaryRef io_surface_properties;
    CFNumberRef cv_pix_fmt;

    vda_ctx->priv_bitstream = NULL;
    vda_ctx->priv_allocated_size = 0;

    /* Each VCL NAL in the bitstream sent to the decoder
     * is preceded by a 4 bytes length header.
     * Change the avcC atom header if needed, to signal headers of 4 bytes. */
    if (extradata_size >= 4 && (extradata[4] & 0x03) != 0x03) {
        uint8_t *rw_extradata;

        if (!(rw_extradata = av_malloc(extradata_size)))
            return AVERROR(ENOMEM);

        memcpy(rw_extradata, extradata, extradata_size);

        rw_extradata[4] |= 0x03;

        avc_data = CFDataCreate(kCFAllocatorDefault, rw_extradata, extradata_size);

        av_freep(&rw_extradata);
    } else {
        avc_data = CFDataCreate(kCFAllocatorDefault, extradata, extradata_size);
    }

    config_info = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                            4,
                                            &kCFTypeDictionaryKeyCallBacks,
                                            &kCFTypeDictionaryValueCallBacks);

    height   = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &vda_ctx->height);
    width    = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &vda_ctx->width);
    format   = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &vda_ctx->format);

    CFDictionarySetValue(config_info, kVDADecoderConfiguration_Height, height);
    CFDictionarySetValue(config_info, kVDADecoderConfiguration_Width, width);
    CFDictionarySetValue(config_info, kVDADecoderConfiguration_SourceFormat, format);
    CFDictionarySetValue(config_info, kVDADecoderConfiguration_avcCData, avc_data);

    buffer_attributes = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                  2,
                                                  &kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks);
    io_surface_properties = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                      0,
                                                      &kCFTypeDictionaryKeyCallBacks,
                                                      &kCFTypeDictionaryValueCallBacks);
    cv_pix_fmt  = CFNumberCreate(kCFAllocatorDefault,
                                 kCFNumberSInt32Type,
                                 &vda_ctx->cv_pix_fmt_type);
    CFDictionarySetValue(buffer_attributes,
                         kCVPixelBufferPixelFormatTypeKey,
                         cv_pix_fmt);
    CFDictionarySetValue(buffer_attributes,
                         kCVPixelBufferIOSurfacePropertiesKey,
                         io_surface_properties);

    status = VDADecoderCreate(config_info,
                              buffer_attributes,
                              (VDADecoderOutputCallback *)vda_decoder_callback,
                              vda_ctx,
                              &vda_ctx->decoder);

    CFRelease(height);
    CFRelease(width);
    CFRelease(format);
    CFRelease(avc_data);
    CFRelease(config_info);
    CFRelease(io_surface_properties);
    CFRelease(cv_pix_fmt);
    CFRelease(buffer_attributes);

    return status;
}

int ff_vda_destroy_decoder(struct vda_context *vda_ctx)
{
    OSStatus status = kVDADecoderNoErr;

    if (vda_ctx->decoder)
        status = VDADecoderDestroy(vda_ctx->decoder);

    return status;
}

AVHWAccel ff_h264_vda_old_hwaccel = {
    .name           = "h264_vda",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .pix_fmt        = AV_PIX_FMT_VDA_VLD,
    .start_frame    = vda_old_h264_start_frame,
    .decode_slice   = vda_old_h264_decode_slice,
    .end_frame      = vda_old_h264_end_frame,
    .uninit         = ff_videotoolbox_uninit,
    .priv_data_size = sizeof(VTContext),
};

void ff_vda_output_callback(void *opaque,
                            CFDictionaryRef user_info,
                            OSStatus status,
                            uint32_t infoFlags,
                            CVImageBufferRef image_buffer)
{
    AVCodecContext *ctx = opaque;
    VTContext *vda = ctx->internal->hwaccel_priv_data;


    if (vda->frame) {
        CVPixelBufferRelease(vda->frame);
        vda->frame = NULL;
    }

    if (!image_buffer)
        return;

    vda->frame = CVPixelBufferRetain(image_buffer);
}

static int vda_h264_end_frame(AVCodecContext *avctx)
{
    H264Context *h        = avctx->priv_data;
    VTContext *vda        = avctx->internal->hwaccel_priv_data;
    AVVDAContext *vda_ctx = avctx->hwaccel_context;
    AVFrame *frame        = h->cur_pic_ptr->f;
    uint32_t flush_flags  = 1 << 0; ///< kVDADecoderFlush_emitFrames
    CFDataRef coded_frame;
    OSStatus status;

    if (!vda->bitstream_size)
        return AVERROR_INVALIDDATA;


    coded_frame = CFDataCreate(kCFAllocatorDefault,
                               vda->bitstream,
                               vda->bitstream_size);

    status = VDADecoderDecode(vda_ctx->decoder, 0, coded_frame, NULL);

    if (status == kVDADecoderNoErr)
        status = VDADecoderFlush(vda_ctx->decoder, flush_flags);

    CFRelease(coded_frame);

    if (!vda->frame)
        return AVERROR_UNKNOWN;

    if (status != kVDADecoderNoErr) {
        av_log(avctx, AV_LOG_ERROR, "Failed to decode frame (%d)\n", status);
        return AVERROR_UNKNOWN;
    }

    return ff_videotoolbox_buffer_create(vda, frame);
}

int ff_vda_default_init(AVCodecContext *avctx)
{
    AVVDAContext *vda_ctx = avctx->hwaccel_context;
    OSStatus status = kVDADecoderNoErr;
    CFNumberRef height;
    CFNumberRef width;
    CFNumberRef format;
    CFDataRef avc_data;
    CFMutableDictionaryRef config_info;
    CFMutableDictionaryRef buffer_attributes;
    CFMutableDictionaryRef io_surface_properties;
    CFNumberRef cv_pix_fmt;
    int32_t fmt = 'avc1', pix_fmt = vda_ctx->cv_pix_fmt_type;

    // kCVPixelFormatType_420YpCbCr8Planar;

    avc_data = ff_videotoolbox_avcc_extradata_create(avctx);

    config_info = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                            4,
                                            &kCFTypeDictionaryKeyCallBacks,
                                            &kCFTypeDictionaryValueCallBacks);

    height = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &avctx->height);
    width  = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &avctx->width);
    format = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &fmt);
    CFDictionarySetValue(config_info, kVDADecoderConfiguration_Height, height);
    CFDictionarySetValue(config_info, kVDADecoderConfiguration_Width, width);
    CFDictionarySetValue(config_info, kVDADecoderConfiguration_avcCData, avc_data);
    CFDictionarySetValue(config_info, kVDADecoderConfiguration_SourceFormat, format);

    buffer_attributes = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                  2,
                                                  &kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks);
    io_surface_properties = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                                      0,
                                                      &kCFTypeDictionaryKeyCallBacks,
                                                      &kCFTypeDictionaryValueCallBacks);
    cv_pix_fmt      = CFNumberCreate(kCFAllocatorDefault,
                                     kCFNumberSInt32Type,
                                     &pix_fmt);

    CFDictionarySetValue(buffer_attributes,
                         kCVPixelBufferPixelFormatTypeKey,
                         cv_pix_fmt);
    CFDictionarySetValue(buffer_attributes,
                         kCVPixelBufferIOSurfacePropertiesKey,
                         io_surface_properties);

    status = VDADecoderCreate(config_info,
                              buffer_attributes,
                              (VDADecoderOutputCallback *)ff_vda_output_callback,
                              avctx,
                              &vda_ctx->decoder);

    CFRelease(format);
    CFRelease(height);
    CFRelease(width);
    CFRelease(avc_data);
    CFRelease(config_info);
    CFRelease(cv_pix_fmt);
    CFRelease(io_surface_properties);
    CFRelease(buffer_attributes);

    if (status != kVDADecoderNoErr) {
        av_log(avctx, AV_LOG_ERROR, "Cannot initialize VDA %d\n", status);
    }

    switch (status) {
    case kVDADecoderHardwareNotSupportedErr:
    case kVDADecoderFormatNotSupportedErr:
        return AVERROR(ENOSYS);
    case kVDADecoderConfigurationError:
        return AVERROR(EINVAL);
    case kVDADecoderDecoderFailedErr:
        return AVERROR_INVALIDDATA;
    case kVDADecoderNoErr:
        return 0;
    default:
        return AVERROR_UNKNOWN;
    }
}

AVHWAccel ff_h264_vda_hwaccel = {
    .name           = "h264_vda",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .pix_fmt        = AV_PIX_FMT_VDA,
    .alloc_frame    = ff_videotoolbox_alloc_frame,
    .start_frame    = ff_videotoolbox_h264_start_frame,
    .decode_slice   = ff_videotoolbox_h264_decode_slice,
    .end_frame      = vda_h264_end_frame,
    .uninit         = ff_videotoolbox_uninit,
    .priv_data_size = sizeof(VTContext),
};
