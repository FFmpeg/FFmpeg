/*
 * Linux video grab interface
 * Copyright (c) 2000,2001 Fabrice Bellard.
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
#include "avformat.h"
#include <linux/videodev.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>

typedef struct {
    int fd;
    int frame_format; /* see VIDEO_PALETTE_xxx */
    int use_mmap;
    int width, height;
    int frame_rate;
    INT64 time_frame;
    int frame_size;
} VideoData;

const char *v4l_device = "/dev/video";

/* XXX: move all that to the context */

static struct video_capability  video_cap;
static UINT8 *video_buf;
static struct video_mbuf gb_buffers;
static struct video_mmap gb_buf;
static struct video_audio audio, audio_saved;
static int gb_frame = 0;

static int grab_read_header(AVFormatContext *s1, AVFormatParameters *ap)
{
    VideoData *s = s1->priv_data;
    AVStream *st;
    int width, height;
    int video_fd, frame_size;
    int ret, frame_rate;
    int desired_palette;

    if (!ap || ap->width <= 0 || ap->height <= 0 || ap->frame_rate <= 0)
        return -1;
    
    width = ap->width;
    height = ap->height;
    frame_rate = ap->frame_rate;

    st = av_new_stream(s1, 0);
    if (!st)
        return -ENOMEM;

    s->width = width;
    s->height = height;
    s->frame_rate = frame_rate;

    video_fd = open(v4l_device, O_RDWR);
    if (video_fd < 0) {
        perror(v4l_device);
        goto fail;
    }
    
    if (ioctl(video_fd,VIDIOCGCAP,&video_cap) < 0) {
        perror("VIDIOCGCAP");
        goto fail;
    }

    if (!(video_cap.type & VID_TYPE_CAPTURE)) {
        fprintf(stderr, "Fatal: grab device does not handle capture\n");
        goto fail;
    }

    desired_palette = -1;
    if (st->codec.pix_fmt == PIX_FMT_YUV420P) {
        desired_palette = VIDEO_PALETTE_YUV420P;
    } else if (st->codec.pix_fmt == PIX_FMT_YUV422) {
        desired_palette = VIDEO_PALETTE_YUV422;
    } else if (st->codec.pix_fmt == PIX_FMT_BGR24) {
        desired_palette = VIDEO_PALETTE_RGB24;
    }    
    
    /* unmute audio */
    ioctl(video_fd, VIDIOCGAUDIO, &audio);
    memcpy(&audio_saved, &audio, sizeof(audio));
    audio.flags &= ~VIDEO_AUDIO_MUTE;
    ioctl(video_fd, VIDIOCSAUDIO, &audio);

    ret = ioctl(video_fd,VIDIOCGMBUF,&gb_buffers);
    if (ret < 0) {
        /* try to use read based access */
        struct video_window win;
        struct video_picture pict;
        int val;

        win.x = 0;
        win.y = 0;
        win.width = width;
        win.height = height;
        win.chromakey = -1;
        win.flags = 0;

        ioctl(video_fd, VIDIOCSWIN, &win);

        ioctl(video_fd, VIDIOCGPICT, &pict);
#if 0
        printf("v4l: colour=%d hue=%d brightness=%d constrast=%d whiteness=%d\n",
               pict.colour,
               pict.hue,
               pict.brightness,
               pict.contrast,
               pict.whiteness);
#endif        
        /* try to choose a suitable video format */
        pict.palette = desired_palette;
        if (desired_palette == -1 || (ret = ioctl(video_fd, VIDIOCSPICT, &pict)) < 0) {
            pict.palette=VIDEO_PALETTE_YUV420P;
            ret = ioctl(video_fd, VIDIOCSPICT, &pict);
            if (ret < 0) {
                pict.palette=VIDEO_PALETTE_YUV422;
                ret = ioctl(video_fd, VIDIOCSPICT, &pict);
                if (ret < 0) {
                    pict.palette=VIDEO_PALETTE_RGB24;
                    ret = ioctl(video_fd, VIDIOCSPICT, &pict);
                    if (ret < 0) 
                        goto fail1;
                }
            }
        }

        s->frame_format = pict.palette;

        val = 1;
        ioctl(video_fd, VIDIOCCAPTURE, &val);

        s->time_frame = gettime();
        s->use_mmap = 0;
    } else {
        video_buf = mmap(0,gb_buffers.size,PROT_READ|PROT_WRITE,MAP_SHARED,video_fd,0);
        if ((unsigned char*)-1 == video_buf) {
            perror("mmap");
            goto fail;
        }
        gb_frame = 0;
        s->time_frame = gettime();
        
        /* start to grab the first frame */
        gb_buf.frame = gb_frame % gb_buffers.frames;
        gb_buf.height = height;
        gb_buf.width = width;
        gb_buf.format = desired_palette;

        if (desired_palette == -1 || (ret = ioctl(video_fd, VIDIOCMCAPTURE, &gb_buf)) < 0) {
            gb_buf.format = VIDEO_PALETTE_YUV420P;
            
            ret = ioctl(video_fd, VIDIOCMCAPTURE, &gb_buf);
            if (ret < 0 && errno != EAGAIN) {
                /* try YUV422 */
                gb_buf.format = VIDEO_PALETTE_YUV422;
                
                ret = ioctl(video_fd, VIDIOCMCAPTURE, &gb_buf);
                if (ret < 0 && errno != EAGAIN) {
                    /* try RGB24 */
                    gb_buf.format = VIDEO_PALETTE_RGB24;
                    ret = ioctl(video_fd, VIDIOCMCAPTURE, &gb_buf);
                }
            }
        }
        if (ret < 0) {
            if (errno != EAGAIN) {
            fail1:
                fprintf(stderr, "Fatal: grab device does not support suitable format\n");
            } else {
                fprintf(stderr,"Fatal: grab device does not receive any video signal\n");
            }
            goto fail;
        }
        s->frame_format = gb_buf.format;
        s->use_mmap = 1;
    }

    switch(s->frame_format) {
    case VIDEO_PALETTE_YUV420P:
        frame_size = (width * height * 3) / 2;
        st->codec.pix_fmt = PIX_FMT_YUV420P;
        break;
    case VIDEO_PALETTE_YUV422:
        frame_size = width * height * 2;
        st->codec.pix_fmt = PIX_FMT_YUV422;
        break;
    case VIDEO_PALETTE_RGB24:
        frame_size = width * height * 3;
        st->codec.pix_fmt = PIX_FMT_BGR24; /* NOTE: v4l uses BGR24, not RGB24 ! */
        break;
    default:
        goto fail;
    }
    s->fd = video_fd;
    s->frame_size = frame_size;
    
    st->codec.codec_type = CODEC_TYPE_VIDEO;
    st->codec.codec_id = CODEC_ID_RAWVIDEO;
    st->codec.width = width;
    st->codec.height = height;
    st->codec.frame_rate = frame_rate;

    return 0;
 fail:
    if (video_fd >= 0)
        close(video_fd);
    av_free(st);
    return -EIO;
}

