/*
 * Image format
 * Copyright (c) 2000, 2001 Gerard Lantau.
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

#define IMGFMT_YUV     1
#define IMGFMT_PGMYUV  2
#define IMGFMT_PGM     3
#define IMGFMT_PPM     4

typedef struct {
    int width;
    int height;
    int img_number;
    int img_size;
    int img_fmt;
    int is_pipe;
    char path[1024];
} VideoData;

static inline int pnm_space(int c)  
{
    return (c==' ' || c=='\n' || c=='\r' || c=='\t');
}

static void pnm_get(ByteIOContext *f, char *str, int buf_size) 
{
    char *s;
    int c;
    
    do  {
        c=get_byte(f);
        if (c=='#')  {
            do  {
                c=get_byte(f);
            } while (c!='\n');
            c=get_byte(f);
        }
    } while (pnm_space(c));
    
    s=str;
    do  {
        if (url_feof(f))
            break;
        if ((s - str)  < buf_size - 1)
            *s++=c;
        c=get_byte(f);
    } while (!pnm_space(c));
    *s = '\0';
}

static int pgm_read(VideoData *s, ByteIOContext *f, UINT8 *buf, int size, int is_yuv)
{
    int width, height, i;
    char buf1[32];
    UINT8 *picture[3];

    width = s->width;
    height = s->height;

    pnm_get(f, buf1, sizeof(buf1));
    if (strcmp(buf1, "P5")) {
        return -EIO;
    }
    pnm_get(f, buf1, sizeof(buf1));
    pnm_get(f, buf1, sizeof(buf1));
    pnm_get(f, buf1, sizeof(buf1));
    
    picture[0] = buf;
    picture[1] = buf + width * height;
    picture[2] = buf + width * height + (width * height / 4);
    get_buffer(f, picture[0], width * height);
    
    height>>=1;
    width>>=1;
    if (is_yuv) {
        for(i=0;i<height;i++) {
            get_buffer(f, picture[1] + i * width, width);
            get_buffer(f, picture[2] + i * width, width);
        }
    } else {
        for(i=0;i<height;i++) {
            memset(picture[1] + i * width, 128, width);
            memset(picture[2] + i * width, 128, width);
        }
    }
    return 0;
}

static int ppm_read(VideoData *s, ByteIOContext *f, UINT8 *buf, int size)
{
    int width, height;
    char buf1[32];
    UINT8 *picture[3];

    width = s->width;
    height = s->height;

    pnm_get(f, buf1, sizeof(buf1));
    if (strcmp(buf1, "P6")) {
        return -EIO;
    }
    
    pnm_get(f, buf1, sizeof(buf1));
    pnm_get(f, buf1, sizeof(buf1));
    pnm_get(f, buf1, sizeof(buf1));
    
    picture[0] = buf;
    get_buffer(f, picture[0], width * height*3);
    
    return 0;

}

static int yuv_read(VideoData *s, const char *filename, UINT8 *buf, int size1)
{
    ByteIOContext pb1, *pb = &pb1;
    char fname[1024], *p;
    int size;

    size = s->width * s->height;
    
    strcpy(fname, filename);
    p = strrchr(fname, '.');
    if (!p || p[1] != 'Y')
        return -EIO;

    if (url_fopen(pb, fname, URL_RDONLY) < 0)
        return -EIO;
    
    get_buffer(pb, buf, size);
    url_fclose(pb);
    
    p[1] = 'U';
    if (url_fopen(pb, fname, URL_RDONLY) < 0)
        return -EIO;

    get_buffer(pb, buf + size, size / 4);
    url_fclose(pb);
    
    p[1] = 'V';
    if (url_fopen(pb, fname, URL_RDONLY) < 0)
        return -EIO;

    get_buffer(pb, buf + size + (size / 4), size / 4);
    url_fclose(pb);
    return 0;
}

int img_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    VideoData *s = s1->priv_data;
    char filename[1024];
    int ret;
    ByteIOContext f1, *f;

    snprintf(filename, sizeof(filename), s->path, s->img_number);
    
    if (!s->is_pipe) {
        f = &f1;
        if (url_fopen(f, filename, URL_RDONLY) < 0)
            return -EIO;
    } else {
        f = &s1->pb;
        if (url_feof(f))
            return -EIO;
    }

    av_new_packet(pkt, s->img_size);
    pkt->stream_index = 0;

    switch(s->img_fmt) {
    case IMGFMT_PGMYUV:
        ret = pgm_read(s, f, pkt->data, pkt->size, 1);
        break;
    case IMGFMT_PGM:
        ret = pgm_read(s, f, pkt->data, pkt->size, 0);
        break;
    case IMGFMT_YUV:
        ret = yuv_read(s, filename, pkt->data, pkt->size);
        break;
    case IMGFMT_PPM:
        ret = ppm_read(s, f, pkt->data, pkt->size);
        break;
    default:
        return -EIO;
    }
    
    if (!s->is_pipe) {
        url_fclose(f);
    }

    if (ret < 0) {
        av_free_packet(pkt);
        return -EIO; /* signal EOF */
    } else {
        s->img_number++;
        return 0;
    }
}

