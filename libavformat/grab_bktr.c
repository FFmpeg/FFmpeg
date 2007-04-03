/*
 * *BSD video grab interface
 * Copyright (c) 2002 Steve O'Hara-Smith
 * based on
 *           Linux video grab interface
 *           Copyright (c) 2000,2001 Gerard Lantau.
 * and
 *           simple_grab.c Copyright (c) 1999 Roger Hardiman
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
#include "avformat.h"
#if defined (HAVE_DEV_BKTR_IOCTL_METEOR_H) && defined (HAVE_DEV_BKTR_IOCTL_BT848_H)
# include <dev/bktr/ioctl_meteor.h>
# include <dev/bktr/ioctl_bt848.h>
#elif defined (HAVE_MACHINE_IOCTL_METEOR_H) && defined (HAVE_MACHINE_IOCTL_BT848_H)
# include <machine/ioctl_meteor.h>
# include <machine/ioctl_bt848.h>
#elif defined (HAVE_DEV_VIDEO_METEOR_IOCTL_METEOR_H) && defined (HAVE_DEV_VIDEO_METEOR_IOCTL_BT848_H)
# include <dev/video/meteor/ioctl_meteor.h>
# include <dev/video/bktr/ioctl_bt848.h>
#elif HAVE_DEV_IC_BT8XX_H
# include <dev/ic/bt8xx.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <signal.h>

typedef struct {
    int video_fd;
    int tuner_fd;
    int width, height;
    int frame_rate;
    int frame_rate_base;
    u_int64_t per_frame;
} VideoData;


#define PAL 1
#define PALBDGHI 1
#define NTSC 2
#define NTSCM 2
#define SECAM 3
#define PALN 4
#define PALM 5
#define NTSCJ 6

/* PAL is 768 x 576. NTSC is 640 x 480 */
#define PAL_HEIGHT 576
#define SECAM_HEIGHT 576
#define NTSC_HEIGHT 480

#ifndef VIDEO_FORMAT
#define VIDEO_FORMAT NTSC
#endif

static int bktr_dev[] = { METEOR_DEV0, METEOR_DEV1, METEOR_DEV2,
    METEOR_DEV3, METEOR_DEV_SVIDEO };

uint8_t *video_buf;
size_t video_buf_size;
u_int64_t last_frame_time;
volatile sig_atomic_t nsignals;


static void catchsignal(int signal)
{
    nsignals++;
    return;
}

static int bktr_init(const char *video_device, int width, int height,
    int format, int *video_fd, int *tuner_fd, int idev, double frequency)
{
    struct meteor_geomet geo;
    int h_max;
    long ioctl_frequency;
    char *arg;
    int c;
    struct sigaction act, old;

    if (idev < 0 || idev > 4)
    {
        arg = getenv ("BKTR_DEV");
        if (arg)
            idev = atoi (arg);
        if (idev < 0 || idev > 4)
            idev = 1;
    }

    if (format < 1 || format > 6)
    {
        arg = getenv ("BKTR_FORMAT");
        if (arg)
            format = atoi (arg);
        if (format < 1 || format > 6)
            format = VIDEO_FORMAT;
    }

    if (frequency <= 0)
    {
        arg = getenv ("BKTR_FREQUENCY");
        if (arg)
            frequency = atof (arg);
        if (frequency <= 0)
            frequency = 0.0;
    }

    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_handler = catchsignal;
    sigaction(SIGUSR1, &act, &old);

    *tuner_fd = open("/dev/tuner0", O_RDONLY);
    if (*tuner_fd < 0)
        perror("Warning: Tuner not opened, continuing");

    *video_fd = open(video_device, O_RDONLY);
    if (*video_fd < 0) {
        perror(video_device);
        return -1;
    }

    geo.rows = height;
    geo.columns = width;
    geo.frames = 1;
    geo.oformat = METEOR_GEO_YUV_422 | METEOR_GEO_YUV_12;

    switch (format) {
    case PAL:   h_max = PAL_HEIGHT;   c = BT848_IFORM_F_PALBDGHI; break;
    case PALN:  h_max = PAL_HEIGHT;   c = BT848_IFORM_F_PALN;     break;
    case PALM:  h_max = PAL_HEIGHT;   c = BT848_IFORM_F_PALM;     break;
    case SECAM: h_max = SECAM_HEIGHT; c = BT848_IFORM_F_SECAM;    break;
    case NTSC:  h_max = NTSC_HEIGHT;  c = BT848_IFORM_F_NTSCM;    break;
    case NTSCJ: h_max = NTSC_HEIGHT;  c = BT848_IFORM_F_NTSCJ;    break;
    default:    h_max = PAL_HEIGHT;   c = BT848_IFORM_F_PALBDGHI; break;
    }

    if (height <= h_max / 2)
        geo.oformat |= METEOR_GEO_EVEN_ONLY;

    if (ioctl(*video_fd, METEORSETGEO, &geo) < 0) {
        perror("METEORSETGEO");
        return -1;
    }

    if (ioctl(*video_fd, BT848SFMT, &c) < 0) {
        perror("BT848SFMT");
        return -1;
    }

    c = bktr_dev[idev];
    if (ioctl(*video_fd, METEORSINPUT, &c) < 0) {
        perror("METEORSINPUT");
        return -1;
    }

    video_buf_size = width * height * 12 / 8;

    video_buf = (uint8_t *)mmap((caddr_t)0, video_buf_size,
        PROT_READ, MAP_SHARED, *video_fd, (off_t)0);
    if (video_buf == MAP_FAILED) {
        perror("mmap");
        return -1;
    }

    if (frequency != 0.0) {
        ioctl_frequency  = (unsigned long)(frequency*16);
        if (ioctl(*tuner_fd, TVTUNER_SETFREQ, &ioctl_frequency) < 0)
            perror("TVTUNER_SETFREQ");
    }

    c = AUDIO_UNMUTE;
    if (ioctl(*tuner_fd, BT848_SAUDIO, &c) < 0)
        perror("TVTUNER_SAUDIO");

    c = METEOR_CAP_CONTINOUS;
    ioctl(*video_fd, METEORCAPTUR, &c);

    c = SIGUSR1;
    ioctl(*video_fd, METEORSSIGNAL, &c);

    return 0;
}