static int v4l_mm_read_picture(VideoData *s, UINT8 *buf)
{
    UINT8 *ptr;
    struct timeval tv_s;
    //struct timeval tv_e;
    //int delay;

    /* Setup to capture the next frame */
    gb_buf.frame = (gb_frame + 1) % gb_buffers.frames;
    if (ioctl(s->fd, VIDIOCMCAPTURE, &gb_buf) < 0) {
        if (errno == EAGAIN)
            fprintf(stderr,"Cannot Sync\n");
        else
            perror("VIDIOCMCAPTURE");
        return -EIO;
    }

    gettimeofday(&tv_s, 0);

    while (ioctl(s->fd, VIDIOCSYNC, &gb_frame) < 0 &&
           (errno == EAGAIN || errno == EINTR));

    /*
    gettimeofday(&tv_e, 0);

    delay = (tv_e.tv_sec - tv_s.tv_sec) * 1000000 + tv_e.tv_usec - tv_s.tv_usec;
    if (delay > 10000) 
        printf("VIDIOCSYNC took %d us\n", delay);
    */

    ptr = video_buf + gb_buffers.offsets[gb_frame];
    memcpy(buf, ptr, s->frame_size);

    /* This is now the grabbing frame */
    gb_frame = gb_buf.frame;

    return s->frame_size;
}

static int grab_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    VideoData *s = s1->priv_data;
    INT64 curtime, delay;
    struct timespec ts;
    int first;
    INT64 per_frame = (INT64_C(1000000) * FRAME_RATE_BASE) / s->frame_rate;
    int dropped = 0;

    /* Calculate the time of the next frame */
    s->time_frame += per_frame;

    /* wait based on the frame rate */
    for(first = 1;; first = 0) {
        curtime = gettime();
        delay = s->time_frame - curtime;
        if (delay <= 0) {
            if (delay < -per_frame) {
                /* printf("grabbing is %d frames late (dropping)\n", (int) -(delay / 16666)); */
                dropped = 1;
                s->time_frame += per_frame;
            }
            break;
        }    
        ts.tv_sec = delay / 1000000;
        ts.tv_nsec = (delay % 1000000) * 1000;
        nanosleep(&ts, NULL);
    }

    if (av_new_packet(pkt, s->frame_size) < 0)
        return -EIO;

    if (dropped)
        pkt->flags |= PKT_FLAG_DROPPED_FRAME;

    /* read one frame */
    if (s->use_mmap) {
        return v4l_mm_read_picture(s, pkt->data);
    } else {
        if (read(s->fd, pkt->data, pkt->size) != pkt->size)
            return -EIO;
        return s->frame_size;
    }
}

static int grab_read_close(AVFormatContext *s1)
{
    VideoData *s = s1->priv_data;
    /* restore audio settings */
    ioctl(s->fd, VIDIOCSAUDIO, &audio_saved);

    close(s->fd);
    return 0;
}

AVInputFormat video_grab_device_format = {
    "video_grab_device",
    "video grab",
    sizeof(VideoData),
    NULL,
    grab_read_header,
    grab_read_packet,
    grab_read_close,
    flags: AVFMT_NOFILE,
};

int video_grab_init(void)
{
    av_register_input_format(&video_grab_device_format);
    return 0;
}
