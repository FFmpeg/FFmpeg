/*
 * BeOS audio play interface
 * Copyright (c) 2000, 2001 Fabrice Bellard
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

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <Application.h>
#include <SoundPlayer.h>

extern "C" {
#include "libavformat/avformat.h"
}

#if HAVE_BSOUNDRECORDER
#include <SoundRecorder.h>
using namespace BPrivate::Media::Experimental;
#endif

/* enable performance checks */
//#define PERF_CHECK

/* enable Media Kit latency checks */
//#define LATENCY_CHECK

#define AUDIO_BLOCK_SIZE 4096
#define AUDIO_BLOCK_COUNT 8

#define AUDIO_BUFFER_SIZE (AUDIO_BLOCK_SIZE*AUDIO_BLOCK_COUNT)

typedef struct {
    int fd; // UNUSED
    int sample_rate;
    int channels;
    int frame_size; /* in bytes ! */
    CodecID codec_id;
    uint8_t buffer[AUDIO_BUFFER_SIZE];
    int buffer_ptr;
    /* ring buffer */
    sem_id input_sem;
    int input_index;
    sem_id output_sem;
    int output_index;
    BSoundPlayer *player;
#if HAVE_BSOUNDRECORDER
    BSoundRecorder *recorder;
#endif
    int has_quit; /* signal callbacks not to wait */
    volatile bigtime_t starve_time;
} AudioData;

static thread_id main_thid;
static thread_id bapp_thid;
static int own_BApp_created = 0;
static int refcount = 0;

/* create the BApplication and Run() it */
static int32 bapp_thread(void *arg)
{
    new BApplication("application/x-vnd.ffmpeg");
    own_BApp_created = 1;
    be_app->Run();
    /* kill the process group */
//    kill(0, SIGINT);
//    kill(main_thid, SIGHUP);
    return B_OK;
}

/* create the BApplication only if needed */
static void create_bapp_if_needed(void)
{
    if (refcount++ == 0) {
        /* needed by libmedia */
        if (be_app == NULL) {
            bapp_thid = spawn_thread(bapp_thread, "ffmpeg BApplication", B_NORMAL_PRIORITY, NULL);
            resume_thread(bapp_thid);
            while (!own_BApp_created)
                snooze(50000);
        }
    }
}

static void destroy_bapp_if_needed(void)
{
    if (--refcount == 0 && own_BApp_created) {
        be_app->Lock();
        be_app->Quit();
        be_app = NULL;
    }
}

/* called back by BSoundPlayer */
static void audioplay_callback(void *cookie, void *buffer, size_t bufferSize, const media_raw_audio_format &format)
{
    AudioData *s;
    size_t len, amount;
    unsigned char *buf = (unsigned char *)buffer;

    s = (AudioData *)cookie;
    if (s->has_quit)
        return;
    while (bufferSize > 0) {
#ifdef PERF_CHECK
        bigtime_t t;
        t = system_time();
#endif
        len = MIN(AUDIO_BLOCK_SIZE, bufferSize);
        if (acquire_sem_etc(s->output_sem, len, B_CAN_INTERRUPT, 0LL) < B_OK) {
            s->has_quit = 1;
            s->player->SetHasData(false);
            return;
        }
        amount = MIN(len, (AUDIO_BUFFER_SIZE - s->output_index));
        memcpy(buf, &s->buffer[s->output_index], amount);
        s->output_index += amount;
        if (s->output_index >= AUDIO_BUFFER_SIZE) {
            s->output_index %= AUDIO_BUFFER_SIZE;
            memcpy(buf + amount, &s->buffer[s->output_index], len - amount);
            s->output_index += len-amount;
            s->output_index %= AUDIO_BUFFER_SIZE;
        }
        release_sem_etc(s->input_sem, len, 0);
#ifdef PERF_CHECK
        t = system_time() - t;
        s->starve_time = MAX(s->starve_time, t);
#endif
        buf += len;
        bufferSize -= len;
    }
}

#if HAVE_BSOUNDRECORDER
/* called back by BSoundRecorder */
static void audiorecord_callback(void *cookie, bigtime_t timestamp, void *buffer, size_t bufferSize, const media_multi_audio_format &format)
{
    AudioData *s;
    size_t len, amount;
    unsigned char *buf = (unsigned char *)buffer;

    s = (AudioData *)cookie;
    if (s->has_quit)
        return;

    while (bufferSize > 0) {
        len = MIN(bufferSize, AUDIO_BLOCK_SIZE);
        //printf("acquire_sem(input, %d)\n", len);
        if (acquire_sem_etc(s->input_sem, len, B_CAN_INTERRUPT, 0LL) < B_OK) {
            s->has_quit = 1;
            return;
        }
        amount = MIN(len, (AUDIO_BUFFER_SIZE - s->input_index));
        memcpy(&s->buffer[s->input_index], buf, amount);
        s->input_index += amount;
        if (s->input_index >= AUDIO_BUFFER_SIZE) {
            s->input_index %= AUDIO_BUFFER_SIZE;
            memcpy(&s->buffer[s->input_index], buf + amount, len - amount);
            s->input_index += len - amount;
        }
        release_sem_etc(s->output_sem, len, 0);
        //printf("release_sem(output, %d)\n", len);
        buf += len;
        bufferSize -= len;
    }
}
#endif

