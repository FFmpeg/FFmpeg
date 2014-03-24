/*
 * QTKit input device
 * Copyright (c) 2013 Vadim Kalinsky <vadim@kalinsky.ru>
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
 * QTKit input device
 * @author Vadim Kalinsky <vadim@kalinsky.ru>
 */

#import <QTKit/QTkit.h>
#include <pthread.h>

#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "libavformat/internal.h"
#include "libavutil/internal.h"
#include "libavutil/time.h"
#include "avdevice.h"

static const int kQTKitTimeBase = 100;

static const AVRational kQTKitTimeBase_q = {
    .num = 1,
    .den = kQTKitTimeBase
};

typedef struct
{
    AVClass*        class;

    float           frame_rate;
    int             frames_captured;
    int64_t         first_pts;
    pthread_mutex_t frame_lock;
    pthread_cond_t  frame_wait_cond;
    id              qt_delegate;

    QTCaptureSession*                 capture_session;
    QTCaptureDecompressedVideoOutput* video_output;
    CVImageBufferRef                  current_frame;
} CaptureContext;

static void lock_frames(CaptureContext* ctx)
{
    pthread_mutex_lock(&ctx->frame_lock);
}

static void unlock_frames(CaptureContext* ctx)
{
    pthread_mutex_unlock(&ctx->frame_lock);
}

/** FrameReciever class - delegate for QTCaptureSession
 */
@interface FFMPEG_FrameReceiver : NSObject
{
    CaptureContext* _context;
}

- (id)initWithContext:(CaptureContext*)context;

- (void)captureOutput:(QTCaptureOutput *)captureOutput
  didOutputVideoFrame:(CVImageBufferRef)videoFrame
     withSampleBuffer:(QTSampleBuffer *)sampleBuffer
       fromConnection:(QTCaptureConnection *)connection;

@end

@implementation FFMPEG_FrameReceiver

- (id)initWithContext:(CaptureContext*)context
{
    if (self = [super init]) {
        _context = context;
    }
    return self;
}

- (void)captureOutput:(QTCaptureOutput *)captureOutput
  didOutputVideoFrame:(CVImageBufferRef)videoFrame
     withSampleBuffer:(QTSampleBuffer *)sampleBuffer
       fromConnection:(QTCaptureConnection *)connection
{
    lock_frames(_context);
    if (_context->current_frame != nil) {
        CVBufferRelease(_context->current_frame);
    }

    _context->current_frame = CVBufferRetain(videoFrame);

    pthread_cond_signal(&_context->frame_wait_cond);

    unlock_frames(_context);

    ++_context->frames_captured;
}

@end

static void destroy_context(CaptureContext* ctx)
{
    [ctx->capture_session stopRunning];

    [ctx->capture_session release];
    [ctx->video_output    release];
    [ctx->qt_delegate     release];

    ctx->capture_session = NULL;
    ctx->video_output    = NULL;
    ctx->qt_delegate     = NULL;

    pthread_mutex_destroy(&ctx->frame_lock);
    pthread_cond_destroy(&ctx->frame_wait_cond);

    if (ctx->current_frame)
        CVBufferRelease(ctx->current_frame);
}

