/*
 * AVFoundation input device
 * Copyright (c) 2014 Thilo Borgmann <thilo.borgmann@mail.de>
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
 * AVFoundation input device
 * @author Thilo Borgmann <thilo.borgmann@mail.de>
 */

#include "config.h"

#import <AVFoundation/AVFoundation.h>
#if HAVE_IOKIT
#   import <IOKit/IOKitLib.h>
    /* kIOMainPortDefault is only available since macOS 12; fall back to the
     * equivalent kIOMasterPortDefault when targeting older releases. */
#   if defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && __MAC_OS_X_VERSION_MIN_REQUIRED >= 120000
#       define AVF_IO_MAIN_PORT_DEFAULT kIOMainPortDefault
#   else
#       define AVF_IO_MAIN_PORT_DEFAULT kIOMasterPortDefault
#   endif
#endif

#include <pthread.h>

#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#include "libavformat/demux.h"
#include "libavformat/internal.h"
#include "libavutil/internal.h"
#include "libavutil/parseutils.h"
#include "libavutil/time.h"
#include "libavutil/imgutils.h"
#include "avdevice.h"

static const int avf_time_base = 1000000;

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
#if !TARGET_OS_IPHONE && __MAC_OS_X_VERSION_MIN_REQUIRED >= 1080
    { AV_PIX_FMT_GRAY8,        kCVPixelFormatType_OneComponent8 },
#endif
    { AV_PIX_FMT_NONE, 0 }
};

typedef struct
{
    AVClass*        class;

    int             frames_captured;
    int             audio_frames_captured;
    pthread_mutex_t frame_lock;
    pthread_cond_t  frame_wait_cond;
    int             is_stopping;
    id              avf_delegate;
    id              avf_audio_delegate;

    AVRational      framerate;
    int             width, height;

    int             capture_cursor;
    int             capture_mouse_clicks;
    int             capture_raw_data;
    int             drop_late_frames;
    int             video_is_muxed;
    int             video_is_screen;

    int             list_devices;
    int             video_device_index;
    int             video_stream_index;
    int             audio_device_index;
    int             audio_stream_index;

    char            *url;
    char            *video_filename;
    char            *audio_filename;
    char            *video_device_id;
    char            *audio_device_id;

    int             num_video_devices;

    int             audio_channels;
    int             audio_bits_per_sample;
    int             audio_float;
    int             audio_be;
    int             audio_signed_integer;
    int             audio_packed;
    int             audio_non_interleaved;

    int32_t         *audio_buffer;
    int             audio_buffer_size;

    enum AVPixelFormat pixel_format;

    AVCaptureSession         *capture_session;
    AVCaptureVideoDataOutput *video_output;
    AVCaptureAudioDataOutput *audio_output;
    CMSampleBufferRef         current_frame;
    CMSampleBufferRef         current_audio_frame;

    AVCaptureDevice          *observed_device;
#if !TARGET_OS_IPHONE && __MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
    AVCaptureDeviceTransportControlsPlaybackMode observed_mode;
#endif
    int                      observed_quit;
} AVFContext;

static void lock_frames(AVFContext* ctx)
{
    pthread_mutex_lock(&ctx->frame_lock);
}

static void unlock_frames(AVFContext* ctx)
{
    pthread_cond_broadcast(&ctx->frame_wait_cond);
    pthread_mutex_unlock(&ctx->frame_lock);
}

/** FrameReceiver class - delegate for AVCaptureSession
 */
@interface AVFFrameReceiver : NSObject
{
    AVFContext* _context;
}

- (id)initWithContext:(AVFContext*)context;

- (void)  captureOutput:(AVCaptureOutput *)captureOutput
  didOutputSampleBuffer:(CMSampleBufferRef)videoFrame
         fromConnection:(AVCaptureConnection *)connection;

@end

@implementation AVFFrameReceiver

- (id)initWithContext:(AVFContext*)context
{
    if (self = [super init]) {
        _context = context;

        // start observing if a device is set for it
#if !TARGET_OS_IPHONE && __MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
        if (_context->observed_device) {
            NSString *keyPath = NSStringFromSelector(@selector(transportControlsPlaybackMode));
            NSKeyValueObservingOptions options = NSKeyValueObservingOptionNew;

            [_context->observed_device addObserver: self
                                        forKeyPath: keyPath
                                           options: options
                                           context: _context];
        }
#endif
    }
    return self;
}

- (void)dealloc {
    // stop observing if a device is set for it
#if !TARGET_OS_IPHONE && __MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
    if (_context->observed_device) {
        NSString *keyPath = NSStringFromSelector(@selector(transportControlsPlaybackMode));
        [_context->observed_device removeObserver: self forKeyPath: keyPath];
    }
#endif
    [super dealloc];
}

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary *)change
                       context:(void *)context {
    if (context == _context) {
#if !TARGET_OS_IPHONE && __MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
        AVCaptureDeviceTransportControlsPlaybackMode mode =
            [change[NSKeyValueChangeNewKey] integerValue];

        if (mode != _context->observed_mode) {
            if (mode == AVCaptureDeviceTransportControlsNotPlayingMode) {
                // Set under the lock and broadcast so a reader blocked in
                // avf_read_packet() wakes up and returns EOF instead of
                // hanging once the device stops delivering frames.
                lock_frames(_context);
                _context->observed_quit = 1;
                unlock_frames(_context);
            }
            _context->observed_mode = mode;
        }
#endif
    } else {
        [super observeValueForKeyPath: keyPath
                             ofObject: object
                               change: change
                              context: context];
    }
}

- (void)  captureOutput:(AVCaptureOutput *)captureOutput
  didOutputSampleBuffer:(CMSampleBufferRef)videoFrame
         fromConnection:(AVCaptureConnection *)connection
{
    lock_frames(_context);

    while ((_context->current_frame != nil) && !_context->is_stopping) {
        pthread_cond_wait(&_context->frame_wait_cond, &_context->frame_lock);
    }

    if (_context->is_stopping) {
        unlock_frames(_context);
        return;
    }

    _context->current_frame = (CMSampleBufferRef)CFRetain(videoFrame);

    unlock_frames(_context);

    ++_context->frames_captured;
}

@end

/** AudioReceiver class - delegate for AVCaptureSession
 */
@interface AVFAudioReceiver : NSObject
{
    AVFContext* _context;
}

- (id)initWithContext:(AVFContext*)context;

- (void)  captureOutput:(AVCaptureOutput *)captureOutput
  didOutputSampleBuffer:(CMSampleBufferRef)audioFrame
         fromConnection:(AVCaptureConnection *)connection;

@end

@implementation AVFAudioReceiver

- (id)initWithContext:(AVFContext*)context
{
    if (self = [super init]) {
        _context = context;
    }
    return self;
}

