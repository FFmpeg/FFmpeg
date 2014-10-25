/*
 * AVCapture input device
 * Copyright (c) 2014 Joseph Benden <joe@thrallingpenguin.com>
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

/**
 * @file
 * AVCapture input device
 * @author Joseph Benden <joe@thrallingpenguin.com>
 */
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#include <pthread.h>

#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "libavformat/internal.h"
#include "libavutil/internal.h"
#include "libavutil/time.h"
#include "avdevice.h"
#import "avfframereceiver.h"

static const int avf_time_base = 100;

static const AVRational avf_time_base_q = {
    .num = 1,
    .den = avf_time_base
};

struct AVFPixelFormatSpec {
    enum AVPixelFormat ff_id;
    OSType avf_id;
};

static const struct AVFPixelFormatSpec avf_pixel_formats[] = {
    { AV_PIX_FMT_MONOBLACK,    kCVPixelFormatType_1Monochrome },
    { AV_PIX_FMT_RGB555BE,     kCVPixelFormatType_16BE555 },
    { AV_PIX_FMT_RGB555LE,     kCVPixelFormatType_16LE555 },
    { AV_PIX_FMT_RGB565BE,     kCVPixelFormatType_16BE565 },
    { AV_PIX_FMT_RGB565LE,     kCVPixelFormatType_16LE565 },
    { AV_PIX_FMT_RGB24,        kCVPixelFormatType_24RGB },
    { AV_PIX_FMT_BGR24,        kCVPixelFormatType_24BGR },
    { AV_PIX_FMT_0RGB,         kCVPixelFormatType_32ARGB },
    { AV_PIX_FMT_BGR0,         kCVPixelFormatType_32BGRA },
    { AV_PIX_FMT_0BGR,         kCVPixelFormatType_32ABGR },
    { AV_PIX_FMT_RGB0,         kCVPixelFormatType_32RGBA },
    { AV_PIX_FMT_BGR48BE,      kCVPixelFormatType_48RGB },
    { AV_PIX_FMT_UYVY422,      kCVPixelFormatType_422YpCbCr8 },
    { AV_PIX_FMT_YUVA444P,     kCVPixelFormatType_4444YpCbCrA8R },
    { AV_PIX_FMT_YUVA444P16LE, kCVPixelFormatType_4444AYpCbCr16 },
    { AV_PIX_FMT_YUV444P,      kCVPixelFormatType_444YpCbCr8 },
    { AV_PIX_FMT_YUV422P16,    kCVPixelFormatType_422YpCbCr16 },
    { AV_PIX_FMT_YUV422P10,    kCVPixelFormatType_422YpCbCr10 },
    { AV_PIX_FMT_YUV444P10,    kCVPixelFormatType_444YpCbCr10 },
    { AV_PIX_FMT_YUV420P,      kCVPixelFormatType_420YpCbCr8Planar },
    { AV_PIX_FMT_NV12,         kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange },
    { AV_PIX_FMT_YUYV422,      kCVPixelFormatType_422YpCbCr8_yuvs },
    { AV_PIX_FMT_GRAY8,        kCVPixelFormatType_OneComponent8 },
    { AV_PIX_FMT_NONE, 0 }
};

static void destroy_context(AVFContext* ctx)
{
    [ctx->capture_session stopRunning];

    [ctx->capture_session release];
    [ctx->video_output    release];
    [ctx->avf_delegate    release];

    ctx->capture_session = NULL;
    ctx->video_output    = NULL;
    ctx->avf_delegate    = NULL;

    pthread_mutex_destroy(&ctx->frame_lock);
    pthread_cond_destroy(&ctx->frame_wait_cond);

    if (ctx->current_frame) {
        CFRelease(ctx->current_frame);
    }
}

