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
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#define _LINUX_TIME_H 1
#include <linux/videodev.h>
#include <time.h>

typedef struct {
    int fd;
    int frame_format; /* see VIDEO_PALETTE_xxx */
    int use_mmap;
    int width, height;
    int frame_rate;
    int frame_rate_base;
    int64_t time_frame;
    int frame_size;
    struct video_capability video_cap;
    struct video_audio audio_saved;
    uint8_t *video_buf;
    struct video_mbuf gb_buffers;
    struct video_mmap gb_buf;
    int gb_frame;

    /* ATI All In Wonder specific stuff */
    /* XXX: remove and merge in libavcodec/imgconvert.c */
    int aiw_enabled;
    int deint;
    int halfw;
    uint8_t *src_mem;
    uint8_t *lum_m4_mem;
} VideoData;

static int aiw_init(VideoData *s);
static int aiw_read_picture(VideoData *s, uint8_t *data);
static int aiw_close(VideoData *s);

static int grab_read_header(AVFormatContext *s1, AVFormatParameters *ap)
{
    VideoData *s = s1->priv_data;
    AVStream *st;
    int width, height;
    int video_fd, frame_size;
    int ret, frame_rate, frame_rate_base;
    int desired_palette;
    struct video_tuner tuner;
    struct video_audio audio;
    const char *video_device;
    int j;

    if (!ap || ap->width <= 0 || ap->height <= 0 || ap->frame_rate <= 0)
        return -1;
    
    width = ap->width;
    height = ap->height;
    frame_rate      = ap->frame_rate;
    frame_rate_base = ap->frame_rate_base;

    st = av_new_stream(s1, 0);
    if (!st)
        return -ENOMEM;
    av_set_pts_info(st, 48, 1, 1000000); /* 48 bits pts in us */

    s->width = width;
    s->height = height;
    s->frame_rate      = frame_rate;
    s->frame_rate_base = frame_rate_base;

    video_device = ap->device;
    if (!video_device)
        video_device = "/dev/video";
    video_fd = open(video_device, O_RDWR);
    if (video_fd < 0) {
        perror(video_device);
        goto fail;
    }
    
    if (ioctl(video_fd,VIDIOCGCAP, &s->video_cap) < 0) {
        perror("VIDIOCGCAP");
        goto fail;
    }

    if (!(s->video_cap.type & VID_TYPE_CAPTURE)) {
	av_log(s1, AV_LOG_ERROR, "Fatal: grab device does not handle capture\n");
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

    /* set tv standard */
    if (ap->standard && !ioctl(video_fd, VIDIOCGTUNER, &tuner)) {
	if (!strcasecmp(ap->standard, "pal"))
	    tuner.mode = VIDEO_MODE_PAL;
	else if (!strcasecmp(ap->standard, "secam"))
	    tuner.mode = VIDEO_MODE_SECAM;
	else
	    tuner.mode = VIDEO_MODE_NTSC;
	ioctl(video_fd, VIDIOCSTUNER, &tuner);
    }
    
    /* unmute audio */
    audio.audio = 0;
    ioctl(video_fd, VIDIOCGAUDIO, &audio);
    memcpy(&s->audio_saved, &audio, sizeof(audio));
    audio.flags &= ~VIDEO_AUDIO_MUTE;
    ioctl(video_fd, VIDIOCSAUDIO, &audio);

    ret = ioctl(video_fd,VIDIOCGMBUF,&s->gb_buffers);
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

        s->time_frame = av_gettime() * s->frame_rate / s->frame_rate_base;
        s->use_mmap = 0;
        
        /* ATI All In Wonder automatic activation */
        if (!strcmp(s->video_cap.name, "Km")) {
            if (aiw_init(s) < 0)
                goto fail;
            s->aiw_enabled = 1;
            /* force 420P format because convertion from YUV422 to YUV420P
               is done in this driver (ugly) */
            s->frame_format = VIDEO_PALETTE_YUV420P;
        }
    } else {
        s->video_buf = mmap(0,s->gb_buffers.size,PROT_READ|PROT_WRITE,MAP_SHARED,video_fd,0);
        if ((unsigned char*)-1 == s->video_buf) {
            perror("mmap");
            goto fail;
        }
        s->gb_frame = 0;
        s->time_frame = av_gettime() * s->frame_rate / s->frame_rate_base;
        
        /* start to grab the first frame */
        s->gb_buf.frame = s->gb_frame % s->gb_buffers.frames;
        s->gb_buf.height = height;
        s->gb_buf.width = width;
        s->gb_buf.format = desired_palette;

        if (desired_palette == -1 || (ret = ioctl(video_fd, VIDIOCMCAPTURE, &s->gb_buf)) < 0) {
            s->gb_buf.format = VIDEO_PALETTE_YUV420P;
            
            ret = ioctl(video_fd, VIDIOCMCAPTURE, &s->gb_buf);
            if (ret < 0 && errno != EAGAIN) {
                /* try YUV422 */
                s->gb_buf.format = VIDEO_PALETTE_YUV422;
                
                ret = ioctl(video_fd, VIDIOCMCAPTURE, &s->gb_buf);
                if (ret < 0 && errno != EAGAIN) {
                    /* try RGB24 */
                    s->gb_buf.format = VIDEO_PALETTE_RGB24;
                    ret = ioctl(video_fd, VIDIOCMCAPTURE, &s->gb_buf);
                }
            }
        }
        if (ret < 0) {
            if (errno != EAGAIN) {
            fail1:
                av_log(s1, AV_LOG_ERROR, "Fatal: grab device does not support suitable format\n");
            } else {
                av_log(s1, AV_LOG_ERROR,"Fatal: grab device does not receive any video signal\n");
            }
            goto fail;
        }
	for (j = 1; j < s->gb_buffers.frames; j++) {
	  s->gb_buf.frame = j;
	  ioctl(video_fd, VIDIOCMCAPTURE, &s->gb_buf);
	}
        s->frame_format = s->gb_buf.format;
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
    st->codec.frame_rate      = frame_rate;
    st->codec.frame_rate_base = frame_rate_base;

    return 0;
 fail:
    if (video_fd >= 0)
        close(video_fd);
    av_free(st);
    return AVERROR_IO;
}

static int v4l_mm_read_picture(VideoData *s, uint8_t *buf)
{
    uint8_t *ptr;

    while (ioctl(s->fd, VIDIOCSYNC, &s->gb_frame) < 0 &&
           (errno == EAGAIN || errno == EINTR));

    ptr = s->video_buf + s->gb_buffers.offsets[s->gb_frame];
    memcpy(buf, ptr, s->frame_size);

    /* Setup to capture the next frame */
    s->gb_buf.frame = s->gb_frame;
    if (ioctl(s->fd, VIDIOCMCAPTURE, &s->gb_buf) < 0) {
        if (errno == EAGAIN)
            av_log(NULL, AV_LOG_ERROR, "Cannot Sync\n");
        else
            perror("VIDIOCMCAPTURE");
        return AVERROR_IO;
    }

    /* This is now the grabbing frame */
    s->gb_frame = (s->gb_frame + 1) % s->gb_buffers.frames;

    return s->frame_size;
}

static int grab_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    VideoData *s = s1->priv_data;
    int64_t curtime, delay;
    struct timespec ts;

    /* Calculate the time of the next frame */
    s->time_frame += int64_t_C(1000000);

    /* wait based on the frame rate */
    for(;;) {
        curtime = av_gettime();
        delay = s->time_frame  * s->frame_rate_base / s->frame_rate - curtime;
        if (delay <= 0) {
            if (delay < int64_t_C(-1000000) * s->frame_rate_base / s->frame_rate) {
                /* printf("grabbing is %d frames late (dropping)\n", (int) -(delay / 16666)); */
                s->time_frame += int64_t_C(1000000);
            }
            break;
        }    
        ts.tv_sec = delay / 1000000;
        ts.tv_nsec = (delay % 1000000) * 1000;
        nanosleep(&ts, NULL);
    }

    if (av_new_packet(pkt, s->frame_size) < 0)
        return AVERROR_IO;

    pkt->pts = curtime & ((1LL << 48) - 1);

    /* read one frame */
    if (s->aiw_enabled) {
        return aiw_read_picture(s, pkt->data);
    } else if (s->use_mmap) {
        return v4l_mm_read_picture(s, pkt->data);
    } else {
        if (read(s->fd, pkt->data, pkt->size) != pkt->size)
            return AVERROR_IO;
        return s->frame_size;
    }
}