static int sizes[][2] = {
    { 640, 480 },
    { 720, 480 },
    { 720, 576 },
    { 352, 288 },
    { 352, 240 },
    { 160, 128 },
    { 512, 384 },
    { 640, 352 },
};

static int infer_size(int *width_ptr, int *height_ptr, int size)
{
    int i;

    for(i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
        if ((sizes[i][0] * sizes[i][1]) == size) {
            *width_ptr = sizes[i][0];
            *height_ptr = sizes[i][1];
            return 0;
        }
    }
    return -1;
}

static int img_read_header(AVFormatContext *s1, AVFormatParameters *ap)
{
    VideoData *s;
    int i, h;
    char buf[1024];
    char buf1[32];
    ByteIOContext pb1, *f = &pb1;
    AVStream *st;

    s = malloc(sizeof(VideoData));
    if (!s)
        return -ENOMEM;

    s1->priv_data = s;

    s1->nb_streams = 1;
    st = av_mallocz(sizeof(AVStream));
    if (!st) {
        free(s);
        return -ENOMEM;
    }
    s1->streams[0] = st;
    
    strcpy(s->path, s1->filename);
    s->img_number = 0;

    /* find format */
    if (s1->format->flags & AVFMT_NOFILE)
        s->is_pipe = 0;
    else
        s->is_pipe = 1;
        
    if (s1->format == &pgmyuvpipe_format ||
        s1->format == &pgmyuv_format)
        s->img_fmt = IMGFMT_PGMYUV;
    else if (s1->format == &pgmpipe_format ||
             s1->format == &pgm_format)
        s->img_fmt = IMGFMT_PGM;
    else if (s1->format == &imgyuv_format)
        s->img_fmt = IMGFMT_YUV;
    else if (s1->format == &ppmpipe_format ||
             s1->format == &ppm_format)
        s->img_fmt = IMGFMT_PPM;
    else
        goto fail;

    if (!s->is_pipe) {
        /* try to find the first image */
        for(i=0;i<5;i++) {
            snprintf(buf, sizeof(buf), s->path, s->img_number);
            if (url_fopen(f, buf, URL_RDONLY) >= 0)
                break;
            s->img_number++;
        }
        if (i == 5)
            goto fail;
    } else {
        f = &s1->pb;
    }
    
    /* find the image size */
    /* XXX: use generic file format guessing, as mpeg */
    switch(s->img_fmt) {
    case IMGFMT_PGM:
    case IMGFMT_PGMYUV:
    case IMGFMT_PPM:
        pnm_get(f, buf1, sizeof(buf1));
        pnm_get(f, buf1, sizeof(buf1));
        s->width = atoi(buf1);
        pnm_get(f, buf1, sizeof(buf1));
        h = atoi(buf1);
        if (s->img_fmt == IMGFMT_PGMYUV)
            h = (h * 2) / 3;
        s->height = h;
        if (s->width <= 0 ||
            s->height <= 0 ||
            (s->width % 2) != 0 ||
            (s->height % 2) != 0) {
            goto fail1;
        }
        break;
    case IMGFMT_YUV:
        /* infer size by using the file size. */
        {
            int img_size;
            URLContext *h;

            /* XXX: hack hack */
            h = url_fileno(f);
            img_size = url_seek(h, 0, SEEK_END);
            if (infer_size(&s->width, &s->height, img_size) < 0) {
                goto fail1;
            }
        }
        break;
    }

    if (!s->is_pipe) {
        url_fclose(f);
    } else {
        url_fseek(f, 0, SEEK_SET);
    }
    

    st->codec.codec_type = CODEC_TYPE_VIDEO;
    st->codec.codec_id = CODEC_ID_RAWVIDEO;
    st->codec.width = s->width;
    st->codec.height = s->height;
    if (s->img_fmt == IMGFMT_PPM) {
        st->codec.pix_fmt = PIX_FMT_RGB24;
        s->img_size = (s->width * s->height * 3);
    } else {
        st->codec.pix_fmt = PIX_FMT_YUV420P;
        s->img_size = (s->width * s->height * 3) / 2;
    }
    if (!ap || !ap->frame_rate)
        st->codec.frame_rate = 25 * FRAME_RATE_BASE;
    else
        st->codec.frame_rate = ap->frame_rate;
    
    return 0;
 fail1:
    if (!s->is_pipe)
        url_fclose(f);
 fail:
    free(s);
    return -EIO;
}

