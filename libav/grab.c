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

        s->time_frame = av_gettime();
        s->use_mmap = 0;
    } else {
        video_buf = mmap(0,gb_buffers.size,PROT_READ|PROT_WRITE,MAP_SHARED,video_fd,0);
        if ((unsigned char*)-1 == video_buf) {
            perror("mmap");
            goto fail;
        }
        gb_frame = 0;
        s->time_frame = av_gettime();
        
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

    /* Setup to capture the next frame */
    gb_buf.frame = (gb_frame + 1) % gb_buffers.frames;
    if (ioctl(s->fd, VIDIOCMCAPTURE, &gb_buf) < 0) {
        if (errno == EAGAIN)
            fprintf(stderr,"Cannot Sync\n");
        else
            perror("VIDIOCMCAPTURE");
        return -EIO;
    }

    while (ioctl(s->fd, VIDIOCSYNC, &gb_frame) < 0 &&
           (errno == EAGAIN || errno == EINTR));

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
        curtime = av_gettime();
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

    if (s->use_mmap)
        munmap(video_buf, gb_buffers.size);

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
    .flags = AVFMT_NOFILE,
};

/*
 * Done below so we can register the aiw grabber
 * /
int video_grab_init(void)
{
    av_register_input_format(&video_grab_device_format);
    return 0;
}
*/

typedef struct {
    int fd;
    int frame_format; /* see VIDEO_PALETTE_xxx */
    int width, height;
    int frame_rate;
    INT64 time_frame;
    int frame_size;
    int deint;
    int halfw;
    UINT8 *src_mem;
    UINT8 *lum_m4_mem;
} AIWVideoData;

static int aiw_grab_read_header(AVFormatContext *s1, AVFormatParameters *ap)
{
    AIWVideoData *s = s1->priv_data;
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

    video_fd = open(v4l_device, O_RDONLY | O_NONBLOCK);
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
        pict.palette=VIDEO_PALETTE_YUV422;
        ret = ioctl(video_fd, VIDIOCSPICT, &pict);
        if (ret < 0) {
            fprintf(stderr,"Could Not Find YUY2 capture window.\n");
            goto fail;
        }
        if ((width == video_cap.maxwidth && height == video_cap.maxheight) ||
            (width == video_cap.maxwidth && height == video_cap.maxheight*2) ||
            (width == video_cap.maxwidth/2 && height == video_cap.maxheight)) {

            s->deint=0;
            s->halfw=0;
            if (height == video_cap.maxheight*2) s->deint=1;
            if (width == video_cap.maxwidth/2) s->halfw=1;
        } else {
            fprintf(stderr,"\nIncorrect Grab Size Supplied - Supported Sizes Are:\n");
            fprintf(stderr," %dx%d  %dx%d %dx%d\n\n",
                video_cap.maxwidth,video_cap.maxheight,
                video_cap.maxwidth,video_cap.maxheight*2,
                video_cap.maxwidth/2,video_cap.maxheight);
            goto fail;
        }

        s->frame_format = pict.palette;

        val = 1;
        ioctl(video_fd, VIDIOCCAPTURE, &val);

        s->time_frame = av_gettime();
    } else {
        fprintf(stderr,"mmap-based capture will not work with this grab.\n");
        goto fail;
    }

    frame_size = (width * height * 3) / 2;
    st->codec.pix_fmt = PIX_FMT_YUV420P;
    s->fd = video_fd;
    s->frame_size = frame_size;
    
    st->codec.codec_type = CODEC_TYPE_VIDEO;
    st->codec.codec_id = CODEC_ID_RAWVIDEO;
    st->codec.width = width;
    st->codec.height = height;
    st->codec.frame_rate = frame_rate;

    if (s->halfw == 0) {
        s->src_mem = av_malloc(s->width*2);
    } else {
        s->src_mem = av_malloc(s->width*4);
    }
    if (!s->src_mem) goto fail;

    s->lum_m4_mem = av_malloc(s->width);
    if (!s->lum_m4_mem) {
        av_free(s->src_mem);
        goto fail;
    }

    return 0;
 fail:
    if (video_fd >= 0)
        close(video_fd);
    av_free(st);
    return -EIO;
}

//#ifdef HAVE_MMX
//#undef HAVE_MMX
//#endif

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


static int aiw_grab_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    AIWVideoData *s = s1->priv_data;
    INT64 curtime, delay;
    struct timespec ts;
    int first;
    INT64 per_frame = (INT64_C(1000000) * FRAME_RATE_BASE) / s->frame_rate;
    int dropped = 0;

    /* Calculate the time of the next frame */
    s->time_frame += per_frame;

    /* wait based on the frame rate */
    for(first = 1;; first = 0) {
        curtime = av_gettime();
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

    /* read fields */
    {
        UINT8 *ptr, *lum, *cb, *cr;
        int h;
#ifndef HAVE_MMX
        int sum;
#endif
        UINT8* src = s->src_mem;
        UINT8 *ptrend = &src[s->width*2];
        lum=&pkt->data[0];
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
            UINT8 *lum_m1, *lum_m2, *lum_m3, *lum_m4;
#ifdef HAVE_MMX
            mmx_t rounder;
            rounder.uw[0]=4;
            rounder.uw[1]=4;
            rounder.uw[2]=4;
            rounder.uw[3]=4;
            movq_m2r(rounder,mm6);
            pxor_r2r(mm7,mm7);
#else
            UINT8 *cm = cropTbl + MAX_NEG_CROP;
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
            lum=&pkt->data[s->width];
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
    }
    return s->frame_size;
}

static int aiw_grab_read_close(AVFormatContext *s1)
{
    AIWVideoData *s = s1->priv_data;

    close(s->fd);
    av_free(s->lum_m4_mem);
    av_free(s->src_mem);

    return 0;
}

AVInputFormat aiw_grab_device_format = {
    "aiw_grab_device",
    "All-In-Wonder (km read-based) video grab",
    sizeof(AIWVideoData),
    NULL,
    aiw_grab_read_header,
    aiw_grab_read_packet,
    aiw_grab_read_close,
    .flags = AVFMT_NOFILE,
};

int video_grab_init(void)
{
    av_register_input_format(&video_grab_device_format);
    av_register_input_format(&aiw_grab_device_format);
    return 0;
}