static int grab_read_close(AVFormatContext *s1)
{
    VideoData *s = s1->priv_data;

    if (s->aiw_enabled)
        aiw_close(s);

    if (s->use_mmap)
        munmap(s->video_buf, s->gb_buffers.size);

    /* mute audio. we must force it because the BTTV driver does not
       return its state correctly */
    s->audio_saved.flags |= VIDEO_AUDIO_MUTE;
    ioctl(s->fd, VIDIOCSAUDIO, &s->audio_saved);

    close(s->fd);
    return 0;
}

static AVInputFormat video_grab_device_format = {
    "video4linux",
    "video grab",
    sizeof(VideoData),
    NULL,
    grab_read_header,
    grab_read_packet,
    grab_read_close,
    .flags = AVFMT_NOFILE,
};

/* All in Wonder specific stuff */
/* XXX: remove and merge in libavcodec/imgconvert.c */

static int aiw_init(VideoData *s)
{
    int width, height;

    width = s->width;
    height = s->height;

    if ((width == s->video_cap.maxwidth && height == s->video_cap.maxheight) ||
        (width == s->video_cap.maxwidth && height == s->video_cap.maxheight*2) ||
        (width == s->video_cap.maxwidth/2 && height == s->video_cap.maxheight)) {
        
        s->deint=0;
        s->halfw=0;
        if (height == s->video_cap.maxheight*2) s->deint=1;
        if (width == s->video_cap.maxwidth/2) s->halfw=1;
    } else {
        av_log(NULL, AV_LOG_ERROR, "\nIncorrect Grab Size Supplied - Supported Sizes Are:\n");
        av_log(NULL, AV_LOG_ERROR, " %dx%d  %dx%d %dx%d\n\n",
                s->video_cap.maxwidth,s->video_cap.maxheight,
                s->video_cap.maxwidth,s->video_cap.maxheight*2,
                s->video_cap.maxwidth/2,s->video_cap.maxheight);
        goto fail;
    }

    if (s->halfw == 0) {
        s->src_mem = av_malloc(s->width*2);
    } else {
        s->src_mem = av_malloc(s->width*4);
    }
    if (!s->src_mem) goto fail;

    s->lum_m4_mem = av_malloc(s->width);
    if (!s->lum_m4_mem)
        goto fail;
    return 0;
 fail:
    av_freep(&s->src_mem);
    av_freep(&s->lum_m4_mem);
    return -1;
}

