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

#if FF_API_VDA_ASYNC
#include <CoreFoundation/CFString.h>

/* Helper to create a dictionary according to the given pts. */
static CFDictionaryRef vda_dictionary_with_pts(int64_t i_pts)
{
    CFStringRef key = CFSTR("FF_VDA_DECODER_PTS_KEY");
    CFNumberRef value = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &i_pts);
    CFDictionaryRef user_info = CFDictionaryCreate(kCFAllocatorDefault,
                                                   (const void **)&key,
                                                   (const void **)&value,
                                                   1,
                                                   &kCFTypeDictionaryKeyCallBacks,
                                                   &kCFTypeDictionaryValueCallBacks);
    CFRelease(value);
    return user_info;
}

/* Helper to retrieve the pts from the given dictionary. */
static int64_t vda_pts_from_dictionary(CFDictionaryRef user_info)
{
    CFNumberRef pts;
    int64_t outValue = 0;

    if (!user_info)
        return 0;

    pts = CFDictionaryGetValue(user_info, CFSTR("FF_VDA_DECODER_PTS_KEY"));

    if (pts)
        CFNumberGetValue(pts, kCFNumberSInt64Type, &outValue);

    return outValue;
}

/* Removes and releases all frames from the queue. */
static void vda_clear_queue(struct vda_context *vda_ctx)
{
    vda_frame *top_frame;

    pthread_mutex_lock(&vda_ctx->queue_mutex);

    while (vda_ctx->queue) {
        top_frame = vda_ctx->queue;
        vda_ctx->queue = top_frame->next_frame;
        ff_vda_release_vda_frame(top_frame);
    }

    pthread_mutex_unlock(&vda_ctx->queue_mutex);
}

static int vda_decoder_decode(struct vda_context *vda_ctx,
                              uint8_t *bitstream,
                              int bitstream_size,
                              int64_t frame_pts)
{
    OSStatus status;
    CFDictionaryRef user_info;
    CFDataRef coded_frame;

    coded_frame = CFDataCreate(kCFAllocatorDefault, bitstream, bitstream_size);
    user_info = vda_dictionary_with_pts(frame_pts);

    status = VDADecoderDecode(vda_ctx->decoder, 0, coded_frame, user_info);

    CFRelease(user_info);
    CFRelease(coded_frame);

    return status;
}

vda_frame *ff_vda_queue_pop(struct vda_context *vda_ctx)
{
    vda_frame *top_frame;

    if (!vda_ctx->queue)
        return NULL;

    pthread_mutex_lock(&vda_ctx->queue_mutex);
    top_frame = vda_ctx->queue;
    vda_ctx->queue = top_frame->next_frame;
    pthread_mutex_unlock(&vda_ctx->queue_mutex);

    return top_frame;
}

void ff_vda_release_vda_frame(vda_frame *frame)
{
    if (frame) {
        CVPixelBufferRelease(frame->cv_buffer);
        av_freep(&frame);
    }
}
#endif

/* Decoder callback that adds the vda frame to the queue in display order. */
static void vda_decoder_callback (void *vda_hw_ctx,
                                  CFDictionaryRef user_info,
                                  OSStatus status,
                                  uint32_t infoFlags,
                                  CVImageBufferRef image_buffer)
{
    struct vda_context *vda_ctx = vda_hw_ctx;

    if (!image_buffer)
        return;

    if (vda_ctx->cv_pix_fmt_type != CVPixelBufferGetPixelFormatType(image_buffer))
        return;

    if (vda_ctx->use_sync_decoding) {
        vda_ctx->cv_buffer = CVPixelBufferRetain(image_buffer);
    } else {
        vda_frame *new_frame;
        vda_frame *queue_walker;

        if (!(new_frame = av_mallocz(sizeof(*new_frame))))
            return;

        new_frame->next_frame = NULL;
        new_frame->cv_buffer = CVPixelBufferRetain(image_buffer);
        new_frame->pts = vda_pts_from_dictionary(user_info);

        pthread_mutex_lock(&vda_ctx->queue_mutex);

        queue_walker = vda_ctx->queue;

        if (!queue_walker || (new_frame->pts < queue_walker->pts)) {
            /* we have an empty queue, or this frame earlier than the current queue head */
            new_frame->next_frame = queue_walker;
            vda_ctx->queue = new_frame;
        } else {
            /* walk the queue and insert this frame where it belongs in display order */
            vda_frame *next_frame;

            while (1) {
                next_frame = queue_walker->next_frame;

                if (!next_frame || (new_frame->pts < next_frame->pts)) {
                    new_frame->next_frame = next_frame;
                    queue_walker->next_frame = new_frame;
                    break;
                }
                queue_walker = next_frame;
            }
        }

        pthread_mutex_unlock(&vda_ctx->queue_mutex);
    }
}