static int audio_open(AudioData *s, int is_output, const char *audio_device)
{
    int p[2];
    int ret;
    media_raw_audio_format format;
    media_multi_audio_format iformat;

#if !HAVE_BSOUNDRECORDER
    if (!is_output)
        return AVERROR(EIO); /* not for now */
#endif
    s->input_sem = create_sem(AUDIO_BUFFER_SIZE, "ffmpeg_ringbuffer_input");
    if (s->input_sem < B_OK)
        return AVERROR(EIO);
    s->output_sem = create_sem(0, "ffmpeg_ringbuffer_output");
    if (s->output_sem < B_OK) {
        delete_sem(s->input_sem);
        return AVERROR(EIO);
    }
    s->input_index = 0;
    s->output_index = 0;
    create_bapp_if_needed();
    s->frame_size = AUDIO_BLOCK_SIZE;
    /* bump up the priority (avoid realtime though) */
    set_thread_priority(find_thread(NULL), B_DISPLAY_PRIORITY+1);
#if HAVE_BSOUNDRECORDER
    if (!is_output) {
        bool wait_for_input = false;
        if (audio_device && !strcmp(audio_device, "wait:"))
            wait_for_input = true;
        s->recorder = new BSoundRecorder(&iformat, wait_for_input, "ffmpeg input", audiorecord_callback);
        if (wait_for_input && (s->recorder->InitCheck() == B_OK)) {
            s->recorder->WaitForIncomingConnection(&iformat);
        }
        if (s->recorder->InitCheck() != B_OK || iformat.format != media_raw_audio_format::B_AUDIO_SHORT) {
            delete s->recorder;
            s->recorder = NULL;
            if (s->input_sem)
                delete_sem(s->input_sem);
            if (s->output_sem)
                delete_sem(s->output_sem);
            return AVERROR(EIO);
        }
        s->codec_id = (iformat.byte_order == B_MEDIA_LITTLE_ENDIAN)?CODEC_ID_PCM_S16LE:CODEC_ID_PCM_S16BE;
        s->channels = iformat.channel_count;
        s->sample_rate = (int)iformat.frame_rate;
        s->frame_size = iformat.buffer_size;
        s->recorder->SetCookie(s);
        s->recorder->SetVolume(1.0);
        s->recorder->Start();
        return 0;
    }
#endif
    format = media_raw_audio_format::wildcard;
    format.format = media_raw_audio_format::B_AUDIO_SHORT;
    format.byte_order = B_HOST_IS_LENDIAN ? B_MEDIA_LITTLE_ENDIAN : B_MEDIA_BIG_ENDIAN;
    format.channel_count = s->channels;
    format.buffer_size = s->frame_size;
    format.frame_rate = s->sample_rate;
    s->player = new BSoundPlayer(&format, "ffmpeg output", audioplay_callback);
    if (s->player->InitCheck() != B_OK) {
        delete s->player;
        s->player = NULL;
        if (s->input_sem)
            delete_sem(s->input_sem);
        if (s->output_sem)
            delete_sem(s->output_sem);
        return AVERROR(EIO);
    }
    s->player->SetCookie(s);
    s->player->SetVolume(1.0);
    s->player->Start();
    s->player->SetHasData(true);
    return 0;
}

static int audio_close(AudioData *s)
{
    if (s->input_sem)
        delete_sem(s->input_sem);
    if (s->output_sem)
        delete_sem(s->output_sem);
    s->has_quit = 1;
    if (s->player) {
        s->player->Stop();
    }
    if (s->player)
        delete s->player;
#if HAVE_BSOUNDRECORDER
    if (s->recorder)
        delete s->recorder;
#endif
    destroy_bapp_if_needed();
    return 0;
}

/* sound output support */
static int audio_write_header(AVFormatContext *s1)
{
    AudioData *s = (AudioData *)s1->priv_data;
    AVStream *st;
    int ret;

    st = s1->streams[0];
    s->sample_rate = st->codec->sample_rate;
    s->channels = st->codec->channels;
    ret = audio_open(s, 1, NULL);
    if (ret < 0)
        return AVERROR(EIO);
    return 0;
}