#ifdef HAVE_MMX
#include "../libavcodec/i386/mmx.h"

#define LINE_WITH_UV \
                    movq_m2r(ptr[0],mm0); \
                    movq_m2r(ptr[8],mm1);  \
                    movq_r2r(mm0, mm4); \
                    punpcklbw_r2r(mm1,mm0); \
                    punpckhbw_r2r(mm1,mm4); \
                    movq_r2r(mm0,mm5); \
                    punpcklbw_r2r(mm4,mm0); \
                    punpckhbw_r2r(mm4,mm5); \
                    movq_r2r(mm0,mm1); \
                    punpcklbw_r2r(mm5,mm1); \
                    movq_r2m(mm1,lum[0]); \
                    movq_m2r(ptr[16],mm2); \
                    movq_m2r(ptr[24],mm1); \
                    movq_r2r(mm2,mm4); \
                    punpcklbw_r2r(mm1,mm2); \
                    punpckhbw_r2r(mm1,mm4); \
                    movq_r2r(mm2,mm3); \
                    punpcklbw_r2r(mm4,mm2); \
                    punpckhbw_r2r(mm4,mm3); \
                    movq_r2r(mm2,mm1); \
                    punpcklbw_r2r(mm3,mm1); \
                    movq_r2m(mm1,lum[8]); \
                    punpckhdq_r2r(mm2,mm0); \
                    punpckhdq_r2r(mm3,mm5); \
                    movq_r2m(mm0,cb[0]); \
                    movq_r2m(mm5,cr[0]);

