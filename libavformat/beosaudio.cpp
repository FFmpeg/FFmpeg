/*
 * BeOS audio play interface
 * Copyright (c) 2000, 2001 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
#include "avformat.h"
}

/* enable performance checks */
//#define PERF_CHECK

/* Pipes are 4k in BeOS IIRC */
#define AUDIO_BLOCK_SIZE 4096
//#define AUDIO_BLOCK_SIZE 2048
#define AUDIO_BLOCK_COUNT 8

#define AUDIO_BUFFER_SIZE (AUDIO_BLOCK_SIZE*AUDIO_BLOCK_COUNT)

/* pipes suck for realtime */
#define USE_RING_BUFFER 1

typedef struct {
    int fd;
    int sample_rate;
    int channels;
    int frame_size; /* in bytes ! */
    CodecID codec_id;
    int flip_left : 1;
    uint8_t buffer[AUDIO_BUFFER_SIZE];
    int buffer_ptr;
    int pipefd; /* the other end of the pipe */
    /* ring buffer */
    sem_id input_sem;
    int input_index;
    sem_id output_sem;
    int output_index;
    int queued;
    BSoundPlayer *player;
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
#ifdef USE_RING_BUFFER
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
#else
        len = read(s->pipefd, buf, bufferSize);
#endif
#ifdef PERF_CHECK
        t = system_time() - t;
        s->starve_time = MAX(s->starve_time, t);
#endif
#ifndef USE_RING_BUFFER
        if (len < B_OK) {
            puts("EPIPE");
            s->player->SetHasData(false);
            snooze(100000);
            return;
        }
        if (len == 0) {
            s->player->SetHasData(false);
            snooze(100000);
            return;
        }
#endif
        buf += len;
        bufferSize -= len;
    }
}

static int audio_open(AudioData *s, int is_output)
{
    int p[2];
    int ret;
    media_raw_audio_format format;

    if (!is_output)
        return -EIO; /* not for now */
#ifdef USE_RING_BUFFER
    s->input_sem = create_sem(AUDIO_BUFFER_SIZE, "ffmpeg_ringbuffer_input");
//    s->input_sem = create_sem(AUDIO_BLOCK_SIZE, "ffmpeg_ringbuffer_input");
    if (s->input_sem < B_OK)
        return -EIO;
    s->output_sem = create_sem(0, "ffmpeg_ringbuffer_output");
    if (s->output_sem < B_OK) {
        delete_sem(s->input_sem);
        return -EIO;
    }
    s->input_index = 0;
    s->output_index = 0;
    s->queued = 0;
#else
    ret = pipe(p);
    if (ret < 0)
        return -EIO;
    s->fd = p[is_output?1:0];
    s->pipefd = p[is_output?0:1];
    if (s->fd < 0) {
        perror(is_output?"audio out":"audio in");
        return -EIO;
    }
#endif
    create_bapp_if_needed();
    /* non blocking mode */
//    fcntl(s->fd, F_SETFL, O_NONBLOCK);
//    fcntl(s->pipefd, F_SETFL, O_NONBLOCK);
    s->frame_size = AUDIO_BLOCK_SIZE;
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
#ifdef USE_RING_BUFFER
    if (s->input_sem)
        delete_sem(s->input_sem);
    if (s->output_sem)
        delete_sem(s->output_sem);
#else
        close(s->fd);
        close(s->pipefd);
#endif
        return -EIO;
    }
    s->player->SetCookie(s);
    s->player->SetVolume(1.0);
    s->player->Start();
    s->player->SetHasData(true);
    /* bump up the priority (avoid realtime though) */
    set_thread_priority(find_thread(NULL), B_DISPLAY_PRIORITY+1);
    return 0;
}