static int avf_read_header(AVFormatContext *s)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    AVFContext *ctx         = (AVFContext*)s->priv_data;
    ctx->first_pts          = av_gettime();

    pthread_mutex_init(&ctx->frame_lock, NULL);
    pthread_cond_init(&ctx->frame_wait_cond, NULL);

    // List devices if requested
    if (ctx->list_devices) {
#define MAX_DISPLAYS 32
      av_log(ctx, AV_LOG_INFO, "AVFoundation video devices:\n");
      CGDirectDisplayID displays[MAX_DISPLAYS];
      uint32_t numDisplays;
      uint32_t i;
      CGGetActiveDisplayList(MAX_DISPLAYS, displays, &numDisplays);
      for(i=0; i<numDisplays; i++) {
          CGDisplayModeRef mode;
          CFIndex index, count;
          CFArrayRef modeList;
          modeList=CGDisplayCopyAllDisplayModes(displays[i], NULL);
          count=CFArrayGetCount(modeList);
          NSLog(@"\n\nmode --");
          for(index=0;index<count;index++) {
              mode=(CGDisplayModeRef)CFArrayGetValueAtIndex(modeList, index);
              long h=0, w=0;
              h=CGDisplayModeGetHeight(mode);
              w=CGDisplayModeGetWidth(mode);
              uint32_t flags=CGDisplayModeGetIOFlags(mode);
              NSLog(@"flags: %d", flags);
              NSLog(@"w, h: %ld, %ld", w, h);
              //test mode here against required params and set, using:
              //CGDisplaySetDisplayMode(displays[i], mode, NULL);
            }
          CFRelease(modeList);
        }
      goto fail;
    }

    CGDirectDisplayID displayId = kCGDirectMainDisplay;

    if (ctx->video_device_index >= 0) {
      CGDirectDisplayID displays[MAX_DISPLAYS];
      uint32_t numDisplays;
      CGGetActiveDisplayList(MAX_DISPLAYS, displays, &numDisplays);

      if (ctx->video_device_index >= numDisplays) {
        av_log(ctx, AV_LOG_ERROR, "Invalid device index\n");
        goto fail;
      }

      displayId = displays[ctx->video_device_index];
    }

    // Initialize capture session
    ctx->capture_session = [[AVCaptureSession alloc] init];

    ctx->capture_session.sessionPreset = AVCaptureSessionPresetiFrame1280x720;

    //CGDirectDisplayID displayId = kCGDirectMainDisplay;

    AVCaptureScreenInput *input = [[[AVCaptureScreenInput alloc] initWithDisplayID:displayId] autorelease];
    if (!input) {
        [ctx->capture_session release];
        ctx->capture_session = nil;
        goto fail;
    }

    if ([ctx->capture_session canAddInput:input]) {
        [ctx->capture_session addInput:input];
    } else {
        av_log(s, AV_LOG_ERROR, "can't add video input to capture session\n");
        goto fail;
    }

    // Attaching output
    ctx->video_output = [[AVCaptureVideoDataOutput alloc] init];

    if (!ctx->video_output) {
        av_log(s, AV_LOG_ERROR, "Failed to init AV video output\n");
        goto fail;
    }

    // select pixel format
    struct AVFPixelFormatSpec pxl_fmt_spec;
    pxl_fmt_spec.ff_id = AV_PIX_FMT_NONE;

    for (int i = 0; avf_pixel_formats[i].ff_id != AV_PIX_FMT_NONE; i++) {
        if (ctx->pixel_format == avf_pixel_formats[i].ff_id) {
            pxl_fmt_spec = avf_pixel_formats[i];
            break;
        }
    }

    // check if selected pixel format is supported by AVFoundation
    if (pxl_fmt_spec.ff_id == AV_PIX_FMT_NONE) {
        av_log(s, AV_LOG_ERROR, "Selected pixel format (%s) is not supported by AVFoundation.\n",
               av_get_pix_fmt_name(pxl_fmt_spec.ff_id));
        goto fail;
    }

    // check if the pixel format is available for this device
    if ([[ctx->video_output availableVideoCVPixelFormatTypes] indexOfObject:[NSNumber numberWithInt:pxl_fmt_spec.avf_id]] == NSNotFound) {
        av_log(s, AV_LOG_ERROR, "Selected pixel format (%s) is not supported by the input device.\n",
               av_get_pix_fmt_name(pxl_fmt_spec.ff_id));

        pxl_fmt_spec.ff_id = AV_PIX_FMT_NONE;

        av_log(s, AV_LOG_ERROR, "Supported pixel formats:\n");
        for (NSNumber *pxl_fmt in [ctx->video_output availableVideoCVPixelFormatTypes]) {
            struct AVFPixelFormatSpec pxl_fmt_dummy;
            pxl_fmt_dummy.ff_id = AV_PIX_FMT_NONE;
            for (int i = 0; avf_pixel_formats[i].ff_id != AV_PIX_FMT_NONE; i++) {
                if ([pxl_fmt intValue] == avf_pixel_formats[i].avf_id) {
                    pxl_fmt_dummy = avf_pixel_formats[i];
                    break;
                }
            }

            if (pxl_fmt_dummy.ff_id != AV_PIX_FMT_NONE) {
                av_log(s, AV_LOG_ERROR, "  %s\n", av_get_pix_fmt_name(pxl_fmt_dummy.ff_id));

                // select first supported pixel format instead of user selected (or default) pixel format
                if (pxl_fmt_spec.ff_id == AV_PIX_FMT_NONE) {
                    pxl_fmt_spec = pxl_fmt_dummy;
                }
            }
        }

        // fail if there is no appropriate pixel format or print a warning about overriding the pixel format
        if (pxl_fmt_spec.ff_id == AV_PIX_FMT_NONE) {
            goto fail;
        } else {
            av_log(s, AV_LOG_WARNING, "Overriding selected pixel format to use %s instead.\n",
                   av_get_pix_fmt_name(pxl_fmt_spec.ff_id));
        }
    }


    NSNumber     *pixel_format = [NSNumber numberWithUnsignedInt:pxl_fmt_spec.avf_id];
    NSDictionary *capture_dict = [NSDictionary dictionaryWithObject:pixel_format
                                               forKey:(id)kCVPixelBufferPixelFormatTypeKey];

    [ctx->video_output setVideoSettings:capture_dict];
    [ctx->video_output setAlwaysDiscardsLateVideoFrames:YES];

    ctx->avf_delegate = [[AVFFrameReceiver alloc] initWithContext:ctx];

    dispatch_queue_t queue = dispatch_queue_create("avf_queue", NULL);
    [ctx->video_output setSampleBufferDelegate:ctx->avf_delegate queue:queue];
    dispatch_release(queue);

    if ([ctx->capture_session canAddOutput:ctx->video_output]) {
        [ctx->capture_session addOutput:ctx->video_output];
    } else {
        av_log(s, AV_LOG_ERROR, "can't add video output to capture session\n");
        goto fail;
    }

    [ctx->capture_session startRunning];

    // Take stream info from the first frame.
    while (ctx->frames_captured < 1) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, YES);
    }

    lock_frames(ctx);

    AVStream* stream = avformat_new_stream(s, NULL);

    if (!stream) {
        goto fail;
    }

    avpriv_set_pts_info(stream, 64, 1, avf_time_base);

    CVImageBufferRef image_buffer = CMSampleBufferGetImageBuffer(ctx->current_frame);
    CGSize image_buffer_size      = CVImageBufferGetEncodedSize(image_buffer);

    stream->codec->codec_id   = AV_CODEC_ID_RAWVIDEO;
    stream->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codec->width      = (int)image_buffer_size.width;
    stream->codec->height     = (int)image_buffer_size.height;
    stream->codec->pix_fmt    = pxl_fmt_spec.ff_id;

    CFRelease(ctx->current_frame);
    ctx->current_frame = nil;

    unlock_frames(ctx);
    [pool release];
    return 0;