- (void)  captureOutput:(AVCaptureOutput *)captureOutput
  didOutputSampleBuffer:(CMSampleBufferRef)audioFrame
         fromConnection:(AVCaptureConnection *)connection
{
    lock_frames(_context);

    while ((_context->current_audio_frame != nil) && !_context->is_stopping) {
        pthread_cond_wait(&_context->frame_wait_cond, &_context->frame_lock);
    }

    if (_context->is_stopping) {
        unlock_frames(_context);
        return;
    }

    _context->current_audio_frame = (CMSampleBufferRef)CFRetain(audioFrame);

    unlock_frames(_context);

    ++_context->audio_frames_captured;
}

@end

static void destroy_context(AVFContext* ctx)
{
    // Wake any capture callback blocked waiting for the consumer and make it
    // bail out, so stopRunning() can drain the session without a deadlock.
    lock_frames(ctx);
    ctx->is_stopping = 1;
    unlock_frames(ctx);

    [ctx->capture_session stopRunning];

    [ctx->capture_session release];
    [ctx->video_output    release];
    [ctx->audio_output    release];
    [ctx->avf_delegate    release];
    [ctx->avf_audio_delegate release];

    ctx->capture_session = NULL;
    ctx->video_output    = NULL;
    ctx->audio_output    = NULL;
    ctx->avf_delegate    = NULL;
    ctx->avf_audio_delegate = NULL;

    av_freep(&ctx->url);
    av_freep(&ctx->audio_buffer);

    pthread_cond_destroy(&ctx->frame_wait_cond);
    pthread_mutex_destroy(&ctx->frame_lock);

    if (ctx->current_frame) {
        CFRelease(ctx->current_frame);
        ctx->current_frame = nil;
    }

    if (ctx->current_audio_frame) {
        CFRelease(ctx->current_audio_frame);
        ctx->current_audio_frame = nil;
    }
}

static int parse_device_name(AVFormatContext *s)
{
    AVFContext *ctx = (AVFContext*)s->priv_data;
    char *save;

    ctx->url = av_strdup(s->url);

    if (!ctx->url)
        return AVERROR(ENOMEM);
    if (ctx->url[0] != ':') {
        ctx->video_filename = av_strtok(ctx->url,  ":", &save);
        ctx->audio_filename = av_strtok(NULL, ":", &save);
    } else {
        ctx->audio_filename = av_strtok(ctx->url,  ":", &save);
    }
    return 0;
}

/**
 * Configure the video device.
 *
 * Configure the video device using a run-time approach to access properties
 * since formats, activeFormat are available since  iOS >= 7.0 or OSX >= 10.7
 * and activeVideoMaxFrameDuration is available since i0S >= 7.0 and OSX >= 10.9.
 *
 * The NSUndefinedKeyException must be handled by the caller of this function.
 *
 */
static int configure_video_device(AVFormatContext *s, AVCaptureDevice *video_device)
{
    AVFContext *ctx = (AVFContext*)s->priv_data;

    double framerate = av_q2d(ctx->framerate);
    NSObject *range = nil;
    NSObject *format = nil;
    NSObject *selected_range = nil;
    NSObject *selected_format = nil;

    // try to configure format by formats list
    // might raise an exception if no format list is given
    // (then fallback to default, no configuration)
    @try {
        for (format in [video_device valueForKey:@"formats"]) {
            CMFormatDescriptionRef formatDescription;
            CMVideoDimensions dimensions;

            formatDescription = (CMFormatDescriptionRef) [format performSelector:@selector(formatDescription)];
            dimensions = CMVideoFormatDescriptionGetDimensions(formatDescription);

            if ((ctx->width == 0 && ctx->height == 0) ||
                (dimensions.width == ctx->width && dimensions.height == ctx->height)) {

                selected_format = format;

                for (range in [format valueForKey:@"videoSupportedFrameRateRanges"]) {
                    double max_framerate;

                    [[range valueForKey:@"maxFrameRate"] getValue:&max_framerate];
                    if (fabs (framerate - max_framerate) < 0.01) {
                        selected_range = range;
                        break;
                    }
                }
            }
        }

        if (!selected_format) {
            av_log(s, AV_LOG_ERROR, "Selected video size (%dx%d) is not supported by the device.\n",
                ctx->width, ctx->height);
            goto unsupported_format;
        }

        if (!selected_range) {
            av_log(s, AV_LOG_ERROR, "Selected framerate (%f) is not supported by the device.\n",
                framerate);
            if (ctx->video_is_muxed) {
                av_log(s, AV_LOG_ERROR, "Falling back to default.\n");
            } else {
                goto unsupported_format;
            }
        }

        if ([video_device lockForConfiguration:NULL] == YES) {
            if (selected_format) {
                [video_device setValue:selected_format forKey:@"activeFormat"];
            }
            if (selected_range) {
                NSValue *min_frame_duration = [selected_range valueForKey:@"minFrameDuration"];
                [video_device setValue:min_frame_duration forKey:@"activeVideoMinFrameDuration"];
                [video_device setValue:min_frame_duration forKey:@"activeVideoMaxFrameDuration"];
            }
        } else {
            av_log(s, AV_LOG_ERROR, "Could not lock device for configuration.\n");
            return AVERROR(EINVAL);
        }
    } @catch(NSException *e) {
        av_log(ctx, AV_LOG_WARNING, "Configuration of video device failed, falling back to default.\n");
    }

    return 0;

unsupported_format:

    av_log(s, AV_LOG_ERROR, "Supported modes:\n");
    for (format in [video_device valueForKey:@"formats"]) {
        CMFormatDescriptionRef formatDescription;
        CMVideoDimensions dimensions;

        formatDescription = (CMFormatDescriptionRef) [format performSelector:@selector(formatDescription)];
        dimensions = CMVideoFormatDescriptionGetDimensions(formatDescription);

        for (range in [format valueForKey:@"videoSupportedFrameRateRanges"]) {
            double min_framerate;
            double max_framerate;

            [[range valueForKey:@"minFrameRate"] getValue:&min_framerate];
            [[range valueForKey:@"maxFrameRate"] getValue:&max_framerate];
            av_log(s, AV_LOG_ERROR, "  %dx%d@[%f %f]fps\n",
                dimensions.width, dimensions.height,
                min_framerate, max_framerate);
        }
    }
    return AVERROR(EINVAL);
}