#define LINE_NO_UV \
                    movq_m2r(ptr[0],mm0);\
                    movq_m2r(ptr[8],mm1);\
                    movq_r2r(mm0, mm4);\
                    punpcklbw_r2r(mm1,mm0); \
                    punpckhbw_r2r(mm1,mm4);\
                    movq_r2r(mm0,mm5);\
                    punpcklbw_r2r(mm4,mm0);\
                    punpckhbw_r2r(mm4,mm5);\
                    movq_r2r(mm0,mm1);\
                    punpcklbw_r2r(mm5,mm1);\
                    movq_r2m(mm1,lum[0]);\
                    movq_m2r(ptr[16],mm2);\
                    movq_m2r(ptr[24],mm1);\
                    movq_r2r(mm2,mm4);\
                    punpcklbw_r2r(mm1,mm2);\
                    punpckhbw_r2r(mm1,mm4);\
                    movq_r2r(mm2,mm3);\
                    punpcklbw_r2r(mm4,mm2);\
                    punpckhbw_r2r(mm4,mm3);\
                    movq_r2r(mm2,mm1);\
                    punpcklbw_r2r(mm3,mm1);\
                    movq_r2m(mm1,lum[8]);

#define LINE_WITHUV_AVG \
                    movq_m2r(ptr[0], mm0);\
                    movq_m2r(ptr[8], mm1);\
                    movq_r2r(mm0, mm4);\
                    punpcklbw_r2r(mm1,mm0);\
                    punpckhbw_r2r(mm1,mm4);\
                    movq_r2r(mm0,mm5);\
                    punpcklbw_r2r(mm4,mm0);\
                    punpckhbw_r2r(mm4,mm5);\
                    movq_r2r(mm0,mm1);\
                    movq_r2r(mm5,mm2);\
                    punpcklbw_r2r(mm7,mm1);\
                    punpcklbw_r2r(mm7,mm2);\
                    paddw_r2r(mm6,mm1);\
                    paddw_r2r(mm2,mm1);\
                    psraw_i2r(1,mm1);\
                    packuswb_r2r(mm7,mm1);\
                    movd_r2m(mm1,lum[0]);\
                    movq_m2r(ptr[16],mm2);\
                    movq_m2r(ptr[24],mm1);\
                    movq_r2r(mm2,mm4);\
                    punpcklbw_r2r(mm1,mm2);\
                    punpckhbw_r2r(mm1,mm4);\
                    movq_r2r(mm2,mm3);\
                    punpcklbw_r2r(mm4,mm2);\
                    punpckhbw_r2r(mm4,mm3);\
                    movq_r2r(mm2,mm1);\
                    movq_r2r(mm3,mm4);\
                    punpcklbw_r2r(mm7,mm1);\
                    punpcklbw_r2r(mm7,mm4);\
                    paddw_r2r(mm6,mm1);\
                    paddw_r2r(mm4,mm1);\
                    psraw_i2r(1,mm1);\
                    packuswb_r2r(mm7,mm1);\
                    movd_r2m(mm1,lum[4]);\
                    punpckhbw_r2r(mm7,mm0);\
                    punpckhbw_r2r(mm7,mm2);\
                    paddw_r2r(mm6,mm0);\
                    paddw_r2r(mm2,mm0);\
                    psraw_i2r(1,mm0);\
                    packuswb_r2r(mm7,mm0);\
                    punpckhbw_r2r(mm7,mm5);\
                    punpckhbw_r2r(mm7,mm3);\
                    paddw_r2r(mm6,mm5);\
                    paddw_r2r(mm3,mm5);\
                    psraw_i2r(1,mm5);\
                    packuswb_r2r(mm7,mm5);\
                    movd_r2m(mm0,cb[0]);\
                    movd_r2m(mm5,cr[0]);