fail:
    [pool release];
    destroy_context(ctx);
    return AVERROR(EIO);
}

static int avf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVFContext* ctx = (AVFContext*)s->priv_data;

    do {
        lock_frames(ctx);

        CVImageBufferRef image_buffer = CMSampleBufferGetImageBuffer(ctx->current_frame);

        if (ctx->current_frame != nil) {
            if (av_new_packet(pkt, (int)CVPixelBufferGetDataSize(image_buffer)) < 0) {
                return AVERROR(EIO);
            }

            pkt->pts = pkt->dts = av_rescale_q(av_gettime() - ctx->first_pts,
                                               AV_TIME_BASE_Q,
                                               avf_time_base_q);
            pkt->stream_index  = 0;
            pkt->flags        |= AV_PKT_FLAG_KEY;

            CVPixelBufferLockBaseAddress(image_buffer, 0);

            void* data = CVPixelBufferGetBaseAddress(image_buffer);
            memcpy(pkt->data, data, pkt->size);

            CVPixelBufferUnlockBaseAddress(image_buffer, 0);
            CFRelease(ctx->current_frame);
            ctx->current_frame = nil;
        } else {
            pkt->data = NULL;
            pthread_cond_wait(&ctx->frame_wait_cond, &ctx->frame_lock);
        }

        unlock_frames(ctx);
    } while (!pkt->data);

    return 0;
}

static int avf_close(AVFormatContext *s)
{
    AVFContext* ctx = (AVFContext*)s->priv_data;
    destroy_context(ctx);
    return 0;
}

static const AVOption options[] = {
    { "frame_rate", "set frame rate", offsetof(AVFContext, frame_rate), AV_OPT_TYPE_FLOAT, { .dbl = 30.0 }, 0.1, 30.0, AV_OPT_TYPE_VIDEO_RATE, NULL },
    { "list_devices", "list available devices", offsetof(AVFContext, list_devices), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, AV_OPT_FLAG_DECODING_PARAM, "list_devices" },
    { "true", "", 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, "list_devices" },
    { "false", "", 0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, "list_devices" },
    { "video_device_index", "select video device by index for devices with same name (starts at 0)", offsetof(AVFContext, video_device_index), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "pixel_format", "set pixel format", offsetof(AVFContext, pixel_format), AV_OPT_TYPE_PIXEL_FMT, {.i64 = AV_PIX_FMT_YUV420P}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM},
    { NULL },
};

static const AVClass avf_class = {
    .class_name = "AVFoundation capture input device",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_avcapture_demuxer = {
    .name           = "avcapture",
    .long_name      = NULL_IF_CONFIG_SMALL("AVCapture input device"),
    .priv_data_size = sizeof(AVFContext),
    .read_header    = avf_read_header,
    .read_packet    = avf_read_packet,
    .read_close     = avf_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &avf_class,
};