static int add_video_device(AVFormatContext *s, AVCaptureDevice *video_device)
{
    AVFContext *ctx = (AVFContext*)s->priv_data;
    int ret;
    NSError *error  = nil;
    AVCaptureInput* capture_input = nil;
    struct AVFPixelFormatSpec pxl_fmt_spec;
    NSNumber *pixel_format;
    NSDictionary *capture_dict;
    dispatch_queue_t queue;

    if (!ctx->video_is_screen) {
        capture_input = (AVCaptureInput*) [[[AVCaptureDeviceInput alloc] initWithDevice:video_device error:&error] autorelease];
    } else {
        capture_input = (AVCaptureInput*) video_device;
    }

    if (!capture_input) {
        av_log(s, AV_LOG_ERROR, "Failed to create AV capture input device: %s\n",
               [[error localizedDescription] UTF8String]);
        return 1;
    }

    if ([ctx->capture_session canAddInput:capture_input]) {
        [ctx->capture_session addInput:capture_input];
    } else {
        av_log(s, AV_LOG_ERROR, "can't add video input to capture session\n");
        return 1;
    }

    // Attaching output
    ctx->video_output = [[AVCaptureVideoDataOutput alloc] init];

    if (!ctx->video_output) {
        av_log(s, AV_LOG_ERROR, "Failed to init AV video output\n");
        return 1;
    }

    // Configure device framerate and video size
    @try {
        if ((ret = configure_video_device(s, video_device)) < 0) {
            return ret;
        }
    } @catch (NSException *exception) {
        if (![[exception name] isEqualToString:NSUndefinedKeyException]) {
          av_log (s, AV_LOG_ERROR, "An error occurred: %s", [exception.reason UTF8String]);
          return AVERROR_EXTERNAL;
        }
    }

    // select pixel format
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
        return 1;
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
            return 1;
        } else {
            av_log(s, AV_LOG_WARNING, "Overriding selected pixel format to use %s instead.\n",
                   av_get_pix_fmt_name(pxl_fmt_spec.ff_id));
        }
    }

    // set videoSettings to an empty dict for receiving raw data of muxed devices
    if (ctx->capture_raw_data) {
        ctx->pixel_format = pxl_fmt_spec.ff_id;
        ctx->video_output.videoSettings = @{ };
    } else {
        ctx->pixel_format = pxl_fmt_spec.ff_id;
        pixel_format = [NSNumber numberWithUnsignedInt:pxl_fmt_spec.avf_id];
        capture_dict = [NSDictionary dictionaryWithObject:pixel_format
                                                   forKey:(id)kCVPixelBufferPixelFormatTypeKey];

        [ctx->video_output setVideoSettings:capture_dict];
    }
    [ctx->video_output setAlwaysDiscardsLateVideoFrames:ctx->drop_late_frames];

#if !TARGET_OS_IPHONE && __MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
    // check for transport control support and set observer device if supported
    if (!ctx->video_is_screen) {
        int trans_ctrl = [video_device transportControlsSupported];
        AVCaptureDeviceTransportControlsPlaybackMode trans_mode = [video_device transportControlsPlaybackMode];

        if (trans_ctrl) {
            ctx->observed_mode   = trans_mode;
            ctx->observed_device = video_device;
        }
    }
#endif

    ctx->avf_delegate = [[AVFFrameReceiver alloc] initWithContext:ctx];

    queue = dispatch_queue_create("avf_queue", NULL);
    [ctx->video_output setSampleBufferDelegate:ctx->avf_delegate queue:queue];
    dispatch_release(queue);

    if ([ctx->capture_session canAddOutput:ctx->video_output]) {
        [ctx->capture_session addOutput:ctx->video_output];
    } else {
        av_log(s, AV_LOG_ERROR, "can't add video output to capture session\n");
        return 1;
    }

    return 0;
}

static int add_audio_device(AVFormatContext *s, AVCaptureDevice *audio_device)
{
    AVFContext *ctx = (AVFContext*)s->priv_data;
    NSError *error  = nil;
    AVCaptureDeviceInput* audio_dev_input = [[[AVCaptureDeviceInput alloc] initWithDevice:audio_device error:&error] autorelease];
    dispatch_queue_t queue;

    if (!audio_dev_input) {
        av_log(s, AV_LOG_ERROR, "Failed to create AV capture input device: %s\n",
               [[error localizedDescription] UTF8String]);
        return 1;
    }

    if ([ctx->capture_session canAddInput:audio_dev_input]) {
        [ctx->capture_session addInput:audio_dev_input];
    } else {
        av_log(s, AV_LOG_ERROR, "can't add audio input to capture session\n");
        return 1;
    }

    // Attaching output
    ctx->audio_output = [[AVCaptureAudioDataOutput alloc] init];

    if (!ctx->audio_output) {
        av_log(s, AV_LOG_ERROR, "Failed to init AV audio output\n");
        return 1;
    }

    ctx->avf_audio_delegate = [[AVFAudioReceiver alloc] initWithContext:ctx];

    queue = dispatch_queue_create("avf_audio_queue", NULL);
    [ctx->audio_output setSampleBufferDelegate:ctx->avf_audio_delegate queue:queue];
    dispatch_release(queue);

    if ([ctx->capture_session canAddOutput:ctx->audio_output]) {
        [ctx->capture_session addOutput:ctx->audio_output];
    } else {
        av_log(s, AV_LOG_ERROR, "adding audio output to capture session failed\n");
        return 1;
    }

    return 0;
}

static int get_video_config(AVFormatContext *s)
{
    AVFContext *ctx = (AVFContext*)s->priv_data;
    CVImageBufferRef image_buffer;
    CGSize image_buffer_size;
    AVStream* stream = avformat_new_stream(s, NULL);

    if (!stream) {
        return 1;
    }

    // Take stream info from the first frame.
    while (ctx->frames_captured < 1) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, YES);
    }

    lock_frames(ctx);

    ctx->video_stream_index = stream->index;

    avpriv_set_pts_info(stream, 64, 1, avf_time_base);

    image_buffer = CMSampleBufferGetImageBuffer(ctx->current_frame);

    if (image_buffer) {
        image_buffer_size = CVImageBufferGetEncodedSize(image_buffer);

        stream->codecpar->codec_id   = AV_CODEC_ID_RAWVIDEO;
        stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        stream->codecpar->width      = (int)image_buffer_size.width;
        stream->codecpar->height     = (int)image_buffer_size.height;
        stream->codecpar->format     = ctx->pixel_format;
    } else {
        stream->codecpar->codec_id   = AV_CODEC_ID_DVVIDEO;
        stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        stream->codecpar->format     = ctx->pixel_format;
    }

    CFRelease(ctx->current_frame);
    ctx->current_frame = nil;

    unlock_frames(ctx);

    return 0;
}