static int img_read_close(AVFormatContext *s1)
{
    VideoData *s = s1->priv_data;
    free(s);
    return 0;
}

/******************************************************/
/* image output */

static int pgm_save(AVPicture *picture, int width, int height, ByteIOContext *pb, int is_yuv) 
{
    int i, h;
    char buf[100];
    UINT8 *ptr, *ptr1, *ptr2;

    h = height;
    if (is_yuv)
        h = (height * 3) / 2;
    snprintf(buf, sizeof(buf), 
             "P5\n%d %d\n%d\n",
             width, h, 255);
    put_buffer(pb, buf, strlen(buf));
    
    ptr = picture->data[0];
    for(i=0;i<height;i++) {
        put_buffer(pb, ptr, width);
        ptr += picture->linesize[0];
    }

    if (is_yuv) {
        height >>= 1;
        width >>= 1;
        ptr1 = picture->data[1];
        ptr2 = picture->data[2];
        for(i=0;i<height;i++) {
            put_buffer(pb, ptr1, width);
            put_buffer(pb, ptr2, width);
            ptr1 += picture->linesize[1];
            ptr2 += picture->linesize[2];
        }
    }
    put_flush_packet(pb);
    return 0;
}

static int ppm_save(AVPicture *picture, int width, int height, ByteIOContext *pb) 
{
    int i;
    char buf[100];
    UINT8 *ptr;

    snprintf(buf, sizeof(buf), 
             "P6\n%d %d\n%d\n",
             width, height, 255);
    put_buffer(pb, buf, strlen(buf));
    
    ptr = picture->data[0];
    for(i=0;i<height;i++) {
        put_buffer(pb, ptr, width * 3);
        ptr += picture->linesize[0];
    }

    put_flush_packet(pb);
    return 0;
}

static int yuv_save(AVPicture *picture, int width, int height, const char *filename)
{
    ByteIOContext pb1, *pb = &pb1;
    char fname[1024], *p;
    int i, j;
    UINT8 *ptr;
    static char *ext = "YUV";

    strcpy(fname, filename);
    p = strrchr(fname, '.');
    if (!p || p[1] != 'Y')
        return -EIO;

    for(i=0;i<3;i++) {
        if (i == 1) {
            width >>= 1;
            height >>= 1;
        }

        p[1] = ext[i];
        if (url_fopen(pb, fname, URL_WRONLY) < 0)
            return -EIO;
    
        ptr = picture->data[i];
        for(j=0;j<height;j++) {
            put_buffer(pb, ptr, width);
            ptr += picture->linesize[i];
        }
        put_flush_packet(pb);
        url_fclose(pb);
    }
    return 0;
}

