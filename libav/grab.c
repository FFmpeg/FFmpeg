/*
 * Linux video grab interface
 * Copyright (c) 2000,2001 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include "avformat.h"
#include <linux/videodev.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>


typedef struct {
    int fd;
    int frame_format; /* see VIDEO_PALETTE_xxx */
    int use_mmap;
    int width, height;
    float rate;
    INT64 time_frame;
} VideoData;

const char *v4l_device = "/dev/video";

/* XXX: move all that to the context */

static struct video_capability  video_cap;
static UINT8 *video_buf;
static struct video_mbuf gb_buffers;
static struct video_mmap gb_buf;
static struct video_audio audio, audio_saved;
static int gb_frame = 0;

static int v4l_init(URLContext *h)
{
    VideoData *s = h->priv_data;
    int width, height;
    int ret;
    int video_fd, frame_size;
    
    width = s->width;
    height = s->height;

    video_fd = open(v4l_device, O_RDWR);
    if (video_fd < 0) {
        perror(v4l_device);
        return -EIO;
    }
    
    if (ioctl(video_fd,VIDIOCGCAP,&video_cap) < 0) {
        perror("VIDIOCGCAP");
        goto fail;
    }

    if (!(video_cap.type & VID_TYPE_CAPTURE)) {
        fprintf(stderr, "Fatal: grab device does not handle capture\n");
        goto fail;
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
        gb_buf.frame = (gb_frame + 1) % gb_buffers.frames;
        gb_buf.height = height;
        gb_buf.width = width;
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
        break;
    case VIDEO_PALETTE_YUV422:
        frame_size = width * height * 2;
        break;
    case VIDEO_PALETTE_RGB24:
        frame_size = width * height * 3;
        break;
    default:
        goto fail;
    }
    s->fd = video_fd;
    h->packet_size = frame_size;
    return 0;
 fail:
    close(video_fd);
    return -EIO;
}

static int v4l_mm_read_picture(URLContext *h, UINT8 *buf)
{
    VideoData *s = h->priv_data;
    UINT8 *ptr;

    gb_buf.frame = gb_frame;
    if (ioctl(s->fd, VIDIOCMCAPTURE, &gb_buf) < 0) {
	if (errno == EAGAIN)
	    fprintf(stderr,"Cannot Sync\n");
	else
            perror("VIDIOCMCAPTURE");
	return -EIO;
    }
    gb_frame = (gb_frame + 1) % gb_buffers.frames;

    while (ioctl(s->fd, VIDIOCSYNC, &gb_frame) < 0 &&
           (errno == EAGAIN || errno == EINTR));

    ptr = video_buf + gb_buffers.offsets[gb_frame];
    memcpy(buf, ptr, h->packet_size);
    return h->packet_size;
}

/* note: we support only one picture read at a time */
static int video_read(URLContext *h, UINT8 *buf, int size)
{
    VideoData *s = h->priv_data;
    INT64 curtime;

    if (size != h->packet_size)
        return -EINVAL;

    /* wait based on the frame rate */
    s->time_frame += (int)(1000000 / s->rate);
    do {
        curtime = gettime();
    } while (curtime < s->time_frame);

    /* read one frame */
    if (s->use_mmap) {
        return v4l_mm_read_picture(h, buf);
    } else {
        if (read(s->fd, buf, size) != size)
            return -EIO;
        return h->packet_size;
    }
}

static int video_get_format(URLContext *h, URLFormat *f)
{
    VideoData *s = h->priv_data;

    f->width = s->width;
    f->height = s->height;
    f->frame_rate = (int)(s->rate * FRAME_RATE_BASE);
    strcpy(f->format_name, "rawvideo");

    switch(s->frame_format) {
    case VIDEO_PALETTE_YUV420P:
        f->pix_fmt = PIX_FMT_YUV420P;
        break;
    case VIDEO_PALETTE_YUV422:
        f->pix_fmt = PIX_FMT_YUV422;
        break;
    case VIDEO_PALETTE_RGB24:
        f->pix_fmt = PIX_FMT_BGR24; /* NOTE: v4l uses BGR24, not RGB24 ! */
        break;
    default:
        abort();
    }
    return 0;
}

/* URI syntax: 'video:width,height,rate'
 */
static int video_open(URLContext *h, const char *uri, int flags)
{
    VideoData *s;
    const char *p;
    int width, height;
    int ret;
    float rate;

    /* extract parameters */
    p = uri;
    strstart(p, "video:", &p);
    width = strtol(p, (char **)&p, 0);
    if (width <= 0)
        return -EINVAL;
    if (*p == ',')
        p++;
    height = strtol(p, (char **)&p, 0);
    if (height <= 0)
        return -EINVAL;
    if (*p == ',')
        p++;
    rate = strtod(p, (char **)&p);
    if (rate <= 0)
        return -EINVAL;

    s = malloc(sizeof(VideoData));
    if (!s)
        return -ENOMEM;
    h->priv_data = s;
    h->is_streamed = 1;
    s->width = width;
    s->height = height;
    s->rate = rate;
    ret = v4l_init(h);
    if (ret)
        free(s);
    return ret;
}

static int video_close(URLContext *h)
{
    VideoData *s = h->priv_data;

    /* restore audio settings */
    ioctl(s->fd, VIDIOCSAUDIO, &audio_saved);

    close(s->fd);
    free(s);
    return 0;
}

URLProtocol video_protocol = {
    "video",
    video_open,
    video_read,
    NULL,
    NULL, /* seek */
    video_close,
    video_get_format,
};