static int get_audio_config(AVFormatContext *s)
{
    AVFContext *ctx = (AVFContext*)s->priv_data;
    CMFormatDescriptionRef format_desc;
    AVStream* stream = avformat_new_stream(s, NULL);

    if (!stream) {
        return 1;
    }

    // Take stream info from the first frame.
    while (ctx->audio_frames_captured < 1) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, YES);
    }

    lock_frames(ctx);

    ctx->audio_stream_index = stream->index;

    avpriv_set_pts_info(stream, 64, 1, avf_time_base);

    format_desc = CMSampleBufferGetFormatDescription(ctx->current_audio_frame);
    const AudioStreamBasicDescription *basic_desc = CMAudioFormatDescriptionGetStreamBasicDescription(format_desc);

    if (!basic_desc) {
        unlock_frames(ctx);
        av_log(s, AV_LOG_ERROR, "audio format not available\n");
        return 1;
    }

    stream->codecpar->codec_type     = AVMEDIA_TYPE_AUDIO;
    stream->codecpar->sample_rate    = basic_desc->mSampleRate;
    av_channel_layout_default(&stream->codecpar->ch_layout, basic_desc->mChannelsPerFrame);

    ctx->audio_channels        = basic_desc->mChannelsPerFrame;
    ctx->audio_bits_per_sample = basic_desc->mBitsPerChannel;
    ctx->audio_float           = basic_desc->mFormatFlags & kAudioFormatFlagIsFloat;
    ctx->audio_be              = basic_desc->mFormatFlags & kAudioFormatFlagIsBigEndian;
    ctx->audio_signed_integer  = basic_desc->mFormatFlags & kAudioFormatFlagIsSignedInteger;
    ctx->audio_packed          = basic_desc->mFormatFlags & kAudioFormatFlagIsPacked;
    ctx->audio_non_interleaved = basic_desc->mFormatFlags & kAudioFormatFlagIsNonInterleaved;

    if (basic_desc->mFormatID == kAudioFormatLinearPCM &&
        ctx->audio_float &&
        ctx->audio_bits_per_sample == 32 &&
        ctx->audio_packed) {
        stream->codecpar->codec_id = ctx->audio_be ? AV_CODEC_ID_PCM_F32BE : AV_CODEC_ID_PCM_F32LE;
    } else if (basic_desc->mFormatID == kAudioFormatLinearPCM &&
        ctx->audio_signed_integer &&
        ctx->audio_bits_per_sample == 16 &&
        ctx->audio_packed) {
        stream->codecpar->codec_id = ctx->audio_be ? AV_CODEC_ID_PCM_S16BE : AV_CODEC_ID_PCM_S16LE;
    } else if (basic_desc->mFormatID == kAudioFormatLinearPCM &&
        ctx->audio_signed_integer &&
        ctx->audio_bits_per_sample == 24 &&
        ctx->audio_packed) {
        stream->codecpar->codec_id = ctx->audio_be ? AV_CODEC_ID_PCM_S24BE : AV_CODEC_ID_PCM_S24LE;
    } else if (basic_desc->mFormatID == kAudioFormatLinearPCM &&
        ctx->audio_signed_integer &&
        ctx->audio_bits_per_sample == 32 &&
        ctx->audio_packed) {
        stream->codecpar->codec_id = ctx->audio_be ? AV_CODEC_ID_PCM_S32BE : AV_CODEC_ID_PCM_S32LE;
    } else {
        unlock_frames(ctx);
        av_log(s, AV_LOG_ERROR, "audio format is not supported\n");
        return 1;
    }

    if (ctx->audio_non_interleaved) {
        CMBlockBufferRef block_buffer = CMSampleBufferGetDataBuffer(ctx->current_audio_frame);
        ctx->audio_buffer_size        = CMBlockBufferGetDataLength(block_buffer);
        ctx->audio_buffer             = av_malloc(ctx->audio_buffer_size);
        if (!ctx->audio_buffer) {
            unlock_frames(ctx);
            av_log(s, AV_LOG_ERROR, "error allocating audio buffer\n");
            return 1;
        }
    }

    CFRelease(ctx->current_audio_frame);
    ctx->current_audio_frame = nil;

    unlock_frames(ctx);

    return 0;
}

static NSArray* getDevicesWithMediaType(AVMediaType mediaType) {
#if ((TARGET_OS_IPHONE && __IPHONE_OS_VERSION_MIN_REQUIRED >= 100000) || (TARGET_OS_OSX && __MAC_OS_X_VERSION_MIN_REQUIRED >= 101500))
    NSMutableArray *deviceTypes = nil;
    if (mediaType == AVMediaTypeVideo) {
        deviceTypes = [NSMutableArray arrayWithArray:@[AVCaptureDeviceTypeBuiltInWideAngleCamera]];
        #if (TARGET_OS_IPHONE && __IPHONE_OS_VERSION_MIN_REQUIRED >= 100000)
            [deviceTypes addObject: AVCaptureDeviceTypeBuiltInDualCamera];
            [deviceTypes addObject: AVCaptureDeviceTypeBuiltInTelephotoCamera];
        #endif
        #if (TARGET_OS_IPHONE && __IPHONE_OS_VERSION_MIN_REQUIRED >= 110100)
            [deviceTypes addObject: AVCaptureDeviceTypeBuiltInTrueDepthCamera];
        #endif
        #if (TARGET_OS_IPHONE && __IPHONE_OS_VERSION_MIN_REQUIRED >= 130000)
            [deviceTypes addObject: AVCaptureDeviceTypeBuiltInTripleCamera];
            [deviceTypes addObject: AVCaptureDeviceTypeBuiltInDualWideCamera];
            [deviceTypes addObject: AVCaptureDeviceTypeBuiltInUltraWideCamera];
        #endif
        #if (TARGET_OS_OSX && __MAC_OS_X_VERSION_MIN_REQUIRED >= 130000)
            [deviceTypes addObject: AVCaptureDeviceTypeDeskViewCamera];
        #endif
        #if (TARGET_OS_IPHONE && __IPHONE_OS_VERSION_MIN_REQUIRED >= 150400)
            [deviceTypes addObject: AVCaptureDeviceTypeBuiltInLiDARDepthCamera];
        #endif
        #if (TARGET_OS_IPHONE && __IPHONE_OS_VERSION_MIN_REQUIRED >= 170000 || (TARGET_OS_OSX && __MAC_OS_X_VERSION_MIN_REQUIRED >= 140000))
            [deviceTypes addObject: AVCaptureDeviceTypeContinuityCamera];
            [deviceTypes addObject: AVCaptureDeviceTypeExternal];
        #elif (TARGET_OS_OSX && __MAC_OS_X_VERSION_MIN_REQUIRED < 140000)
            [deviceTypes addObject: AVCaptureDeviceTypeExternalUnknown];
        #endif
    } else if (mediaType == AVMediaTypeAudio) {
        #if (TARGET_OS_IPHONE && __IPHONE_OS_VERSION_MIN_REQUIRED >= 170000 || (TARGET_OS_OSX && __MAC_OS_X_VERSION_MIN_REQUIRED >= 140000))
            deviceTypes = [NSMutableArray arrayWithArray:@[AVCaptureDeviceTypeMicrophone]];
        #else
            deviceTypes = [NSMutableArray arrayWithArray:@[AVCaptureDeviceTypeBuiltInMicrophone]];
        #endif
    } else if (mediaType == AVMediaTypeMuxed) {
        #if (TARGET_OS_IPHONE && __IPHONE_OS_VERSION_MIN_REQUIRED >= 170000 || (TARGET_OS_OSX && __MAC_OS_X_VERSION_MIN_REQUIRED >= 140000))
            deviceTypes = [NSMutableArray arrayWithArray:@[AVCaptureDeviceTypeExternal]];
        #elif (TARGET_OS_OSX && __MAC_OS_X_VERSION_MIN_REQUIRED < 140000)
            deviceTypes = [NSMutableArray arrayWithArray:@[AVCaptureDeviceTypeExternalUnknown]];
        #else
            return nil;
        #endif
    } else {
        return nil;
    }

    AVCaptureDeviceDiscoverySession *captureDeviceDiscoverySession =
        [AVCaptureDeviceDiscoverySession
        discoverySessionWithDeviceTypes:deviceTypes
                              mediaType:mediaType
                               position:AVCaptureDevicePositionUnspecified];
    return [captureDeviceDiscoverySession devices];
#elif TARGET_OS_OSX
    return [AVCaptureDevice devicesWithMediaType:mediaType];
#else
    return nil;
#endif
}