static int vda_sync_decode(struct vda_context *vda_ctx)
{
    OSStatus status;
    CFDataRef coded_frame;
    uint32_t flush_flags = 1 << 0; ///< kVDADecoderFlush_emitFrames

    coded_frame = CFDataCreate(kCFAllocatorDefault,
                               vda_ctx->priv_bitstream,
                               vda_ctx->priv_bitstream_size);

    status = VDADecoderDecode(vda_ctx->decoder, 0, coded_frame, NULL);

    if (kVDADecoderNoErr == status)
        status = VDADecoderFlush(vda_ctx->decoder, flush_flags);

    CFRelease(coded_frame);

    return status;
}

static int start_frame(AVCodecContext *avctx,
                       av_unused const uint8_t *buffer,
                       av_unused uint32_t size)
{
    struct vda_context *vda_ctx = avctx->hwaccel_context;

    if (!vda_ctx->decoder)
        return -1;

    vda_ctx->priv_bitstream_size = 0;

    return 0;
}

static int decode_slice(AVCodecContext *avctx,
                        const uint8_t *buffer,
                        uint32_t size)
{
    struct vda_context *vda_ctx = avctx->hwaccel_context;
    void *tmp;

    if (!vda_ctx->decoder)
        return -1;

    tmp = av_fast_realloc(vda_ctx->priv_bitstream,
                          &vda_ctx->priv_allocated_size,
                          vda_ctx->priv_bitstream_size + size + 4);
    if (!tmp)
        return AVERROR(ENOMEM);

    vda_ctx->priv_bitstream = tmp;

    AV_WB32(vda_ctx->priv_bitstream + vda_ctx->priv_bitstream_size, size);
    memcpy(vda_ctx->priv_bitstream + vda_ctx->priv_bitstream_size + 4, buffer, size);

    vda_ctx->priv_bitstream_size += size + 4;

    return 0;
}

static int end_frame(AVCodecContext *avctx)
{
    H264Context *h                      = avctx->priv_data;
    struct vda_context *vda_ctx         = avctx->hwaccel_context;
    AVFrame *frame                      = &h->s.current_picture_ptr->f;
    int status;

    if (!vda_ctx->decoder || !vda_ctx->priv_bitstream)
        return -1;

    if (vda_ctx->use_sync_decoding) {
        status = vda_sync_decode(vda_ctx);
        frame->data[3] = (void*)vda_ctx->cv_buffer;
    } else {
        status = vda_decoder_decode(vda_ctx, vda_ctx->priv_bitstream,
                                    vda_ctx->priv_bitstream_size,
                                    frame->reordered_opaque);
    }

    if (status)
        av_log(avctx, AV_LOG_ERROR, "Failed to decode frame (%d)\n", status);

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

#if FF_API_VDA_ASYNC
    pthread_mutex_init(&vda_ctx->queue_mutex, NULL);
#endif

    /* Each VCL NAL in the bistream sent to the decoder
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
                              vda_decoder_callback,
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

#if FF_API_VDA_ASYNC
    vda_clear_queue(vda_ctx);
    pthread_mutex_destroy(&vda_ctx->queue_mutex);
#endif
    av_freep(&vda_ctx->priv_bitstream);

    return status;
}

AVHWAccel ff_h264_vda_hwaccel = {
    .name           = "h264_vda",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .pix_fmt        = PIX_FMT_VDA_VLD,
    .start_frame    = start_frame,
    .decode_slice   = decode_slice,
    .end_frame      = end_frame,
};
