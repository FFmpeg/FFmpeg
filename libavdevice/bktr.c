/*
 * *BSD video grab interface
 * Copyright (c) 2002 Steve O'Hara-Smith
 * based on
 *           Linux video grab interface
 *           Copyright (c) 2000,2001 Gerard Lantau
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

#include "libavformat/internal.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#if HAVE_DEV_BKTR_IOCTL_METEOR_H && HAVE_DEV_BKTR_IOCTL_BT848_H
# include <dev/bktr/ioctl_meteor.h>
# include <dev/bktr/ioctl_bt848.h>
#elif HAVE_MACHINE_IOCTL_METEOR_H && HAVE_MACHINE_IOCTL_BT848_H
# include <machine/ioctl_meteor.h>
# include <machine/ioctl_bt848.h>
#elif HAVE_DEV_VIDEO_METEOR_IOCTL_METEOR_H && HAVE_DEV_VIDEO_BKTR_IOCTL_BT848_H
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
#include <stdint.h>
#include "avdevice.h"

typedef struct {
    AVClass *class;
    int video_fd;
    int tuner_fd;
    int width, height;
    uint64_t per_frame;
    int standard;
    char *framerate;  /**< Set by a private option. */
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
uint64_t last_frame_time;
volatile sig_atomic_t nsignals;


static void catchsignal(int signal)
{
    nsignals++;
    return;
}

static av_cold int bktr_init(const char *video_device, int width, int height,
    int format, int *video_fd, int *tuner_fd, int idev, double frequency)
{
    struct meteor_geomet geo;
    int h_max;
    long ioctl_frequency;
    char *arg;
    int c;
    struct sigaction act = { 0 }, old;

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

    sigemptyset(&act.sa_mask);
    act.sa_handler = catchsignal;
    sigaction(SIGUSR1, &act, &old);

    *tuner_fd = open("/dev/tuner0", O_RDONLY);
    if (*tuner_fd < 0)
        av_log(NULL, AV_LOG_ERROR, "Warning. Tuner not opened, continuing: %s\n", strerror(errno));

    *video_fd = open(video_device, O_RDONLY);
    if (*video_fd < 0) {
        av_log(NULL, AV_LOG_ERROR, "%s: %s\n", video_device, strerror(errno));
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
        av_log(NULL, AV_LOG_ERROR, "METEORSETGEO: %s\n", strerror(errno));
        return -1;
    }

    if (ioctl(*video_fd, BT848SFMT, &c) < 0) {
        av_log(NULL, AV_LOG_ERROR, "BT848SFMT: %s\n", strerror(errno));
        return -1;
    }

    c = bktr_dev[idev];
    if (ioctl(*video_fd, METEORSINPUT, &c) < 0) {
        av_log(NULL, AV_LOG_ERROR, "METEORSINPUT: %s\n", strerror(errno));
        return -1;
    }

    video_buf_size = width * height * 12 / 8;

    video_buf = (uint8_t *)mmap((caddr_t)0, video_buf_size,
        PROT_READ, MAP_SHARED, *video_fd, (off_t)0);
    if (video_buf == MAP_FAILED) {
        av_log(NULL, AV_LOG_ERROR, "mmap: %s\n", strerror(errno));
        return -1;
    }

    if (frequency != 0.0) {
        ioctl_frequency  = (unsigned long)(frequency*16);
        if (ioctl(*tuner_fd, TVTUNER_SETFREQ, &ioctl_frequency) < 0)
            av_log(NULL, AV_LOG_ERROR, "TVTUNER_SETFREQ: %s\n", strerror(errno));
    }

    c = AUDIO_UNMUTE;
    if (ioctl(*tuner_fd, BT848_SAUDIO, &c) < 0)
        av_log(NULL, AV_LOG_ERROR, "TVTUNER_SAUDIO: %s\n", strerror(errno));

    c = METEOR_CAP_CONTINOUS;
    ioctl(*video_fd, METEORCAPTUR, &c);

    c = SIGUSR1;
    ioctl(*video_fd, METEORSSIGNAL, &c);

    return 0;
}