#if HAVE_IOKIT
static int avf_io_get_string(io_service_t service, CFStringRef key, char *buf, size_t size)
{
    CFTypeRef ref = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    int ok = ref && CFGetTypeID(ref) == CFStringGetTypeID() && CFStringGetCString(ref, buf, size, kCFStringEncodingUTF8);
    if (ref)
        CFRelease(ref);
    return ok;
}

static int avf_io_get_uint32(io_service_t service, CFStringRef key, uint32_t *out)
{
    CFTypeRef ref = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    int ok = ref && CFGetTypeID(ref) == CFNumberGetTypeID() && CFNumberGetValue(ref, kCFNumberSInt32Type, out);
    if (ref)
        CFRelease(ref);
    return ok;
}
#endif

#if HAVE_IOKIT
static int64_t avf_usb_location_for_serial(const char *serial)
{
    int64_t location = -1;
    io_iterator_t iterator = 0;
    io_service_t service;

    if (IOServiceGetMatchingServices(AVF_IO_MAIN_PORT_DEFAULT,
            IOServiceMatching("IOUSBHostDevice"), &iterator) != KERN_SUCCESS)
        return -1;

    while (location < 0 && (service = IOIteratorNext(iterator))) {
        char found[512];
        uint32_t loc;
        if (avf_io_get_string(service, CFSTR("USB Serial Number"), found, sizeof(found)) &&
            !strcmp(found, serial) &&
            avf_io_get_uint32(service, CFSTR("locationID"), &loc))
            location = loc;
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);

    return location;
}
#else
static int64_t avf_usb_location_for_serial(const char *serial)
{
    return -1;
}
#endif

#if HAVE_IOKIT
static NSString *avf_usb_serial_for_location(uint32_t location)
{
    NSString *serial = nil;
    io_iterator_t iterator = 0;
    io_service_t service;

    if (IOServiceGetMatchingServices(AVF_IO_MAIN_PORT_DEFAULT,
            IOServiceMatching("IOUSBHostDevice"), &iterator) != KERN_SUCCESS)
        return nil;

    while (!serial && (service = IOIteratorNext(iterator))) {
        char found[512];
        uint32_t loc;
        if (avf_io_get_uint32(service, CFSTR("locationID"), &loc) && loc == location &&
            avf_io_get_string(service, CFSTR("USB Serial Number"), found, sizeof(found)))
            serial = [NSString stringWithUTF8String:found];
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);

    return serial;
}
#else
static NSString *avf_usb_serial_for_location(uint32_t location)
{
    return nil;
}
#endif

// USB video uniqueID = locationID<<32 | VID<<16 | PID; match on the locationID.
static AVCaptureDevice *avf_video_device_with_serial(const char *serial,
    NSArray *devices, NSArray *devices_muxed, int *is_muxed)
{
    int64_t location = avf_usb_location_for_serial(serial);
    NSArray *lists[2] = { devices, devices_muxed };

    if (location < 0)
        return nil;

    for (int i = 0; i < 2; i++) {
        for (AVCaptureDevice *device in lists[i]) {
            NSString *uid = [device uniqueID];
            if ([uid hasPrefix:@"0x"] &&
                (uint32_t)(strtoull([uid UTF8String], NULL, 16) >> 32) == (uint32_t)location) {
                *is_muxed = (i == 1);
                return device;
            }
        }
    }

    return nil;
}

// CoreAudio USB-audio UID: AppleUSBAudioEngine:manufacturer:device:serial:interfaces
#if HAVE_IOKIT
static NSString *avf_audio_serial_for_uid(NSString *uid)
{
    if (![uid hasPrefix:@"AppleUSBAudioEngine:"])
        return nil;
    NSArray<NSString *> *fields = [uid componentsSeparatedByString:@":"];
    if (fields.count < 5)
        return nil;
    NSString *serial = fields[fields.count - 2];
    if (serial.length && avf_usb_location_for_serial([serial UTF8String]) >= 0)
        return serial;
    return nil;
}
#else
static NSString *avf_audio_serial_for_uid(NSString *uid)
{
    return nil;
}
#endif

static AVCaptureDevice *avf_audio_device_with_serial(const char *serial, NSArray *devices)
{
    NSString *want = [NSString stringWithUTF8String:serial];

    for (AVCaptureDevice *device in devices)
        if ([avf_audio_serial_for_uid([device uniqueID]) isEqualToString:want])
            return device;

    return nil;
}

static AVCaptureDevice *avf_device_with_uid(const char *uid,
    NSArray *devices, NSArray *devices_muxed, int *is_muxed)
{
    NSString *want = [NSString stringWithUTF8String:uid];
    NSArray *lists[2] = { devices, devices_muxed };

    for (int i = 0; i < 2; i++) {
        for (AVCaptureDevice *device in lists[i]) {
            if ([[device uniqueID] isEqualToString:want]) {
                if (is_muxed)
                    *is_muxed = (i == 1);
                return device;
            }
        }
    }

    return nil;
}

// Best-effort serial string for -list_devices, or nil.
static NSString *avf_device_listing_serial(AVCaptureDevice *device)
{
    NSString *uid = [device uniqueID];

    if ([uid hasPrefix:@"0x"]) {
        unsigned long long value = strtoull([uid UTF8String], NULL, 16);
        NSString *serial = avf_usb_serial_for_location((uint32_t)(value >> 32));
        return serial.length ? serial : nil;
    }
    return avf_audio_serial_for_uid(uid);
}

static void avf_log_device_entry(AVFContext *ctx, int index, AVCaptureDevice *device)
{
    NSString *serial = avf_device_listing_serial(device);

    if (serial)
        av_log(ctx, AV_LOG_INFO, "[%d] %s  [uid:%s] [serial:%s]\n", index,
               [[device localizedName] UTF8String], [[device uniqueID] UTF8String],
               [serial UTF8String]);
    else
        av_log(ctx, AV_LOG_INFO, "[%d] %s  [uid:%s]\n", index,
               [[device localizedName] UTF8String], [[device uniqueID] UTF8String]);
}