static int img_write_header(AVFormatContext *s)
{
    VideoData *img;

    img = av_mallocz(sizeof(VideoData));
    if (!img)
        return -1;
    s->priv_data = img;
    img->img_number = 1;
    strcpy(img->path, s->filename);

    /* find format */
    if (s->format->flags & AVFMT_NOFILE)
        img->is_pipe = 0;
    else
        img->is_pipe = 1;
        
    if (s->format == &pgmyuvpipe_format ||
        s->format == &pgmyuv_format) {
        img->img_fmt = IMGFMT_PGMYUV;
    } else if (s->format == &pgmpipe_format ||
               s->format == &pgm_format) {
        img->img_fmt = IMGFMT_PGM;
    } else if (s->format == &imgyuv_format) {
        img->img_fmt = IMGFMT_YUV;
    } else if (s->format == &ppmpipe_format ||
               s->format == &ppm_format) {
        img->img_fmt = IMGFMT_PPM;
    } else {
        goto fail;
    }
    return 0;
 fail:
    free(img);
    return -EIO;
}

static int img_write_packet(AVFormatContext *s, int stream_index,
                            UINT8 *buf, int size)
{
    VideoData *img = s->priv_data;
    AVStream *st = s->streams[stream_index];
    ByteIOContext pb1, *pb;
    AVPicture picture;
    int width, height, ret, size1;
    char filename[1024];

    width = st->codec.width;
    height = st->codec.height;

    switch(st->codec.pix_fmt) {
    case PIX_FMT_YUV420P:
        size1 = (width * height * 3) / 2;
        if (size != size1)
            return -EIO;
        
        picture.data[0] = buf;
        picture.data[1] = picture.data[0] + width * height;
        picture.data[2] = picture.data[1] + (width * height) / 4;
        picture.linesize[0] = width;
        picture.linesize[1] = width >> 1; 
        picture.linesize[2] = width >> 1;
        break;
    case PIX_FMT_RGB24:
        size1 = (width * height * 3);
        if (size != size1)
            return -EIO;
        picture.data[0] = buf;
        picture.linesize[0] = width * 3;
        break;
    default:
        return -EIO;
    }
    
    snprintf(filename, sizeof(filename), img->path, img->img_number);

    if (!img->is_pipe) {
        pb = &pb1;
        if (url_fopen(pb, filename, URL_WRONLY) < 0)
            return -EIO;
    } else {
        pb = &s->pb;
    }
    switch(img->img_fmt) {
    case IMGFMT_PGMYUV:
        ret = pgm_save(&picture, width, height, pb, 1);
        break;
    case IMGFMT_PGM:
        ret = pgm_save(&picture, width, height, pb, 0);
        break;
    case IMGFMT_YUV:
        ret = yuv_save(&picture, width, height, filename);
        break;
    case IMGFMT_PPM:
        ret = ppm_save(&picture, width, height, pb);
        break;
    }
    if (!img->is_pipe) {
        url_fclose(pb);
    }

    img->img_number++;
    return 0;
}

static int img_write_trailer(AVFormatContext *s)
{
    VideoData *img = s->priv_data;
    free(img);
    return 0;
}

AVFormat pgm_format = {
    "pgm",
    "pgm image format",
    "",
    "pgm",
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,

    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
    AVFMT_NOFILE,
};

AVFormat pgmyuv_format = {
    "pgmyuv",
    "pgm with YUV content image format",
    "",
    "pgm",
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,

    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
    AVFMT_NOFILE,
};

AVFormat ppm_format = {
    "ppm",
    "ppm image format",
    "",
    "ppm",
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,

    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
    AVFMT_NOFILE,
};

AVFormat imgyuv_format = {
    ".Y.U.V",
    ".Y.U.V format",
    "",
    "Y",
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,

    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
    AVFMT_NOFILE,
};

AVFormat pgmpipe_format = {
    "pgmpipe",
    "PGM pipe format",
    "",
    "pgm",
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,

    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
};

AVFormat pgmyuvpipe_format = {
    "pgmyuvpipe",
    "PGM YUV pipe format",
    "",
    "pgm",
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,

    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
};

AVFormat ppmpipe_format = {
    "ppmpipe",
    "PPM pipe format",
    "",
    "ppm",
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,

    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
};