static int qtkit_read_header(AVFormatContext *s)
{
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

    CaptureContext* ctx = (CaptureContext*)s->priv_data;

    ctx->first_pts = av_gettime();

    pthread_mutex_init(&ctx->frame_lock, NULL);
    pthread_cond_init(&ctx->frame_wait_cond, NULL);

    // Find default capture device
    QTCaptureDevice *video_device = [QTCaptureDevice defaultInputDeviceWithMediaType:QTMediaTypeMuxed];

    BOOL success = [video_device open:nil];

    // Video capture device not found, looking for QTMediaTypeVideo
    if (!success) {
        video_device = [QTCaptureDevice defaultInputDeviceWithMediaType:QTMediaTypeVideo];
        success      = [video_device open:nil];

        if (!success) {
            av_log(s, AV_LOG_ERROR, "No QT capture device found\n");
            goto fail;
        }
    }

    NSString* dev_display_name = [video_device localizedDisplayName];
    av_log (s, AV_LOG_DEBUG, "'%s' opened\n", [dev_display_name UTF8String]);

    // Initialize capture session
    ctx->capture_session = [[QTCaptureSession alloc] init];

    QTCaptureDeviceInput* capture_dev_input = [[[QTCaptureDeviceInput alloc] initWithDevice:video_device] autorelease];
    success = [ctx->capture_session addInput:capture_dev_input error:nil];

    if (!success) {
        av_log (s, AV_LOG_ERROR, "Failed to add QT capture device to session\n");
        goto fail;
    }

    // Attaching output
    // FIXME: Allow for a user defined pixel format
    ctx->video_output = [[QTCaptureDecompressedVideoOutput alloc] init];

    NSDictionary *captureDictionary = [NSDictionary dictionaryWithObject:
                                       [NSNumber numberWithUnsignedInt:kCVPixelFormatType_24RGB]
                                       forKey:(id)kCVPixelBufferPixelFormatTypeKey];

    [ctx->video_output setPixelBufferAttributes:captureDictionary];

    ctx->qt_delegate = [[FFMPEG_FrameReceiver alloc] initWithContext:ctx];

    [ctx->video_output setDelegate:ctx->qt_delegate];
    [ctx->video_output setAutomaticallyDropsLateVideoFrames:YES];
    [ctx->video_output setMinimumVideoFrameInterval:1.0/ctx->frame_rate];

    success = [ctx->capture_session addOutput:ctx->video_output error:nil];

    if (!success) {
        av_log (s, AV_LOG_ERROR, "can't add video output to capture session\n");
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

    avpriv_set_pts_info(stream, 64, 1, kQTKitTimeBase);

    stream->codec->codec_id   = AV_CODEC_ID_RAWVIDEO;
    stream->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codec->width      = (int)CVPixelBufferGetWidth (ctx->current_frame);
    stream->codec->height     = (int)CVPixelBufferGetHeight(ctx->current_frame);
    stream->codec->pix_fmt    = AV_PIX_FMT_RGB24;

    CVBufferRelease(ctx->current_frame);
    ctx->current_frame = nil;

    unlock_frames(ctx);

    [pool release];

    return 0;

fail:
    [pool release];

    destroy_context(ctx);

    return AVERROR(EIO);
}

static int qtkit_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    CaptureContext* ctx = (CaptureContext*)s->priv_data;

    do {
        lock_frames(ctx);

        if (ctx->current_frame != nil) {
            if (av_new_packet(pkt, (int)CVPixelBufferGetDataSize(ctx->current_frame)) < 0) {
                return AVERROR(EIO);
            }

            pkt->pts = pkt->dts = av_rescale_q(av_gettime() - ctx->first_pts, AV_TIME_BASE_Q, kQTKitTimeBase_q);
            pkt->stream_index = 0;
            pkt->flags |= AV_PKT_FLAG_KEY;

            CVPixelBufferLockBaseAddress(ctx->current_frame, 0);

            void* data = CVPixelBufferGetBaseAddress(ctx->current_frame);
            memcpy(pkt->data, data, pkt->size);

            CVPixelBufferUnlockBaseAddress(ctx->current_frame, 0);
            CVBufferRelease(ctx->current_frame);
            ctx->current_frame = nil;
        } else {
            pkt->data = NULL;
            pthread_cond_wait(&ctx->frame_wait_cond, &ctx->frame_lock);
        }

        unlock_frames(ctx);
    } while (!pkt->data);

    return 0;
}

static int qtkit_close(AVFormatContext *s)
{
    CaptureContext* ctx = (CaptureContext*)s->priv_data;

    destroy_context(ctx);

    return 0;
}

static const AVOption options[] = {
    { "frame_rate", "set frame rate", offsetof(CaptureContext, frame_rate), AV_OPT_TYPE_FLOAT, { .dbl = 30.0 }, 0.1, 30.0, AV_OPT_TYPE_VIDEO_RATE, NULL },
    { NULL },
};

static const AVClass qtkit_class = {
    .class_name = "QTKit input device",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_qtkit_demuxer = {
    .name           = "qtkit",
    .long_name      = NULL_IF_CONFIG_SMALL("QTKit input device"),
    .priv_data_size = sizeof(CaptureContext),
    .read_header    = qtkit_read_header,
    .read_packet    = qtkit_read_packet,
    .read_close     = qtkit_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &qtkit_class,
};