static void bktr_getframe(u_int64_t per_frame)
{
    u_int64_t curtime;

    curtime = av_gettime();
    if (!last_frame_time
        || ((last_frame_time + per_frame) > curtime)) {
        if (!usleep(last_frame_time + per_frame + per_frame / 8 - curtime)) {
            if (!nsignals)
                av_log(NULL, AV_LOG_INFO,
                       "SLEPT NO signals - %d microseconds late\n",
                       (int)(av_gettime() - last_frame_time - per_frame));
        }
    }
    nsignals = 0;
    last_frame_time = curtime;
}


/* note: we support only one picture read at a time */
static int grab_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    VideoData *s = s1->priv_data;

    if (av_new_packet(pkt, video_buf_size) < 0)
        return AVERROR(EIO);

    bktr_getframe(s->per_frame);

    pkt->pts = av_gettime();
    memcpy(pkt->data, video_buf, video_buf_size);

    return video_buf_size;
}

static int grab_read_header(AVFormatContext *s1, AVFormatParameters *ap)
{
    VideoData *s = s1->priv_data;
    AVStream *st;
    int width, height;
    int frame_rate;
    int frame_rate_base;
    int format = -1;

    if (ap->width <= 0 || ap->height <= 0 || ap->time_base.den <= 0)
        return -1;

    width = ap->width;
    height = ap->height;
    frame_rate = ap->time_base.den;
    frame_rate_base = ap->time_base.num;

    st = av_new_stream(s1, 0);
    if (!st)
        return AVERROR(ENOMEM);
    av_set_pts_info(st, 64, 1, 1000000); /* 64 bits pts in use */

    s->width = width;
    s->height = height;
    s->frame_rate = frame_rate;
    s->frame_rate_base = frame_rate_base;
    s->per_frame = ((u_int64_t)1000000 * s->frame_rate_base) / s->frame_rate;

    st->codec->codec_type = CODEC_TYPE_VIDEO;
    st->codec->pix_fmt = PIX_FMT_YUV420P;
    st->codec->codec_id = CODEC_ID_RAWVIDEO;
    st->codec->width = width;
    st->codec->height = height;
    st->codec->time_base.den = frame_rate;
    st->codec->time_base.num = frame_rate_base;

    if (ap->standard) {
        if (!strcasecmp(ap->standard, "pal"))
            format = PAL;
        else if (!strcasecmp(ap->standard, "secam"))
            format = SECAM;
        else if (!strcasecmp(ap->standard, "ntsc"))
            format = NTSC;
    }

    if (bktr_init(s1->filename, width, height, format,
            &(s->video_fd), &(s->tuner_fd), -1, 0.0) < 0)
        return AVERROR(EIO);

    nsignals = 0;
    last_frame_time = 0;

    return 0;
}

static int grab_read_close(AVFormatContext *s1)
{
    VideoData *s = s1->priv_data;
    int c;

    c = METEOR_CAP_STOP_CONT;
    ioctl(s->video_fd, METEORCAPTUR, &c);
    close(s->video_fd);

    c = AUDIO_MUTE;
    ioctl(s->tuner_fd, BT848_SAUDIO, &c);
    close(s->tuner_fd);

    munmap((caddr_t)video_buf, video_buf_size);

    return 0;
}

AVInputFormat video_grab_bktr_demuxer = {
    "bktr",
    "video grab",
    sizeof(VideoData),
    NULL,
    grab_read_header,
    grab_read_packet,
    grab_read_close,
    .flags = AVFMT_NOFILE,
};
