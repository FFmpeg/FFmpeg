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

typedef struct
{
    AVClass*        class;
    
    float           frame_rate;
    int             frames_captured;
    int64_t         first_pts;
    pthread_mutex_t frame_lock;
    id              qt_delegate;
    
    QTCaptureSession*                 capture_session;
    QTCaptureDeviceInput*             capture_device_input;
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
    [super init];
    
    _context = context;
    
    return self;
}

- (void)captureOutput:(QTCaptureOutput *)captureOutput
  didOutputVideoFrame:(CVImageBufferRef)videoFrame
     withSampleBuffer:(QTSampleBuffer *)sampleBuffer
       fromConnection:(QTCaptureConnection *)connection
{
    lock_frames(_context);
    if( _context->current_frame != nil )
    {
        CVBufferRelease(_context->current_frame);
    }
    
    _context->current_frame = CVBufferRetain(videoFrame);
    unlock_frames(_context);
    
    ++_context->frames_captured;
}

@end


static int isight_read_header(AVFormatContext *s)
{
    CaptureContext* ctx = (CaptureContext*)s->priv_data;
    
    ctx->current_frame       = NULL;
    ctx->capture_device_input = NULL;
    ctx->capture_session     = NULL;
    ctx->qt_delegate         = nil;
    ctx->video_output        = NULL;
    ctx->frames_captured     = 0;
    ctx->first_pts           = av_gettime();
    
    pthread_mutex_init(&ctx->frame_lock, NULL);
    
    NSError* error;

    // Find capture device
    QTCaptureDevice *videoDevice = [QTCaptureDevice defaultInputDeviceWithMediaType:QTMediaTypeVideo];
    
    BOOL success = [videoDevice open:&error];
    
    // Video capture device not found, looking for QTMediaTypeMuxed
    if (!success) {
        videoDevice = [QTCaptureDevice defaultInputDeviceWithMediaType:QTMediaTypeMuxed];
        success = [videoDevice open:&error];
    }
    
    if( !success )
    {
        av_log( s, AV_LOG_ERROR, "can't find QT capture device\n" );
        return -1;
    }
    
    NSString* devDisplayName = [videoDevice localizedDisplayName];
    av_log( s, AV_LOG_WARNING, "'%s' opened\n", [devDisplayName UTF8String] );
    
    // Initialize capture session
    ctx->capture_session = [[QTCaptureSession alloc] init];
    
    ctx->capture_device_input = [[QTCaptureDeviceInput alloc] initWithDevice:videoDevice];
    success = [ctx->capture_session addInput:ctx->capture_device_input error:&error];
    if( !success )
    {
        av_log( s, AV_LOG_ERROR, "can't add QT capture device to session\n" );
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
    [ctx->video_output setMinimumVideoFrameInterval:1.0/ctx->frame_rate];
    
    success = [ctx->capture_session addOutput:ctx->video_output error:&error];
    
    if( !success )
    {
        av_log( s, AV_LOG_ERROR, "can't add video output to capture session\n" );
    }
    
    
    [ctx->capture_session startRunning];
    
    
    // Take stream info from the first frame.
    while( ctx->frames_captured < 1 )
    {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, YES);
        usleep(100);
    }
    
    lock_frames(ctx);
    
    size_t frameWidth  = CVPixelBufferGetWidth(ctx->current_frame);
    size_t frameHeight = CVPixelBufferGetHeight(ctx->current_frame);
    
    CVBufferRelease(ctx->current_frame);
    ctx->current_frame = nil;
    
    unlock_frames(ctx);
    
    AVStream* stream = avformat_new_stream(s, NULL);
    avpriv_set_pts_info( stream, 64, 1, kISightTimeBase );
    
    stream->codec->codec_id   = AV_CODEC_ID_RAWVIDEO;
    stream->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codec->width      = (int)frameWidth;
    stream->codec->height     = (int)frameHeight;
    stream->codec->pix_fmt    = PIX_FMT_RGB24;
    
    s->start_time = 0;
    
    return 0;
}

static int isight_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    CaptureContext* ctx = (CaptureContext*)s->priv_data;
    
    do
    {
        lock_frames(ctx);
        
        if( ctx->current_frame != nil )
        {
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
        }
        else
        {
            pkt->data = NULL;
        }
        
        unlock_frames(ctx);
        
        if( pkt->data == NULL )
            usleep(1000000/60);
    }
    while( pkt->data == NULL );
    
    return 0;
}

static int isight_close(AVFormatContext *s)
{
    CaptureContext* ctx = (CaptureContext*)s->priv_data;
    
    av_log(s, AV_LOG_DEBUG, "isight_close\n");
    [ctx->capture_session stopRunning];

    [ctx->capture_session release];
    [ctx->video_output    release];
    [ctx->qt_delegate     release];

    if( ctx->current_frame )
        CVBufferRelease(ctx->current_frame);

    pthread_mutex_destroy(&ctx->frame_lock);

    return 0;
}

static const AVOption options[] = {
    { "frame_rate", "", offsetof(CaptureContext, frame_rate), AV_OPT_TYPE_FLOAT, { .dbl = 30.0 }, 0.1, 30.0, AV_OPT_FLAG_VIDEO_PARAM, NULL },
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