static void bktr_getframe(uint64_t per_frame)
{
    uint64_t curtime;

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

static int grab_read_header(AVFormatContext *s1)
{
    VideoData *s = s1->priv_data;
    AVStream *st;
    AVRational framerate;
    int ret = 0;

    if (!s->framerate)
        switch (s->standard) {
        case PAL:   s->framerate = av_strdup("pal");  break;
        case NTSC:  s->framerate = av_strdup("ntsc"); break;
        case SECAM: s->framerate = av_strdup("25");   break;
        default:
            av_log(s1, AV_LOG_ERROR, "Unknown standard.\n");
            ret = AVERROR(EINVAL);
            goto out;
        }
    if ((ret = av_parse_video_rate(&framerate, s->framerate)) < 0) {
        av_log(s1, AV_LOG_ERROR, "Could not parse framerate '%s'.\n", s->framerate);
        goto out;
    }

    st = avformat_new_stream(s1, NULL);
    if (!st) {
        ret = AVERROR(ENOMEM);
        goto out;
    }
    avpriv_set_pts_info(st, 64, 1, 1000000); /* 64 bits pts in use */

    s->per_frame = ((uint64_t)1000000 * framerate.den) / framerate.num;

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->pix_fmt = PIX_FMT_YUV420P;
    st->codec->codec_id = CODEC_ID_RAWVIDEO;
    st->codec->width = s->width;
    st->codec->height = s->height;
    st->codec->time_base.den = framerate.num;
    st->codec->time_base.num = framerate.den;


    if (bktr_init(s1->filename, s->width, s->height, s->standard,
                  &s->video_fd, &s->tuner_fd, -1, 0.0) < 0) {
        ret = AVERROR(EIO);
        goto out;
    }

    nsignals = 0;
    last_frame_time = 0;

out:
    return ret;
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

#define OFFSET(x) offsetof(VideoData, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "standard", "", offsetof(VideoData, standard), AV_OPT_TYPE_INT, {.dbl = VIDEO_FORMAT}, PAL, NTSCJ, AV_OPT_FLAG_DECODING_PARAM, "standard" },
    { "PAL",      "", 0, AV_OPT_TYPE_CONST, {.dbl = PAL},   0, 0, AV_OPT_FLAG_DECODING_PARAM, "standard" },
    { "NTSC",     "", 0, AV_OPT_TYPE_CONST, {.dbl = NTSC},  0, 0, AV_OPT_FLAG_DECODING_PARAM, "standard" },
    { "SECAM",    "", 0, AV_OPT_TYPE_CONST, {.dbl = SECAM}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, "standard" },
    { "PALN",     "", 0, AV_OPT_TYPE_CONST, {.dbl = PALN},  0, 0, AV_OPT_FLAG_DECODING_PARAM, "standard" },
    { "PALM",     "", 0, AV_OPT_TYPE_CONST, {.dbl = PALM},  0, 0, AV_OPT_FLAG_DECODING_PARAM, "standard" },
    { "NTSCJ",    "", 0, AV_OPT_TYPE_CONST, {.dbl = NTSCJ}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, "standard" },
    { "video_size", "A string describing frame size, such as 640x480 or hd720.", OFFSET(width), AV_OPT_TYPE_IMAGE_SIZE, {.str = "vga"}, 0, 0, DEC },
    { "framerate", "", OFFSET(framerate), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, DEC },
    { NULL },
};

static const AVClass bktr_class = {
    .class_name = "BKTR grab interface",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_bktr_demuxer = {
    .name           = "bktr",
    .long_name      = NULL_IF_CONFIG_SMALL("video grab"),
    .priv_data_size = sizeof(VideoData),
    .read_header    = grab_read_header,
    .read_packet    = grab_read_packet,
    .read_close     = grab_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &bktr_class,
};
