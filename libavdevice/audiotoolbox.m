/*
 * AudioToolbox output device
 * Copyright (c) 2020 Thilo Borgmann <thilo.borgmann@mail.de>
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
 * AudioToolbox output device
 * @author Thilo Borgmann <thilo.borgmann@mail.de>
 */

#import <AudioToolbox/AudioToolbox.h>
#include <pthread.h>

#include "libavutil/opt.h"
#include "libavformat/internal.h"
#include "libavformat/mux.h"
#include "libavutil/internal.h"
#include "avdevice.h"

typedef struct
{
    AVClass             *class;

    AudioQueueBufferRef buffer[2];
    pthread_mutex_t     buffer_lock[2];
    int                 cur_buf;
    AudioQueueRef       queue;

    int                 list_devices;
    int                 audio_device_index;

} ATContext;

static int check_status(AVFormatContext *avctx, OSStatus *status, const char *msg)
{
    if (*status != noErr) {
        av_log(avctx, AV_LOG_ERROR, "Error: %s (%i)\n", msg, *status);
        return 1;
    } else {
        av_log(avctx, AV_LOG_DEBUG, " OK  : %s\n", msg);
        return 0;
    }
}

static void queue_callback(void* atctx, AudioQueueRef inAQ,
                           AudioQueueBufferRef inBuffer)
{
    // unlock the buffer that has just been consumed
    ATContext *ctx = (ATContext*)atctx;
    for (int i = 0; i < 2; i++) {
        if (inBuffer == ctx->buffer[i]) {
            pthread_mutex_unlock(&ctx->buffer_lock[i]);
        }
    }
}