#define LINE_NOUV_AVG \
                    movq_m2r(ptr[0],mm0);\
                    movq_m2r(ptr[8],mm1);\
                    pand_r2r(mm5,mm0);\
                    pand_r2r(mm5,mm1);\
                    pmaddwd_r2r(mm6,mm0);\
                    pmaddwd_r2r(mm6,mm1);\
                    packssdw_r2r(mm1,mm0);\
                    paddw_r2r(mm6,mm0);\
                    psraw_i2r(1,mm0);\
                    movq_m2r(ptr[16],mm2);\
                    movq_m2r(ptr[24],mm3);\
                    pand_r2r(mm5,mm2);\
                    pand_r2r(mm5,mm3);\
                    pmaddwd_r2r(mm6,mm2);\
                    pmaddwd_r2r(mm6,mm3);\
                    packssdw_r2r(mm3,mm2);\
                    paddw_r2r(mm6,mm2);\
                    psraw_i2r(1,mm2);\
                    packuswb_r2r(mm2,mm0);\
                    movq_r2m(mm0,lum[0]);

#define DEINT_LINE_LUM(ptroff) \
                    movd_m2r(lum_m4[(ptroff)],mm0);\
                    movd_m2r(lum_m3[(ptroff)],mm1);\
                    movd_m2r(lum_m2[(ptroff)],mm2);\
                    movd_m2r(lum_m1[(ptroff)],mm3);\
                    movd_m2r(lum[(ptroff)],mm4);\
                    punpcklbw_r2r(mm7,mm0);\
                    movd_r2m(mm2,lum_m4[(ptroff)]);\
                    punpcklbw_r2r(mm7,mm1);\
                    punpcklbw_r2r(mm7,mm2);\
                    punpcklbw_r2r(mm7,mm3);\
                    punpcklbw_r2r(mm7,mm4);\
                    psllw_i2r(2,mm1);\
                    psllw_i2r(1,mm2);\
                    paddw_r2r(mm6,mm1);\
                    psllw_i2r(2,mm3);\
                    paddw_r2r(mm2,mm1);\
                    paddw_r2r(mm4,mm0);\
                    paddw_r2r(mm3,mm1);\
                    psubusw_r2r(mm0,mm1);\
                    psrlw_i2r(3,mm1);\
                    packuswb_r2r(mm7,mm1);\
                    movd_r2m(mm1,lum_m2[(ptroff)]);

#else
#include "../libavcodec/dsputil.h"

#define LINE_WITH_UV \
                    lum[0]=ptr[0];lum[1]=ptr[2];lum[2]=ptr[4];lum[3]=ptr[6];\
                    cb[0]=ptr[1];cb[1]=ptr[5];\
                    cr[0]=ptr[3];cr[1]=ptr[7];\
                    lum[4]=ptr[8];lum[5]=ptr[10];lum[6]=ptr[12];lum[7]=ptr[14];\
                    cb[2]=ptr[9];cb[3]=ptr[13];\
                    cr[2]=ptr[11];cr[3]=ptr[15];\
                    lum[8]=ptr[16];lum[9]=ptr[18];lum[10]=ptr[20];lum[11]=ptr[22];\
                    cb[4]=ptr[17];cb[5]=ptr[21];\
                    cr[4]=ptr[19];cr[5]=ptr[23];\
                    lum[12]=ptr[24];lum[13]=ptr[26];lum[14]=ptr[28];lum[15]=ptr[30];\
                    cb[6]=ptr[25];cb[7]=ptr[29];\
                    cr[6]=ptr[27];cr[7]=ptr[31];