// Returns 1 if a device id was set (*device = match, or nil after logging on miss), else 0.
static int avf_device_from_id(AVFContext *ctx, AVMediaType media_type,
    NSArray *devices, NSArray *devices_muxed,
    const char *id, AVCaptureDevice **device, int *is_muxed)
{
    BOOL is_audio     = [media_type isEqualToString:AVMediaTypeAudio];
    const char *kind  = is_audio ? "Audio" : "Video";
    const char *value = NULL;

    if (!id)
        return 0;

    if (av_strstart(id, "serial:", &value)) {
        if (is_audio)
            *device = avf_audio_device_with_serial(value, devices);
        else
            *device = avf_video_device_with_serial(value, devices, devices_muxed, is_muxed);
        if (!*device)
            av_log(ctx, AV_LOG_ERROR,
                   "%s capture device with serial number '%s' not found\n", kind, value);
    } else if (av_strstart(id, "uid:", &value)) {
        *device = avf_device_with_uid(value, devices, devices_muxed, is_muxed);
        if (!*device)
            av_log(ctx, AV_LOG_ERROR,
                   "%s capture device with unique ID '%s' not found\n", kind, value);
    } else {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid %s device id '%s': expected 'uid:<unique ID>' or 'serial:<serial number>'\n",
               is_audio ? "audio" : "video", id);
    }
    return 1;
}

static int avf_read_header(AVFormatContext *s)
{
    int ret = 0;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    uint32_t num_screens    = 0;
    AVFContext *ctx         = (AVFContext*)s->priv_data;
    AVCaptureDevice *video_device = nil;
    AVCaptureDevice *audio_device = nil;
    // Find capture device
    NSArray *devices       = getDevicesWithMediaType(AVMediaTypeVideo);
    NSArray *devices_muxed = getDevicesWithMediaType(AVMediaTypeMuxed);
    NSArray *audio_devices = getDevicesWithMediaType(AVMediaTypeAudio);

    ctx->num_video_devices = [devices count] + [devices_muxed count];

    pthread_mutex_init(&ctx->frame_lock, NULL);
    pthread_cond_init(&ctx->frame_wait_cond, NULL);
    ctx->is_stopping = 0;

#if !TARGET_OS_IPHONE && __MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
    CGGetActiveDisplayList(0, NULL, &num_screens);
#endif

    // List devices if requested
    if (ctx->list_devices) {
        int index = 0;
        av_log(ctx, AV_LOG_INFO, "AVFoundation video devices:\n");
        for (AVCaptureDevice *device in devices) {
            index = [devices indexOfObject:device];
            avf_log_device_entry(ctx, index, device);
        }
        for (AVCaptureDevice *device in devices_muxed) {
            index = [devices count] + [devices_muxed indexOfObject:device];
            avf_log_device_entry(ctx, index, device);
        }
#if !TARGET_OS_IPHONE && __MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
        if (num_screens > 0) {
            CGDirectDisplayID screens[num_screens];
            CGGetActiveDisplayList(num_screens, screens, &num_screens);
            for (int i = 0; i < num_screens; i++) {
                av_log(ctx, AV_LOG_INFO, "[%d] Capture screen %d\n", ctx->num_video_devices + i, i);
            }
        }
#endif

        av_log(ctx, AV_LOG_INFO, "AVFoundation audio devices:\n");
        devices = getDevicesWithMediaType(AVMediaTypeAudio);
        for (AVCaptureDevice *device in devices) {
            int index = [devices indexOfObject:device];
            avf_log_device_entry(ctx, index, device);
        }
         goto fail;
    }

    // parse input filename for video and audio device
    ret = parse_device_name(s);
    if (ret)
        goto fail;

    // check for device index given in filename
    if (ctx->video_device_index == -1 && ctx->video_filename) {
        sscanf(ctx->video_filename, "%d", &ctx->video_device_index);
    }
    if (ctx->audio_device_index == -1 && ctx->audio_filename) {
        sscanf(ctx->audio_filename, "%d", &ctx->audio_device_index);
    }

    if (avf_device_from_id(ctx, AVMediaTypeVideo, devices, devices_muxed,
                           ctx->video_device_id, &video_device, &ctx->video_is_muxed)) {
        if (!video_device)
            goto fail;
    } else if (ctx->video_device_index >= 0) {
        if (ctx->video_device_index < ctx->num_video_devices) {
            if (ctx->video_device_index < [devices count]) {
                video_device = [devices objectAtIndex:ctx->video_device_index];
            } else {
                video_device = [devices_muxed objectAtIndex:(ctx->video_device_index - [devices count])];
                ctx->video_is_muxed = 1;
            }
        } else if (ctx->video_device_index < ctx->num_video_devices + num_screens) {
#if !TARGET_OS_IPHONE && __MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
            CGDirectDisplayID screens[num_screens];
            CGGetActiveDisplayList(num_screens, screens, &num_screens);
            AVCaptureScreenInput* capture_screen_input = [[[AVCaptureScreenInput alloc] initWithDisplayID:screens[ctx->video_device_index - ctx->num_video_devices]] autorelease];

            if (ctx->framerate.num > 0) {
                capture_screen_input.minFrameDuration = CMTimeMake(ctx->framerate.den, ctx->framerate.num);
            }

#if !TARGET_OS_IPHONE && __MAC_OS_X_VERSION_MIN_REQUIRED >= 1080
            if (ctx->capture_cursor) {
                capture_screen_input.capturesCursor = YES;
            } else {
                capture_screen_input.capturesCursor = NO;
            }
#endif

            if (ctx->capture_mouse_clicks) {
                capture_screen_input.capturesMouseClicks = YES;
            } else {
                capture_screen_input.capturesMouseClicks = NO;
            }

            video_device = (AVCaptureDevice*) capture_screen_input;
            ctx->video_is_screen = 1;
#endif
         } else {
            av_log(ctx, AV_LOG_ERROR, "Invalid device index\n");
            goto fail;
        }
    } else if (ctx->video_filename &&
               strncmp(ctx->video_filename, "none", 4)) {
        if (!strncmp(ctx->video_filename, "default", 7)) {
            video_device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
        } else {
        // looking for video inputs
        for (AVCaptureDevice *device in devices) {
            if (!strncmp(ctx->video_filename, [[device localizedName] UTF8String], strlen(ctx->video_filename))) {
                video_device = device;
                break;
            }
        }
        // looking for muxed inputs
        for (AVCaptureDevice *device in devices_muxed) {
            if (!strncmp(ctx->video_filename, [[device localizedName] UTF8String], strlen(ctx->video_filename))) {
                video_device = device;
                ctx->video_is_muxed = 1;
                break;
            }
        }

#if !TARGET_OS_IPHONE && __MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
        // looking for screen inputs
        if (!video_device) {
            int idx;
            if(sscanf(ctx->video_filename, "Capture screen %d", &idx) && idx < num_screens) {
                CGDirectDisplayID screens[num_screens];
                CGGetActiveDisplayList(num_screens, screens, &num_screens);
                AVCaptureScreenInput* capture_screen_input = [[[AVCaptureScreenInput alloc] initWithDisplayID:screens[idx]] autorelease];
                video_device = (AVCaptureDevice*) capture_screen_input;
                ctx->video_device_index = ctx->num_video_devices + idx;
                ctx->video_is_screen = 1;

                if (ctx->framerate.num > 0) {
                    capture_screen_input.minFrameDuration = CMTimeMake(ctx->framerate.den, ctx->framerate.num);
                }

#if !TARGET_OS_IPHONE && __MAC_OS_X_VERSION_MIN_REQUIRED >= 1080
                if (ctx->capture_cursor) {
                    capture_screen_input.capturesCursor = YES;
                } else {
                    capture_screen_input.capturesCursor = NO;
                }
#endif

                if (ctx->capture_mouse_clicks) {
                    capture_screen_input.capturesMouseClicks = YES;
                } else {
                    capture_screen_input.capturesMouseClicks = NO;
                }
            }
        }
#endif
        }

        if (!video_device) {
            av_log(ctx, AV_LOG_ERROR, "Video device not found\n");
            goto fail;
        }
    }

    // get audio device
    if (avf_device_from_id(ctx, AVMediaTypeAudio, audio_devices, nil,
                           ctx->audio_device_id, &audio_device, NULL)) {
        if (!audio_device)
            goto fail;
    } else if (ctx->audio_device_index >= 0) {
        if (ctx->audio_device_index >= [audio_devices count]) {
            av_log(ctx, AV_LOG_ERROR, "Invalid audio device index\n");
            goto fail;
        }

        audio_device = [audio_devices objectAtIndex:ctx->audio_device_index];
    } else if (ctx->audio_filename &&
               strncmp(ctx->audio_filename, "none", 4)) {
        if (!strncmp(ctx->audio_filename, "default", 7)) {
            audio_device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio];
        } else {
        for (AVCaptureDevice *device in audio_devices) {
            if (!strncmp(ctx->audio_filename, [[device localizedName] UTF8String], strlen(ctx->audio_filename))) {
                audio_device = device;
                break;
            }
        }
        }

        if (!audio_device) {
            av_log(ctx, AV_LOG_ERROR, "Audio device not found\n");
             goto fail;
        }
    }

    // Video nor Audio capture device not found, looking for AVMediaTypeVideo/Audio
    if (!video_device && !audio_device) {
        av_log(s, AV_LOG_ERROR, "No AV capture device found\n");
        goto fail;
    }

    if (video_device) {
        if (!ctx->video_is_screen) {
            av_log(s, AV_LOG_DEBUG, "'%s' opened\n", [[video_device localizedName] UTF8String]);
        } else {
            av_log(s, AV_LOG_DEBUG, "'%s' opened\n", [[video_device description] UTF8String]);
        }
    }
    if (audio_device) {
        av_log(s, AV_LOG_DEBUG, "audio device '%s' opened\n", [[audio_device localizedName] UTF8String]);
    }

    // Initialize capture session
    ctx->capture_session = [[AVCaptureSession alloc] init];

    if (video_device && add_video_device(s, video_device)) {
        goto fail;
    }
    if (audio_device && add_audio_device(s, audio_device)) {
    }

    [ctx->capture_session startRunning];

    /* Unlock device configuration only after the session is started so it
     * does not reset the capture formats */
    if (!ctx->video_is_screen) {
        [video_device unlockForConfiguration];
    }

    if (video_device && get_video_config(s)) {
        goto fail;
    }

    // set audio stream
    if (audio_device && get_audio_config(s)) {
        goto fail;
    }

    [pool release];
    return 0;