static av_cold int at_write_header(AVFormatContext *avctx)
{
    ATContext *ctx = (ATContext*)avctx->priv_data;
    OSStatus err = noErr;
    CFStringRef device_UID = NULL;
    AudioDeviceID *devices;
    int num_devices;


    // get devices
    UInt32 data_size = 0;
    AudioObjectPropertyAddress prop;
    prop.mSelector = kAudioHardwarePropertyDevices;
    prop.mScope    = kAudioObjectPropertyScopeGlobal;
    prop.mElement  = kAudioObjectPropertyElementMaster;
    err = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &prop, 0, NULL, &data_size);
    if (check_status(avctx, &err, "AudioObjectGetPropertyDataSize devices"))
        return AVERROR(EINVAL);

    num_devices = data_size / sizeof(AudioDeviceID);

    devices = (AudioDeviceID*)(av_malloc(data_size));
    err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop, 0, NULL, &data_size, devices);
    if (check_status(avctx, &err, "AudioObjectGetPropertyData devices")) {
        av_freep(&devices);
        return AVERROR(EINVAL);
    }

    // list devices
    if (ctx->list_devices) {
        CFStringRef device_name = NULL;
        prop.mScope = kAudioDevicePropertyScopeInput;

        av_log(ctx, AV_LOG_INFO, "CoreAudio devices:\n");
        for(UInt32 i = 0; i < num_devices; ++i) {
            // UID
            data_size = sizeof(device_UID);
            prop.mSelector = kAudioDevicePropertyDeviceUID;
            err = AudioObjectGetPropertyData(devices[i], &prop, 0, NULL, &data_size, &device_UID);
            if (check_status(avctx, &err, "AudioObjectGetPropertyData UID"))
                continue;

            // name
            data_size = sizeof(device_name);
            prop.mSelector = kAudioDevicePropertyDeviceNameCFString;
            err = AudioObjectGetPropertyData(devices[i], &prop, 0, NULL, &data_size, &device_name);
            if (check_status(avctx, &err, "AudioObjecTGetPropertyData name"))
                continue;

            av_log(ctx, AV_LOG_INFO, "[%d] %30s, %s\n", i,
                   CFStringGetCStringPtr(device_name, kCFStringEncodingMacRoman),
                   CFStringGetCStringPtr(device_UID, kCFStringEncodingMacRoman));
        }
    }

    // get user-defined device UID or use default device
    // -audio_device_index overrides any URL given
    const char *stream_name = avctx->url;
    if (stream_name && ctx->audio_device_index == -1) {
        sscanf(stream_name, "%d", &ctx->audio_device_index);
    }

    if (ctx->audio_device_index >= 0) {
        // get UID of selected device
        data_size = sizeof(device_UID);
        prop.mSelector = kAudioDevicePropertyDeviceUID;
        err = AudioObjectGetPropertyData(devices[ctx->audio_device_index], &prop, 0, NULL, &data_size, &device_UID);
        if (check_status(avctx, &err, "AudioObjecTGetPropertyData UID")) {
            av_freep(&devices);
            return AVERROR(EINVAL);
        }
    } else {
        // use default device
        device_UID = NULL;
    }

    av_log(ctx, AV_LOG_DEBUG, "stream_name:        %s\n", stream_name);
    av_log(ctx, AV_LOG_DEBUG, "audio_device_idnex: %i\n", ctx->audio_device_index);
    av_log(ctx, AV_LOG_DEBUG, "UID:                %s\n", CFStringGetCStringPtr(device_UID, kCFStringEncodingMacRoman));

    // check input stream
    if (avctx->nb_streams != 1 || avctx->streams[0]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        av_log(ctx, AV_LOG_ERROR, "Only a single audio stream is supported.\n");
        return AVERROR(EINVAL);
    }

    av_freep(&devices);
    AVCodecParameters *codecpar = avctx->streams[0]->codecpar;

    // audio format
    AudioStreamBasicDescription device_format = {0};
    device_format.mSampleRate        = codecpar->sample_rate;
    device_format.mFormatID          = kAudioFormatLinearPCM;
    device_format.mFormatFlags      |= (codecpar->format == AV_SAMPLE_FMT_FLT) ? kLinearPCMFormatFlagIsFloat : 0;
    device_format.mFormatFlags      |= (codecpar->codec_id == AV_CODEC_ID_PCM_S8) ? kLinearPCMFormatFlagIsSignedInteger : 0;
    device_format.mFormatFlags      |= (codecpar->codec_id == AV_NE(AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S16LE)) ? kLinearPCMFormatFlagIsSignedInteger : 0;
    device_format.mFormatFlags      |= (codecpar->codec_id == AV_NE(AV_CODEC_ID_PCM_S24BE, AV_CODEC_ID_PCM_S24LE)) ? kLinearPCMFormatFlagIsSignedInteger : 0;
    device_format.mFormatFlags      |= (codecpar->codec_id == AV_NE(AV_CODEC_ID_PCM_S32BE, AV_CODEC_ID_PCM_S32LE)) ? kLinearPCMFormatFlagIsSignedInteger : 0;
    device_format.mFormatFlags      |= (av_sample_fmt_is_planar(codecpar->format)) ? kAudioFormatFlagIsNonInterleaved : 0;
    device_format.mFormatFlags      |= (codecpar->codec_id == AV_CODEC_ID_PCM_F32BE) ? kAudioFormatFlagIsBigEndian : 0;
    device_format.mFormatFlags      |= (codecpar->codec_id == AV_CODEC_ID_PCM_S16BE) ? kAudioFormatFlagIsBigEndian : 0;
    device_format.mFormatFlags      |= (codecpar->codec_id == AV_CODEC_ID_PCM_S24BE) ? kAudioFormatFlagIsBigEndian : 0;
    device_format.mFormatFlags      |= (codecpar->codec_id == AV_CODEC_ID_PCM_S32BE) ? kAudioFormatFlagIsBigEndian : 0;
    device_format.mChannelsPerFrame  = codecpar->ch_layout.nb_channels;
    device_format.mBitsPerChannel    = (codecpar->codec_id == AV_NE(AV_CODEC_ID_PCM_S24BE, AV_CODEC_ID_PCM_S24LE)) ? 24 : (av_get_bytes_per_sample(codecpar->format) << 3);
    device_format.mBytesPerFrame     = (device_format.mBitsPerChannel >> 3) * device_format.mChannelsPerFrame;
    device_format.mFramesPerPacket   = 1;
    device_format.mBytesPerPacket    = device_format.mBytesPerFrame * device_format.mFramesPerPacket;
    device_format.mReserved          = 0;

    av_log(ctx, AV_LOG_DEBUG, "device_format.mSampleRate        = %i\n", codecpar->sample_rate);
    av_log(ctx, AV_LOG_DEBUG, "device_format.mFormatID          = %s\n", "kAudioFormatLinearPCM");
    av_log(ctx, AV_LOG_DEBUG, "device_format.mFormatFlags      |= %s\n", (codecpar->format == AV_SAMPLE_FMT_FLT) ? "kLinearPCMFormatFlagIsFloat" : "0");
    av_log(ctx, AV_LOG_DEBUG, "device_format.mFormatFlags      |= %s\n", (codecpar->codec_id == AV_CODEC_ID_PCM_S8) ? "kLinearPCMFormatFlagIsSignedInteger" : "0");
    av_log(ctx, AV_LOG_DEBUG, "device_format.mFormatFlags      |= %s\n", (codecpar->codec_id == AV_NE(AV_CODEC_ID_PCM_S32BE, AV_CODEC_ID_PCM_S32LE)) ? "kLinearPCMFormatFlagIsSignedInteger" : "0");
    av_log(ctx, AV_LOG_DEBUG, "device_format.mFormatFlags      |= %s\n", (codecpar->codec_id == AV_NE(AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S16LE)) ? "kLinearPCMFormatFlagIsSignedInteger" : "0");
    av_log(ctx, AV_LOG_DEBUG, "device_format.mFormatFlags      |= %s\n", (codecpar->codec_id == AV_NE(AV_CODEC_ID_PCM_S24BE, AV_CODEC_ID_PCM_S24LE)) ? "kLinearPCMFormatFlagIsSignedInteger" : "0");
    av_log(ctx, AV_LOG_DEBUG, "device_format.mFormatFlags      |= %s\n", (av_sample_fmt_is_planar(codecpar->format)) ? "kAudioFormatFlagIsNonInterleaved" : "0");
    av_log(ctx, AV_LOG_DEBUG, "device_format.mFormatFlags      |= %s\n", (codecpar->codec_id == AV_CODEC_ID_PCM_F32BE) ? "kAudioFormatFlagIsBigEndian" : "0");
    av_log(ctx, AV_LOG_DEBUG, "device_format.mFormatFlags      |= %s\n", (codecpar->codec_id == AV_CODEC_ID_PCM_S16BE) ? "kAudioFormatFlagIsBigEndian" : "0");
    av_log(ctx, AV_LOG_DEBUG, "device_format.mFormatFlags      |= %s\n", (codecpar->codec_id == AV_CODEC_ID_PCM_S24BE) ? "kAudioFormatFlagIsBigEndian" : "0");
    av_log(ctx, AV_LOG_DEBUG, "device_format.mFormatFlags      |= %s\n", (codecpar->codec_id == AV_CODEC_ID_PCM_S32BE) ? "kAudioFormatFlagIsBigEndian" : "0");
    av_log(ctx, AV_LOG_DEBUG, "device_format.mFormatFlags      == %i\n", device_format.mFormatFlags);
    av_log(ctx, AV_LOG_DEBUG, "device_format.mChannelsPerFrame  = %i\n", codecpar->ch_layout.nb_channels);
    av_log(ctx, AV_LOG_DEBUG, "device_format.mBitsPerChannel    = %i\n", av_get_bytes_per_sample(codecpar->format) << 3);
    av_log(ctx, AV_LOG_DEBUG, "device_format.mBytesPerFrame     = %i\n", (device_format.mBitsPerChannel >> 3) * codecpar->ch_layout.nb_channels);
    av_log(ctx, AV_LOG_DEBUG, "device_format.mBytesPerPacket    = %i\n", device_format.mBytesPerFrame);
    av_log(ctx, AV_LOG_DEBUG, "device_format.mFramesPerPacket   = %i\n", 1);
    av_log(ctx, AV_LOG_DEBUG, "device_format.mReserved          = %i\n", 0);

    // create new output queue for the device
    err = AudioQueueNewOutput(&device_format, queue_callback, ctx,
                              NULL, kCFRunLoopCommonModes,
                              0, &ctx->queue);
    if (check_status(avctx, &err, "AudioQueueNewOutput")) {
        if (err == kAudioFormatUnsupportedDataFormatError)
            av_log(ctx, AV_LOG_ERROR, "Unsupported output format.\n");
        return AVERROR(EINVAL);
    }

    // set user-defined device or leave untouched for default
    if (device_UID != NULL) {
        err = AudioQueueSetProperty(ctx->queue, kAudioQueueProperty_CurrentDevice, &device_UID, sizeof(device_UID));
        if (check_status(avctx, &err, "AudioQueueSetProperty output UID"))
            return AVERROR(EINVAL);
    }

    // start the queue
    err = AudioQueueStart(ctx->queue, NULL);
    if (check_status(avctx, &err, "AudioQueueStart"))
        return AVERROR(EINVAL);

    // init the mutexes for double-buffering
    pthread_mutex_init(&ctx->buffer_lock[0], NULL);
    pthread_mutex_init(&ctx->buffer_lock[1], NULL);

    return 0;
}