#define LINE_NO_UV \
                    lum[0]=ptr[0];lum[1]=ptr[2];lum[2]=ptr[4];lum[3]=ptr[6];\
                    lum[4]=ptr[8];lum[5]=ptr[10];lum[6]=ptr[12];lum[7]=ptr[14];\
                    lum[8]=ptr[16];lum[9]=ptr[18];lum[10]=ptr[20];lum[11]=ptr[22];\
                    lum[12]=ptr[24];lum[13]=ptr[26];lum[14]=ptr[28];lum[15]=ptr[30];

#define LINE_WITHUV_AVG \
                    sum=(ptr[0]+ptr[2]+1) >> 1;lum[0]=sum; \
                    sum=(ptr[4]+ptr[6]+1) >> 1;lum[1]=sum; \
                    sum=(ptr[1]+ptr[5]+1) >> 1;cb[0]=sum; \
                    sum=(ptr[3]+ptr[7]+1) >> 1;cr[0]=sum; \
                    sum=(ptr[8]+ptr[10]+1) >> 1;lum[2]=sum; \
                    sum=(ptr[12]+ptr[14]+1) >> 1;lum[3]=sum; \
                    sum=(ptr[9]+ptr[13]+1) >> 1;cb[1]=sum; \
                    sum=(ptr[11]+ptr[15]+1) >> 1;cr[1]=sum; \
                    sum=(ptr[16]+ptr[18]+1) >> 1;lum[4]=sum; \
                    sum=(ptr[20]+ptr[22]+1) >> 1;lum[5]=sum; \
                    sum=(ptr[17]+ptr[21]+1) >> 1;cb[2]=sum; \
                    sum=(ptr[19]+ptr[23]+1) >> 1;cr[2]=sum; \
                    sum=(ptr[24]+ptr[26]+1) >> 1;lum[6]=sum; \
                    sum=(ptr[28]+ptr[30]+1) >> 1;lum[7]=sum; \
                    sum=(ptr[25]+ptr[29]+1) >> 1;cb[3]=sum; \
                    sum=(ptr[27]+ptr[31]+1) >> 1;cr[3]=sum; 

#define LINE_NOUV_AVG \
                    sum=(ptr[0]+ptr[2]+1) >> 1;lum[0]=sum; \
                    sum=(ptr[4]+ptr[6]+1) >> 1;lum[1]=sum; \
                    sum=(ptr[8]+ptr[10]+1) >> 1;lum[2]=sum; \
                    sum=(ptr[12]+ptr[14]+1) >> 1;lum[3]=sum; \
                    sum=(ptr[16]+ptr[18]+1) >> 1;lum[4]=sum; \
                    sum=(ptr[20]+ptr[22]+1) >> 1;lum[5]=sum; \
                    sum=(ptr[24]+ptr[26]+1) >> 1;lum[6]=sum; \
                    sum=(ptr[28]+ptr[30]+1) >> 1;lum[7]=sum; 

#define DEINT_LINE_LUM(ptroff) \
                    sum=(-lum_m4[(ptroff)]+(lum_m3[(ptroff)]<<2)+(lum_m2[(ptroff)]<<1)+(lum_m1[(ptroff)]<<2)-lum[(ptroff)]); \
                    lum_m4[(ptroff)]=lum_m2[(ptroff)];\
                    lum_m2[(ptroff)]=cm[(sum+4)>>3];\
                    sum=(-lum_m4[(ptroff)+1]+(lum_m3[(ptroff)+1]<<2)+(lum_m2[(ptroff)+1]<<1)+(lum_m1[(ptroff)+1]<<2)-lum[(ptroff)+1]); \
                    lum_m4[(ptroff)+1]=lum_m2[(ptroff)+1];\
                    lum_m2[(ptroff)+1]=cm[(sum+4)>>3];\
                    sum=(-lum_m4[(ptroff)+2]+(lum_m3[(ptroff)+2]<<2)+(lum_m2[(ptroff)+2]<<1)+(lum_m1[(ptroff)+2]<<2)-lum[(ptroff)+2]); \
                    lum_m4[(ptroff)+2]=lum_m2[(ptroff)+2];\
                    lum_m2[(ptroff)+2]=cm[(sum+4)>>3];\
                    sum=(-lum_m4[(ptroff)+3]+(lum_m3[(ptroff)+3]<<2)+(lum_m2[(ptroff)+3]<<1)+(lum_m1[(ptroff)+3]<<2)-lum[(ptroff)+3]); \
                    lum_m4[(ptroff)+3]=lum_m2[(ptroff)+3];\
                    lum_m2[(ptroff)+3]=cm[(sum+4)>>3];

