/*
 * VDA HW acceleration.
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

#include <pthread.h>
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFData.h>
#include <CoreFoundation/CFString.h>

#include "avcodec.h"
#include "vda_internal.h"

/**
 * \addtogroup VDA_Decoding
 *
 * @{
 */

/* Mutex manager callback. */
static int vda_lock_operation(void **mtx, enum AVLockOp op)
{
    switch (op) {
    case AV_LOCK_CREATE:
        *mtx = av_malloc(sizeof(pthread_mutex_t));
        if (!*mtx)
            return 1;
        return !!pthread_mutex_init(*mtx, NULL);
    case AV_LOCK_OBTAIN:
        return !!pthread_mutex_lock(*mtx);
    case AV_LOCK_RELEASE:
        return !!pthread_mutex_unlock(*mtx);
    case AV_LOCK_DESTROY:
        pthread_mutex_destroy(*mtx);
        av_freep(mtx);
        return 0;
    }
    return 1;
}

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

    vda_lock_operation(&vda_ctx->queue_mutex, AV_LOCK_OBTAIN);

    while (vda_ctx->queue) {
        top_frame = vda_ctx->queue;
        vda_ctx->queue = top_frame->next_frame;
        ff_vda_release_vda_frame(top_frame);
    }

    vda_lock_operation(&vda_ctx->queue_mutex, AV_LOCK_RELEASE);
}


/* Decoder callback that adds the vda frame to the queue in display order. */
static void vda_decoder_callback (void *vda_hw_ctx,
                                  CFDictionaryRef user_info,
                                  OSStatus status,
                                  uint32_t infoFlags,
                                  CVImageBufferRef image_buffer)
{
    struct vda_context *vda_ctx = (struct vda_context*)vda_hw_ctx;
    vda_frame *new_frame;
    vda_frame *queue_walker;

    if (!image_buffer)
        return;

    if (vda_ctx->cv_pix_fmt_type != CVPixelBufferGetPixelFormatType(image_buffer))
        return;

    new_frame = av_mallocz(sizeof(vda_frame));
    if (!new_frame)
        return;

    new_frame->next_frame = NULL;
    new_frame->cv_buffer = CVPixelBufferRetain(image_buffer);
    new_frame->pts = vda_pts_from_dictionary(user_info);

    vda_lock_operation(&vda_ctx->queue_mutex, AV_LOCK_OBTAIN);

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

    vda_lock_operation(&vda_ctx->queue_mutex, AV_LOCK_RELEASE);
}

int ff_vda_create_decoder(struct vda_context *vda_ctx,
                          uint8_t *extradata,
                          int extradata_size)
{
    OSStatus status = kVDADecoderNoErr;
    CFNumberRef height;
    CFNumberRef width;
    CFNumberRef format;
    CFDataRef avc_data;
    CFMutableDictionaryRef config_info;
    CFMutableDictionaryRef buffer_attributes;
    CFMutableDictionaryRef io_surface_properties;
    CFNumberRef cv_pix_fmt;

    vda_ctx->bitstream = NULL;
    vda_ctx->ref_size = 0;

    if (av_lockmgr_register(vda_lock_operation))
        return -1;

    vda_lock_operation(&vda_ctx->queue_mutex, AV_LOCK_CREATE);

    config_info = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                            4,
                                            &kCFTypeDictionaryKeyCallBacks,
                                            &kCFTypeDictionaryValueCallBacks);

    height = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &vda_ctx->height);
    width = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &vda_ctx->width);
    format = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &vda_ctx->format);
    avc_data = CFDataCreate(kCFAllocatorDefault, extradata, extradata_size);

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

    if (kVDADecoderNoErr != status)
        return status;

    return 0;
}

int ff_vda_destroy_decoder(struct vda_context *vda_ctx)
{
    OSStatus status = kVDADecoderNoErr;

    if (vda_ctx->decoder)
        status = VDADecoderDestroy(vda_ctx->decoder);

    vda_clear_queue(vda_ctx);

    if (vda_ctx->queue_mutex)
        vda_lock_operation(&vda_ctx->queue_mutex, AV_LOCK_DESTROY);

    if (vda_ctx->bitstream)
        av_freep(&vda_ctx->bitstream);

    if (kVDADecoderNoErr != status)
        return status;

    return 0;
}

vda_frame *ff_vda_queue_pop(struct vda_context *vda_ctx)
{
    vda_frame *top_frame;

    if (!vda_ctx->queue)
        return NULL;

    vda_lock_operation(&vda_ctx->queue_mutex, AV_LOCK_OBTAIN);
    top_frame = vda_ctx->queue;
    vda_ctx->queue = top_frame->next_frame;
    vda_lock_operation(&vda_ctx->queue_mutex, AV_LOCK_RELEASE);

    return top_frame;
}

void ff_vda_release_vda_frame(vda_frame *frame)
{
    if (frame) {
        CVPixelBufferRelease(frame->cv_buffer);
        av_freep(&frame);
    }
}

int ff_vda_decoder_decode(struct vda_context *vda_ctx,
                          uint8_t *bitstream,
                          int bitstream_size,
                          int64_t frame_pts)
{
    OSStatus status = kVDADecoderNoErr;
    CFDictionaryRef user_info;
    CFDataRef coded_frame;

    coded_frame = CFDataCreate(kCFAllocatorDefault, bitstream, bitstream_size);
    user_info = vda_dictionary_with_pts(frame_pts);

    status = VDADecoderDecode(vda_ctx->decoder, 0, coded_frame, user_info);

    CFRelease(user_info);
    CFRelease(coded_frame);

    if (kVDADecoderNoErr != status)
        return status;

    return 0;
}

/* @} */