static int audio_write_packet(AVFormatContext *s1, AVPacket *pkt)
{
    AudioData *s = (AudioData *)s1->priv_data;
    int len, ret;
    const uint8_t *buf = pkt->data;
    int size = pkt->size;
#ifdef LATENCY_CHECK
bigtime_t lat1, lat2;
lat1 = s->player->Latency();
#endif
#ifdef PERF_CHECK
    bigtime_t t = s->starve_time;
    s->starve_time = 0;
    printf("starve_time: %lld    \n", t);
#endif
    while (size > 0) {
        int amount;
        len = MIN(size, AUDIO_BLOCK_SIZE);
        if (acquire_sem_etc(s->input_sem, len, B_CAN_INTERRUPT, 0LL) < B_OK)
            return AVERROR(EIO);
        amount = MIN(len, (AUDIO_BUFFER_SIZE - s->input_index));
        memcpy(&s->buffer[s->input_index], buf, amount);
        s->input_index += amount;
        if (s->input_index >= AUDIO_BUFFER_SIZE) {
            s->input_index %= AUDIO_BUFFER_SIZE;
            memcpy(&s->buffer[s->input_index], buf + amount, len - amount);
            s->input_index += len - amount;
        }
        release_sem_etc(s->output_sem, len, 0);
        buf += len;
        size -= len;
    }
#ifdef LATENCY_CHECK
lat2 = s->player->Latency();
printf("#### BSoundPlayer::Latency(): before= %lld, after= %lld\n", lat1, lat2);
#endif
    return 0;
}

static int audio_write_trailer(AVFormatContext *s1)
{
    AudioData *s = (AudioData *)s1->priv_data;

    audio_close(s);
    return 0;
}

/* grab support */

static int audio_read_header(AVFormatContext *s1, AVFormatParameters *ap)
{
    AudioData *s = (AudioData *)s1->priv_data;
    AVStream *st;
    int ret;

    if (!ap || ap->sample_rate <= 0 || ap->channels <= 0)
        return -1;

    st = av_new_stream(s1, 0);
    if (!st) {
        return AVERROR(ENOMEM);
    }
    s->sample_rate = ap->sample_rate;
    s->channels = ap->channels;

    ret = audio_open(s, 0, s1->filename);
    if (ret < 0) {
        av_free(st);
        return AVERROR(EIO);
    }
    /* take real parameters */
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = s->codec_id;
    st->codec->sample_rate = s->sample_rate;
    st->codec->channels = s->channels;
    return 0;
    av_set_pts_info(st, 48, 1, 1000000);  /* 48 bits pts in us */
}

static int audio_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    AudioData *s = (AudioData *)s1->priv_data;
    int size;
    size_t len, amount;
    unsigned char *buf;
    status_t err;

    if (av_new_packet(pkt, s->frame_size) < 0)
        return AVERROR(EIO);
    buf = (unsigned char *)pkt->data;
    size = pkt->size;
    while (size > 0) {
        len = MIN(AUDIO_BLOCK_SIZE, size);
        //printf("acquire_sem(output, %d)\n", len);
        while ((err=acquire_sem_etc(s->output_sem, len, B_CAN_INTERRUPT, 0LL)) == B_INTERRUPTED);
        if (err < B_OK) {
            av_free_packet(pkt);
            return AVERROR(EIO);
        }
        amount = MIN(len, (AUDIO_BUFFER_SIZE - s->output_index));
        memcpy(buf, &s->buffer[s->output_index], amount);
        s->output_index += amount;
        if (s->output_index >= AUDIO_BUFFER_SIZE) {
            s->output_index %= AUDIO_BUFFER_SIZE;
            memcpy(buf + amount, &s->buffer[s->output_index], len - amount);
            s->output_index += len-amount;
            s->output_index %= AUDIO_BUFFER_SIZE;
        }
        release_sem_etc(s->input_sem, len, 0);
        //printf("release_sem(input, %d)\n", len);
        buf += len;
        size -= len;
    }
    //XXX: add pts info
    return 0;
}

static int audio_read_close(AVFormatContext *s1)
{
    AudioData *s = (AudioData *)s1->priv_data;

    audio_close(s);
    return 0;
}

static AVInputFormat audio_beos_demuxer = {
    "audio_beos",
    NULL_IF_CONFIG_SMALL("audio grab and output"),
    sizeof(AudioData),
    NULL,
    audio_read_header,
    audio_read_packet,
    audio_read_close,
    NULL,
    NULL,
    AVFMT_NOFILE,
};

AVOutputFormat audio_beos_muxer = {
    "audio_beos",
    NULL_IF_CONFIG_SMALL("audio grab and output"),
    "",
    "",
    sizeof(AudioData),
#if HAVE_BIGENDIAN
    CODEC_ID_PCM_S16BE,
#else
    CODEC_ID_PCM_S16LE,
#endif
    CODEC_ID_NONE,
    audio_write_header,
    audio_write_packet,
    audio_write_trailer,
    AVFMT_NOFILE,
};

extern "C" {

int audio_init(void)
{
    main_thid = find_thread(NULL);
    av_register_input_format(&audio_beos_demuxer);
    av_register_output_format(&audio_beos_muxer);
    return 0;
}

} // "C"

