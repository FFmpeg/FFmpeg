/*
 * iSight capture device
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

#import <QTKit/QTkit.h>
#include <pthread.h>

#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "libavformat/internal.h"
#include "libavutil/internal.h"
#include "libavutil/time.h"
#include "avdevice.h"

const int kISightTimeBase = 100;

const AVRational kISightTimeBase_q = {
    .num = 1,
    .den = kISightTimeBase
};

typedef struct {
    AVClass*        class;

    AVRational      frame_rate;
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


// FrameReciever class - delegate for QTCaptureSession
@interface FFMPEG_FrameReceiver : NSObject {
    CaptureContext* _context;
}

- (id)initWithContext:(CaptureContext*)context;

- (void)captureOutput:(QTCaptureOutput *)captureOutput
  didOutputVideoFrame:(CVImageBufferRef)videoFrame
     withSampleBuffer:(QTSampleBuffer*)sampleBuffer
       fromConnection:(QTCaptureConnection *)connection;

@end

@implementation FFMPEG_FrameReceiver

- (id)initWithContext:(CaptureContext*)context {
    if ( self = [super init] ) {
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
    if ( _context->current_frame != nil ) {
        CVBufferRelease(_context->current_frame);
    }

    _context->current_frame = CVBufferRetain(videoFrame);

    pthread_cond_signal(&_context->frame_wait_cond);

    unlock_frames(_context);

    ++_context->frames_captured;
}

@end


static void destroy_context( CaptureContext* ctx )
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

    if ( ctx->current_frame )
        CVBufferRelease(ctx->current_frame);
}

static int isight_read_header(AVFormatContext *s)
{
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

    CaptureContext* ctx = (CaptureContext*)s->priv_data;

    ctx->current_frame   = NULL;
    ctx->capture_session = NULL;
    ctx->qt_delegate     = NULL;
    ctx->video_output    = NULL;
    ctx->frames_captured = 0;
    ctx->first_pts       = av_gettime();

    pthread_mutex_init(&ctx->frame_lock, NULL);
    pthread_cond_init(&ctx->frame_wait_cond, NULL);

    // Find capture device
    QTCaptureDevice *video_device = [QTCaptureDevice defaultInputDeviceWithMediaType:QTMediaTypeMuxed];

    BOOL success = [video_device open:nil];

    // Video capture device not found, looking for QTMediaTypeVideo
    if (!success) {
        video_device = [QTCaptureDevice defaultInputDeviceWithMediaType:QTMediaTypeVideo];
        success = [video_device open:nil];
    }

    if ( !success ) {
        av_log( s, AV_LOG_ERROR, "Could not find QT capture device\n" );
        goto fail;
    }

    NSString* dev_display_name = [video_device localizedDisplayName];
    av_log( s, AV_LOG_DEBUG, "'%s' opened\n", [dev_display_name UTF8String] );

    // Initialize capture session
    ctx->capture_session = [[QTCaptureSession alloc] init];

    QTCaptureDeviceInput* capture_dev_input = [[[QTCaptureDeviceInput alloc] initWithDevice:video_device] autorelease];
    success = [ctx->capture_session addInput:capture_dev_input error:nil];
    if ( !success ) {
        av_log( s, AV_LOG_ERROR, "Could not add QT capture device to session\n" );
        goto fail;
    }

    // Attaching output
    ctx->video_output = [[QTCaptureDecompressedVideoOutput alloc] init];

    NSDictionary *captureDictionary = [NSDictionary dictionaryWithObject:
                                   [NSNumber numberWithUnsignedInt:kCVPixelFormatType_24RGB]
                                   forKey:(id)kCVPixelBufferPixelFormatTypeKey];

    [ctx->video_output setPixelBufferAttributes:captureDictionary];

    ctx->qt_delegate = [[FFMPEG_FrameReceiver alloc] initWithContext:ctx];
    [ctx->video_output setDelegate:ctx->qt_delegate];
    [ctx->video_output setAutomaticallyDropsLateVideoFrames:YES];
    [ctx->video_output setMinimumVideoFrameInterval:(float)ctx->frame_rate.den/ctx->frame_rate.num];

    success = [ctx->capture_session addOutput:ctx->video_output error:nil];

    if ( !success ) {
        av_log( s, AV_LOG_ERROR, "Could not add video output to capture session\n" );
        goto fail;
    }


    [ctx->capture_session startRunning];


    // Take stream info from the first frame.
    while ( ctx->frames_captured < 1 ) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, YES);
    }

    lock_frames(ctx);

    size_t frame_width  = CVPixelBufferGetWidth(ctx->current_frame);
    size_t frame_height = CVPixelBufferGetHeight(ctx->current_frame);

    CVBufferRelease(ctx->current_frame);
    ctx->current_frame = nil;

    unlock_frames(ctx);

    AVStream* stream = avformat_new_stream(s, NULL);
    avpriv_set_pts_info( stream, 64, 1, kISightTimeBase );

    stream->codec->codec_id   = AV_CODEC_ID_RAWVIDEO;
    stream->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codec->width      = (int)frame_width;
    stream->codec->height     = (int)frame_height;
    stream->codec->pix_fmt    = AV_PIX_FMT_RGB24;

    s->start_time = 0;

    [pool release];

    return 0;

fail:
    [pool release];

    destroy_context(ctx);

    return AVERROR(EIO);
}

static int isight_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    CaptureContext* ctx = (CaptureContext*)s->priv_data;
    
    do {
        lock_frames(ctx);

        if ( ctx->current_frame != nil ) {
            CVPixelBufferLockBaseAddress(ctx->current_frame, 0);

            av_new_packet(pkt, (int)CVPixelBufferGetDataSize(ctx->current_frame));
            pkt->pts = pkt->dts = av_rescale_q(av_gettime() - ctx->first_pts, AV_TIME_BASE_Q, kISightTimeBase_q);
            pkt->stream_index = 0;
            pkt->flags |= AV_PKT_FLAG_KEY;

            void* data = CVPixelBufferGetBaseAddress(ctx->current_frame);
            memcpy( pkt->data, data, pkt->size );

            CVPixelBufferUnlockBaseAddress(ctx->current_frame, 0);
            CVBufferRelease(ctx->current_frame);
            
            ctx->current_frame = nil;
            
            av_log( s, AV_LOG_DEBUG, "read frame pts %lld\n", pkt->pts );
        } else {
            pkt->data = NULL;
        }

        if ( pkt->data ) {
            pthread_cond_wait(&ctx->frame_wait_cond, &ctx->frame_lock);
        }

        unlock_frames(ctx);
    } while ( !pkt->data );
    
    return 0;
}

static int isight_close(AVFormatContext *s)
{
    CaptureContext* ctx = (CaptureContext*)s->priv_data;
    
    if (ctx)
        destroy_context(ctx);
    
    return 0;
}

static const AVOption options[] = {
    { "frame_rate", "set frame rate", offsetof(CaptureContext, frame_rate),
        AV_OPT_TYPE_VIDEO_RATE,
        { .str = "25" },
        0, 0,
        AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass isight_class = {
    .class_name = "iSight camera",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_isight_demuxer = {
    .name           = "isight",
    .long_name      = NULL_IF_CONFIG_SMALL("Macbook/iMac embedded camera capture"),
    .priv_data_size = sizeof(CaptureContext),
    .read_header    = isight_read_header,
    .read_packet    = isight_read_packet,
    .read_close     = isight_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &isight_class,
};