static int at_write_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    ATContext *ctx = (ATContext*)avctx->priv_data;
    OSStatus err = noErr;

    // use the other buffer
    ctx->cur_buf = !ctx->cur_buf;

    // lock for writing or wait for the buffer to be available
    // will be unlocked by queue callback
    pthread_mutex_lock(&ctx->buffer_lock[ctx->cur_buf]);

    // (re-)allocate the buffer if not existant or of different size
    if (!ctx->buffer[ctx->cur_buf] || ctx->buffer[ctx->cur_buf]->mAudioDataBytesCapacity != pkt->size) {
        err = AudioQueueAllocateBuffer(ctx->queue, pkt->size, &ctx->buffer[ctx->cur_buf]);
        if (check_status(avctx, &err, "AudioQueueAllocateBuffer")) {
            pthread_mutex_unlock(&ctx->buffer_lock[ctx->cur_buf]);
            return AVERROR(ENOMEM);
        }
    }

    AudioQueueBufferRef buf = ctx->buffer[ctx->cur_buf];

    // copy audio data into buffer and enqueue the buffer
    memcpy(buf->mAudioData, pkt->data, buf->mAudioDataBytesCapacity);
    buf->mAudioDataByteSize = buf->mAudioDataBytesCapacity;
    err = AudioQueueEnqueueBuffer(ctx->queue, buf, 0, NULL);
    if (check_status(avctx, &err, "AudioQueueEnqueueBuffer")) {
        pthread_mutex_unlock(&ctx->buffer_lock[ctx->cur_buf]);
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold int at_write_trailer(AVFormatContext *avctx)
{
    ATContext *ctx = (ATContext*)avctx->priv_data;
    OSStatus err = noErr;

    pthread_mutex_destroy(&ctx->buffer_lock[0]);
    pthread_mutex_destroy(&ctx->buffer_lock[1]);

    err = AudioQueueFlush(ctx->queue);
    check_status(avctx, &err, "AudioQueueFlush");
    err = AudioQueueDispose(ctx->queue, true);
    check_status(avctx, &err, "AudioQueueDispose");

    return 0;
}

static const AVOption options[] = {
    { "list_devices", "list available audio devices", offsetof(ATContext, list_devices), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM },
    { "audio_device_index", "select audio device by index (starts at 0)", offsetof(ATContext, audio_device_index), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { NULL },
};

static const AVClass at_class = {
    .class_name = "AudioToolbox",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_AUDIO_OUTPUT,
};

const FFOutputFormat ff_audiotoolbox_muxer = {
    .p.name         = "audiotoolbox",
    .p.long_name    = NULL_IF_CONFIG_SMALL("AudioToolbox output device"),
    .priv_data_size = sizeof(ATContext),
    .p.audio_codec  = AV_NE(AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S16LE),
    .p.video_codec  = AV_CODEC_ID_NONE,
    .write_header   = at_write_header,
    .write_packet   = at_write_packet,
    .write_trailer  = at_write_trailer,
    .p.flags        = AVFMT_NOFILE,
    .p.priv_class   = &at_class,
};