fail:
    [pool release];
    destroy_context(ctx);
    if (ret)
        return ret;
    return AVERROR(EIO);
}

static int copy_cvpixelbuffer(AVFormatContext *s,
                               CVPixelBufferRef image_buffer,
                               AVPacket *pkt)
{
    AVFContext *ctx = s->priv_data;
    int src_linesize[4];
    const uint8_t *src_data[4];
    int width  = CVPixelBufferGetWidth(image_buffer);
    int height = CVPixelBufferGetHeight(image_buffer);
    int status;

    memset(src_linesize, 0, sizeof(src_linesize));
    memset(src_data, 0, sizeof(src_data));

    status = CVPixelBufferLockBaseAddress(image_buffer, 0);
    if (status != kCVReturnSuccess) {
        av_log(s, AV_LOG_ERROR, "Could not lock base address: %d (%dx%d)\n", status, width, height);
        return AVERROR_EXTERNAL;
    }

    if (CVPixelBufferIsPlanar(image_buffer)) {
        size_t plane_count = CVPixelBufferGetPlaneCount(image_buffer);
        int i;
        for(i = 0; i < plane_count; i++){
            src_linesize[i] = CVPixelBufferGetBytesPerRowOfPlane(image_buffer, i);
            src_data[i] = CVPixelBufferGetBaseAddressOfPlane(image_buffer, i);
        }
    } else {
        src_linesize[0] = CVPixelBufferGetBytesPerRow(image_buffer);
        src_data[0] = CVPixelBufferGetBaseAddress(image_buffer);
    }

    status = av_image_copy_to_buffer(pkt->data, pkt->size,
                                     src_data, src_linesize,
                                     ctx->pixel_format, width, height, 1);



    CVPixelBufferUnlockBaseAddress(image_buffer, 0);

    return status;
}