#endif


/* Read two fields separately. */
static int aiw_read_picture(VideoData *s, uint8_t *data)
{
    uint8_t *ptr, *lum, *cb, *cr;
    int h;
#ifndef HAVE_MMX
    int sum;
#endif
    uint8_t* src = s->src_mem;
    uint8_t *ptrend = &src[s->width*2];
    lum=data;
    cb=&lum[s->width*s->height];
    cr=&cb[(s->width*s->height)/4];
    if (s->deint == 0 && s->halfw == 0) {
        while (read(s->fd,src,s->width*2) < 0) {
            usleep(100);
        }
        for (h = 0; h < s->height-2; h+=2) {
            for (ptr = &src[0]; ptr < ptrend; ptr+=32, lum+=16, cb+=8, cr+=8) {
                LINE_WITH_UV
                    }
            read(s->fd,src,s->width*2);
            for (ptr = &src[0]; ptr < ptrend; ptr+=32, lum+=16) {
                LINE_NO_UV
                    }
            read(s->fd,src,s->width*2);
        }
        /*
         * Do last two lines
         */
        for (ptr = &src[0]; ptr < ptrend; ptr+=32, lum+=16, cb+=8, cr+=8) {
            LINE_WITH_UV
                }
        read(s->fd,src,s->width*2);
        for (ptr = &src[0]; ptr < ptrend; ptr+=32, lum+=16) {
            LINE_NO_UV
                }
        /* drop second field */
        while (read(s->fd,src,s->width*2) < 0) {
            usleep(100);
        }
        for (h = 0; h < s->height - 1; h++) {
            read(s->fd,src,s->width*2);
        }
    } else if (s->halfw == 1) {
#ifdef HAVE_MMX
        mmx_t rounder;
        mmx_t masker;
        rounder.uw[0]=1;
        rounder.uw[1]=1;
        rounder.uw[2]=1;
        rounder.uw[3]=1;
        masker.ub[0]=0xff;
        masker.ub[1]=0;
        masker.ub[2]=0xff;
        masker.ub[3]=0;
        masker.ub[4]=0xff;
        masker.ub[5]=0;
        masker.ub[6]=0xff;
        masker.ub[7]=0;
        pxor_r2r(mm7,mm7);
        movq_m2r(rounder,mm6);
#endif
        while (read(s->fd,src,s->width*4) < 0) {
            usleep(100);
        }
        ptrend = &src[s->width*4];
        for (h = 0; h < s->height-2; h+=2) {
            for (ptr = &src[0]; ptr < ptrend; ptr+=32, lum+=8, cb+=4, cr+=4) {
                LINE_WITHUV_AVG
                    }
            read(s->fd,src,s->width*4);
#ifdef HAVE_MMX
            movq_m2r(masker,mm5);
#endif
            for (ptr = &src[0]; ptr < ptrend; ptr+=32, lum+=8) {
                LINE_NOUV_AVG
                    }
            read(s->fd,src,s->width*4);
        }
        /*
 * Do last two lines
 */
        for (ptr = &src[0]; ptr < ptrend; ptr+=32, lum+=8, cb+=4, cr+=4) {
            LINE_WITHUV_AVG
                }
        read(s->fd,src,s->width*4);
#ifdef HAVE_MMX
        movq_m2r(masker,mm5);
#endif
        for (ptr = &src[0]; ptr < ptrend; ptr+=32, lum+=8) {
            LINE_NOUV_AVG
                }
        /* drop second field */
        while (read(s->fd,src,s->width*4) < 0) {
            usleep(100);
        }
        for (h = 0; h < s->height - 1; h++) {
            read(s->fd,src,s->width*4);
        }
    } else {
        uint8_t *lum_m1, *lum_m2, *lum_m3, *lum_m4;
#ifdef HAVE_MMX
        mmx_t rounder;
        rounder.uw[0]=4;
        rounder.uw[1]=4;
        rounder.uw[2]=4;
        rounder.uw[3]=4;
        movq_m2r(rounder,mm6);
        pxor_r2r(mm7,mm7);
#else
        uint8_t *cm = cropTbl + MAX_NEG_CROP;
#endif

        /* read two fields and deinterlace them */
        while (read(s->fd,src,s->width*2) < 0) {
            usleep(100);
        }
        for (h = 0; h < (s->height/2)-2; h+=2) {
            for (ptr = &src[0]; ptr < ptrend; ptr+=32, lum+=16, cb+=8, cr+=8) {
                LINE_WITH_UV
                    }
            read(s->fd,src,s->width*2);
            /* skip a luminance line - will be filled in later */
            lum += s->width;
            for (ptr = &src[0]; ptr < ptrend; ptr+=32, lum+=16, cb+=8, cr+=8) {
                LINE_WITH_UV
                    }
            /* skip a luminance line - will be filled in later */
            lum += s->width;
            read(s->fd,src,s->width*2);
        }
        /*
 * Do last two lines
 */
        for (ptr = &src[0]; ptr < ptrend; ptr+=32, lum+=16, cb+=8, cr+=8) {
            LINE_WITH_UV
                }
        /* skip a luminance line - will be filled in later */
        lum += s->width;
        read(s->fd,src,s->width*2);
        for (ptr = &src[0]; ptr < ptrend; ptr+=32, lum+=16, cb+=8, cr+=8) {
            LINE_WITH_UV
                }
        /*
 *
 * SECOND FIELD
 *
 */
        lum=&data[s->width];
        while (read(s->fd,src,s->width*2) < 0) {
            usleep(10);
        }
        /* First (and last) two lines not interlaced */
        for (h = 0; h < 2; h++) {
            for (ptr = &src[0]; ptr < ptrend; ptr+=32, lum+=16) {
                LINE_NO_UV
                    }
            read(s->fd,src,s->width*2);
            /* skip a luminance line */
            lum += s->width;
        }
        lum_m1=&lum[-s->width];
        lum_m2=&lum_m1[-s->width];
        lum_m3=&lum_m2[-s->width];
        memmove(s->lum_m4_mem,&lum_m3[-s->width],s->width);
        for (; h < (s->height/2)-1; h++) {
            lum_m4=s->lum_m4_mem;
            for (ptr = &src[0]; ptr < ptrend; ptr+=32, lum+=16,lum_m1+=16,lum_m2+=16,lum_m3+=16,lum_m4+=16) {
                LINE_NO_UV

                    DEINT_LINE_LUM(0)
                    DEINT_LINE_LUM(4)
                    DEINT_LINE_LUM(8)
                    DEINT_LINE_LUM(12)
                    }
            read(s->fd,src,s->width*2);
            /* skip a luminance line */
            lum += s->width;
            lum_m1 += s->width;
            lum_m2 += s->width;
            lum_m3 += s->width;
            //                lum_m4 += s->width;
        }
        /*
 * Do last line
 */
        lum_m4=s->lum_m4_mem;
        for (ptr = &src[0]; ptr < ptrend; ptr+=32, lum+=16, lum_m1+=16, lum_m2+=16, lum_m3+=16, lum_m4+=16) {
            LINE_NO_UV

                DEINT_LINE_LUM(0)
                DEINT_LINE_LUM(4)
                DEINT_LINE_LUM(8)
                DEINT_LINE_LUM(12)
                }
    }
#ifdef HAVE_MMX
    emms();
#endif
    return s->frame_size;
}

static int aiw_close(VideoData *s)
{
    av_freep(&s->lum_m4_mem);
    av_freep(&s->src_mem);
    return 0;
}

int video_grab_init(void)
{
    av_register_input_format(&video_grab_device_format);
    return 0;
}
