/*
 * Linux audio/video grab interface
 * Copyright (c) 2000 Gerard Lantau.
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
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <linux/videodev.h>
#include <linux/soundcard.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/time.h>
#include <getopt.h>

#include "mpegenc.h"
#include "mpegvideo.h"

long long gettime(void)
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

/* v4l capture */

const char *v4l_device = "/dev/video";

static struct video_capability  video_cap;
int video_fd = -1;
UINT8 *video_buf, *picture_buf;
struct video_mbuf gb_buffers;
struct video_mmap gb_buf;
struct video_audio audio;
int gb_frame = 0;
long long time_frame;
int frame_rate;
int use_mmap = 0;

int v4l_init(int rate, int width, int height)
{
    frame_rate = rate;

    video_fd = open(v4l_device, O_RDWR);
    
    if (ioctl(video_fd,VIDIOCGCAP,&video_cap) < 0) {
        perror("VIDIOCGCAP");
        return -1;
    }
    
    /* unmute audio */
    ioctl(video_fd, VIDIOCGAUDIO, &audio);
    audio.flags &= ~VIDEO_AUDIO_MUTE;
    ioctl(video_fd, VIDIOCSAUDIO, &audio);

    if (!(video_cap.type & VID_TYPE_CAPTURE)) {
        /* try to use read based access */
        struct video_window win;
        int val;

        win.x = 0;
        win.y = 0;
        win.width = width;
        win.height = height;
        win.chromakey = -1;
        win.flags = 0;

        ioctl(video_fd, VIDIOCSWIN, &win);

        val = 1;
        ioctl(video_fd, VIDIOCCAPTURE, &val);
        video_buf = malloc( width * height * 2);
        picture_buf = malloc( (width * height * 3) / 2);
        use_mmap = 0;
        return 0;
    }
    
    if (ioctl(video_fd,VIDIOCGMBUF,&gb_buffers) < 0) {
        perror("ioctl VIDIOCGMBUF");
    }
    
    video_buf = mmap(0,gb_buffers.size,PROT_READ|PROT_WRITE,MAP_SHARED,video_fd,0);
    if ((unsigned char*)-1 == video_buf) {
        perror("mmap");
        return -1;
    }
    gb_frame = 0;
    time_frame = gettime();
    
    /* start to grab the first frame */
    gb_buf.frame = 1 - gb_frame;
    gb_buf.height = height;
    gb_buf.width = width;
    gb_buf.format = VIDEO_PALETTE_YUV420P;
    
    if (ioctl(video_fd, VIDIOCMCAPTURE, &gb_buf) < 0) {
        if (errno == EAGAIN)
            fprintf(stderr,"Cannot Sync\n");
        else
            perror("VIDIOCMCAPTURE");
        return -1;
    }
    use_mmap = 1;
    return 0;
}

/* test with read call and YUV422 stream */
static int v4l_basic_read_picture(UINT8 *picture[3],
                                  int width, int height,
                                  int picture_number)
{
    int x, y;
    UINT8 *p, *lum, *cb, *cr;
    
    if (read(video_fd, video_buf, width * height * 2) < 0)
        perror("read");

    picture[0] = picture_buf;
    picture[1] = picture_buf + width * height;
    picture[2] = picture_buf + (width * height) + (width * height) / 4;
    
    /* XXX: optimize */
    lum = picture[0];
    cb = picture[1];
    cr = picture[2];
    p = video_buf;
    for(y=0;y<height;y+=2) {
        for(x=0;x<width;x+=2) {
            lum[0] = p[0];
            cb[0] = p[1];
            lum[1] = p[2];
            cr[0] = p[3];
            p += 4;
            lum += 2;
            cb++;
            cr++;
        }
        for(x=0;x<width;x+=2) {
            lum[0] = p[0];
            lum[1] = p[2];
            p += 4;
            lum += 2;
        }
    }
    return 0;
}

static int v4l_mm_read_picture(UINT8 *picture[3],
                               int width, int height,
                               int picture_number)
{
    UINT8 *ptr;
    int size;
    long long curtime;

    /* wait based on the frame rate */
    time_frame += 1000000 / frame_rate;
    do {
        curtime = gettime();
    } while (curtime < time_frame);
    
    gb_buf.frame = gb_frame;
    if (ioctl(video_fd, VIDIOCMCAPTURE, &gb_buf) < 0) {
	if (errno == EAGAIN)
	    fprintf(stderr,"Cannot Sync\n");
	else
            perror("VIDIOCMCAPTURE");
	return -1;
    }
    gb_frame = 1 - gb_frame;

    if (ioctl(video_fd, VIDIOCSYNC, &gb_frame) < 0) {
        if (errno != EAGAIN) {
            perror("VIDIOCSYNC");
        }
    }

    size = width * height;
    ptr = video_buf + gb_buffers.offsets[gb_frame];
    picture[0] = ptr;
    picture[1] = ptr + size;
    picture[2] = ptr + size + (size / 4);
    
    return 0;
}

int v4l_read_picture(UINT8 *picture[3],
                     int width, int height,
                     int picture_number)
{
    if (use_mmap) {
        return v4l_mm_read_picture(picture, width, height, picture_number);
    } else {
        return v4l_basic_read_picture(picture, width, height, picture_number);
    }
}

/* open audio device */
int audio_open(int freq, int channels)
{
    int audio_fd, tmp, err;

    audio_fd = open("/dev/dsp",O_RDONLY);
    if (audio_fd == -1) {
        perror("/dev/dsp");
        return -1;
    }
    /* non blocking mode */
    fcntl(audio_fd, F_SETFL, O_NONBLOCK);

#if 0
    tmp=(NB_FRAGMENTS << 16) | FRAGMENT_BITS;
    err=ioctl(audio_fd, SNDCTL_DSP_SETFRAGMENT, &tmp);
    if (err < 0) {
        perror("SNDCTL_DSP_SETFRAGMENT");
    }
#endif

    /* always set to this size */
    /* XXX: incorrect if big endian */
    tmp=AFMT_S16_LE;
    err=ioctl(audio_fd,SNDCTL_DSP_SETFMT,&tmp);
    if (err < 0) {
        perror("SNDCTL_DSP_SETFMT");
    }
    
    tmp= (channels == 2);
    err=ioctl(audio_fd,SNDCTL_DSP_STEREO,&tmp);
    if (err < 0) {
        perror("SNDCTL_DSP_STEREO");
    }
    
    /* should be last */
    tmp = freq;
    err=ioctl(audio_fd, SNDCTL_DSP_SPEED, &tmp);
    if (err < 0) {
        perror("SNDCTL_DSP_SPEED");
    }
    return audio_fd;
}