static int avf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVFContext* ctx = (AVFContext*)s->priv_data;

    lock_frames(ctx);
    do {
        CVImageBufferRef image_buffer;
        CMBlockBufferRef block_buffer;

        if (ctx->current_frame != nil) {
            int status;
            int length = 0;

            image_buffer = CMSampleBufferGetImageBuffer(ctx->current_frame);
            block_buffer = CMSampleBufferGetDataBuffer(ctx->current_frame);

            if (image_buffer != nil) {
                length = (int)CVPixelBufferGetDataSize(image_buffer);
            } else if (block_buffer != nil) {
                length = (int)CMBlockBufferGetDataLength(block_buffer);
            } else  {
                unlock_frames(ctx);
                return AVERROR(EINVAL);
            }

            if (av_new_packet(pkt, length) < 0) {
                unlock_frames(ctx);
                return AVERROR(EIO);
            }

            CMItemCount count;
            CMSampleTimingInfo timing_info;

            if (CMSampleBufferGetOutputSampleTimingInfoArray(ctx->current_frame, 1, &timing_info, &count) == noErr) {
                AVRational timebase_q = av_make_q(1, timing_info.presentationTimeStamp.timescale);
                pkt->pts = pkt->dts = av_rescale_q(timing_info.presentationTimeStamp.value, timebase_q, avf_time_base_q);
            }

            pkt->stream_index  = ctx->video_stream_index;
            pkt->flags        |= AV_PKT_FLAG_KEY;

            if (image_buffer) {
                status = copy_cvpixelbuffer(s, image_buffer, pkt);
            } else {
                status = 0;
                OSStatus ret = CMBlockBufferCopyDataBytes(block_buffer, 0, pkt->size, pkt->data);
                if (ret != kCMBlockBufferNoErr) {
                    status = AVERROR(EIO);
                }
             }
            CFRelease(ctx->current_frame);
            ctx->current_frame = nil;

            if (status < 0) {
                unlock_frames(ctx);
                return status;
            }
        } else if (ctx->current_audio_frame != nil) {
            CMBlockBufferRef block_buffer = CMSampleBufferGetDataBuffer(ctx->current_audio_frame);
            int block_buffer_size         = CMBlockBufferGetDataLength(block_buffer);

            if (!block_buffer || !block_buffer_size) {
                unlock_frames(ctx);
                return AVERROR(EIO);
            }

            if (ctx->audio_non_interleaved && block_buffer_size > ctx->audio_buffer_size) {
                unlock_frames(ctx);
                return AVERROR_BUFFER_TOO_SMALL;
            }

            if (av_new_packet(pkt, block_buffer_size) < 0) {
                unlock_frames(ctx);
                return AVERROR(EIO);
            }

            CMItemCount count;
            CMSampleTimingInfo timing_info;

            if (CMSampleBufferGetOutputSampleTimingInfoArray(ctx->current_audio_frame, 1, &timing_info, &count) == noErr) {
                AVRational timebase_q = av_make_q(1, timing_info.presentationTimeStamp.timescale);
                pkt->pts = pkt->dts = av_rescale_q(timing_info.presentationTimeStamp.value, timebase_q, avf_time_base_q);
            }

            pkt->stream_index  = ctx->audio_stream_index;
            pkt->flags        |= AV_PKT_FLAG_KEY;

            if (ctx->audio_non_interleaved) {
                int sample, c, shift, num_samples;

                OSStatus ret = CMBlockBufferCopyDataBytes(block_buffer, 0, pkt->size, ctx->audio_buffer);
                if (ret != kCMBlockBufferNoErr) {
                    unlock_frames(ctx);
                    return AVERROR(EIO);
                }

                num_samples = pkt->size / (ctx->audio_channels * (ctx->audio_bits_per_sample >> 3));

                // transform decoded frame into output format
                #define INTERLEAVE_OUTPUT(bps)                                         \
                {                                                                      \
                    int##bps##_t **src;                                                \
                    int##bps##_t *dest;                                                \
                    src = av_malloc(ctx->audio_channels * sizeof(int##bps##_t*));      \
                    if (!src) {                                                        \
                        unlock_frames(ctx);                                            \
                        return AVERROR(EIO);                                           \
                    }                                                                  \
                                                                                       \
                    for (c = 0; c < ctx->audio_channels; c++) {                        \
                        src[c] = ((int##bps##_t*)ctx->audio_buffer) + c * num_samples; \
                    }                                                                  \
                    dest  = (int##bps##_t*)pkt->data;                                  \
                    shift = bps - ctx->audio_bits_per_sample;                          \
                    for (sample = 0; sample < num_samples; sample++)                   \
                        for (c = 0; c < ctx->audio_channels; c++)                      \
                            *dest++ = src[c][sample] << shift;                         \
                    av_freep(&src);                                                    \
                }

                if (ctx->audio_bits_per_sample <= 16) {
                    INTERLEAVE_OUTPUT(16)
                } else {
                    INTERLEAVE_OUTPUT(32)
                }
            } else {
                OSStatus ret = CMBlockBufferCopyDataBytes(block_buffer, 0, pkt->size, pkt->data);
                if (ret != kCMBlockBufferNoErr) {
                    unlock_frames(ctx);
                    return AVERROR(EIO);
                }
            }

            CFRelease(ctx->current_audio_frame);
            ctx->current_audio_frame = nil;
        } else {
            pkt->data = NULL;
            if (ctx->observed_quit) {
                unlock_frames(ctx);
                return AVERROR_EOF;
            }
            // No frame available yet: wait until a capture callback delivers
            // one (or until the device is being torn down).
            pthread_cond_wait(&ctx->frame_wait_cond, &ctx->frame_lock);
        }
    } while (!pkt->data && !ctx->is_stopping);

    if (ctx->is_stopping) {
        unlock_frames(ctx);
        return AVERROR_EOF;
    }
    unlock_frames(ctx);

    return 0;
}

static int avf_close(AVFormatContext *s)
{
    AVFContext* ctx = (AVFContext*)s->priv_data;
    destroy_context(ctx);
    return 0;
}

static const AVOption options[] = {
    { "list_devices", "list available devices", offsetof(AVFContext, list_devices), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, AV_OPT_FLAG_DECODING_PARAM },
    { "video_device_index", "select video device by index for devices with same name (starts at 0)", offsetof(AVFContext, video_device_index), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "audio_device_index", "select audio device by index for devices with same name (starts at 0)", offsetof(AVFContext, audio_device_index), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "video_device_id", "select video device by prefixed id (uid:<unique ID> or serial:<USB serial number>)", offsetof(AVFContext, video_device_id), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM },
    { "audio_device_id", "select audio device by prefixed id (uid:<unique ID> or serial:<USB serial number>)", offsetof(AVFContext, audio_device_id), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM },
    { "pixel_format", "set pixel format", offsetof(AVFContext, pixel_format), AV_OPT_TYPE_PIXEL_FMT, {.i64 = AV_PIX_FMT_YUV420P}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM},
    { "framerate", "set frame rate", offsetof(AVFContext, framerate), AV_OPT_TYPE_VIDEO_RATE, {.str = "ntsc"}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "video_size", "set video size", offsetof(AVFContext, width), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM },
    { "capture_cursor", "capture the screen cursor", offsetof(AVFContext, capture_cursor), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, AV_OPT_FLAG_DECODING_PARAM },
    { "capture_mouse_clicks", "capture the screen mouse clicks", offsetof(AVFContext, capture_mouse_clicks), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, AV_OPT_FLAG_DECODING_PARAM },
    { "capture_raw_data", "capture the raw data from device connection", offsetof(AVFContext, capture_raw_data), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, AV_OPT_FLAG_DECODING_PARAM },
    { "drop_late_frames", "drop frames that are available later than expected", offsetof(AVFContext, drop_late_frames), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, AV_OPT_FLAG_DECODING_PARAM },

    { NULL },
};

static const AVClass avf_class = {
    .class_name = "AVFoundation indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
};

const FFInputFormat ff_avfoundation_demuxer = {
    .p.name         = "avfoundation",
    .p.long_name    = NULL_IF_CONFIG_SMALL("AVFoundation input device"),
    .p.flags        = AVFMT_NOFILE,
    .p.priv_class   = &avf_class,
    .priv_data_size = sizeof(AVFContext),
    .read_header    = avf_read_header,
    .read_packet    = avf_read_packet,
    .read_close     = avf_close,
};