static int audio_close(AudioData *s)
{
#ifdef USE_RING_BUFFER
    if (s->input_sem)
        delete_sem(s->input_sem);
    if (s->output_sem)
        delete_sem(s->output_sem);
#endif
    s->has_quit = 1;
    if (s->player) {
        s->player->Stop();
    }
    if (s->player)
        delete s->player;
#ifndef USE_RING_BUFFER
    close(s->pipefd);
    close(s->fd);
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
    s->sample_rate = st->codec.sample_rate;
    s->channels = st->codec.channels;
    ret = audio_open(s, 1);
    if (ret < 0)
        return -EIO;
    return 0;
}

static int audio_write_packet(AVFormatContext *s1, int stream_index,
                              uint8_t *buf, int size, int force_pts)
{
    AudioData *s = (AudioData *)s1->priv_data;
    int len, ret;
#ifdef PERF_CHECK
    bigtime_t t = s->starve_time;
    s->starve_time = 0;
    printf("starve_time: %lld    \n", t);
#endif
#ifdef USE_RING_BUFFER
    while (size > 0) {
        int amount;
        len = MIN(size, AUDIO_BLOCK_SIZE);
        if (acquire_sem_etc(s->input_sem, len, B_CAN_INTERRUPT, 0LL) < B_OK)
            return -EIO;
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
#else
    while (size > 0) {
        len = AUDIO_BLOCK_SIZE - s->buffer_ptr;
        if (len > size)
            len = size;
        memcpy(s->buffer + s->buffer_ptr, buf, len);
        s->buffer_ptr += len;
        if (s->buffer_ptr >= AUDIO_BLOCK_SIZE) {
            for(;;) {
//snooze(1000);
                ret = write(s->fd, s->buffer, AUDIO_BLOCK_SIZE);
                if (ret != 0)
                    break;
                if (ret < 0 && (errno != EAGAIN && errno != EINTR))
                    return -EIO;
            }
            s->buffer_ptr = 0;
        }
        buf += len;
        size -= len;
    }
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
        return -ENOMEM;
    }
    s->sample_rate = ap->sample_rate;
    s->channels = ap->channels;

    ret = audio_open(s, 0);
    if (ret < 0) {
        av_free(st);
        return -EIO;
    } else {
        /* take real parameters */
        st->codec.codec_type = CODEC_TYPE_AUDIO;
        st->codec.codec_id = s->codec_id;
        st->codec.sample_rate = s->sample_rate;
        st->codec.channels = s->channels;
        return 0;
    }
}

static int audio_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    AudioData *s = (AudioData *)s1->priv_data;
    int ret;

    if (av_new_packet(pkt, s->frame_size) < 0)
        return -EIO;
    for(;;) {
        ret = read(s->fd, pkt->data, pkt->size);
        if (ret > 0)
            break;
        if (ret == -1 && (errno == EAGAIN || errno == EINTR)) {
            av_free_packet(pkt);
            pkt->size = 0;
            return 0;
        }
        if (!(ret == 0 || (ret == -1 && (errno == EAGAIN || errno == EINTR)))) {
            av_free_packet(pkt);
            return -EIO;
        }
    }
    pkt->size = ret;
    if (s->flip_left && s->channels == 2) {
        int i;
        short *p = (short *) pkt->data;

        for (i = 0; i < ret; i += 4) {
            *p = ~*p;
            p += 2;
        }
    }
    return 0;
}

static int audio_read_close(AVFormatContext *s1)
{
    AudioData *s = (AudioData *)s1->priv_data;

    audio_close(s);
    return 0;
}

AVInputFormat audio_in_format = {
    "audio_device",
    "audio grab and output",
    sizeof(AudioData),
    NULL,
    audio_read_header,
    audio_read_packet,
    audio_read_close,
    NULL,
    AVFMT_NOFILE,
};

AVOutputFormat audio_out_format = {
    "audio_device",
    "audio grab and output",
    "",
    "",
    sizeof(AudioData),
#ifdef WORDS_BIGENDIAN
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
    av_register_input_format(&audio_in_format);
    av_register_output_format(&audio_out_format);
    return 0;
}

} // "C"

