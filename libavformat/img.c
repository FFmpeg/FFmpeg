/*
 * Image format
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
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

extern AVInputFormat pgm_iformat;
extern AVOutputFormat pgm_oformat;
extern AVInputFormat pgmyuv_iformat;
extern AVOutputFormat pgmyuv_oformat;
extern AVInputFormat ppm_iformat;
extern AVOutputFormat ppm_oformat;
extern AVInputFormat imgyuv_iformat;
extern AVOutputFormat imgyuv_oformat;
extern AVInputFormat pgmpipe_iformat;
extern AVOutputFormat pgmpipe_oformat;
extern AVInputFormat pgmyuvpipe_iformat;
extern AVOutputFormat pgmyuvpipe_oformat;
extern AVInputFormat ppmpipe_iformat;
extern AVOutputFormat ppmpipe_oformat;
extern AVOutputFormat yuv4mpegpipe_oformat;

#define IMGFMT_YUV     1
#define IMGFMT_PGMYUV  2
#define IMGFMT_PGM     3
#define IMGFMT_PPM     4
#define IMGFMT_YUV4MPEG     5

#define Y4M_MAGIC "YUV4MPEG2"
#define Y4M_FRAME_MAGIC "FRAME"
#define Y4M_LINE_MAX 256

typedef struct {
    int width;
    int height;
    int img_number;
    int img_size;
    int img_fmt;
    int is_pipe;
    int header_written;
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

static int img_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    VideoData *s = s1->priv_data;
    char filename[1024];
    int ret;
    ByteIOContext f1, *f;

/*
    This if-statement destroys pipes - I do not see why it is necessary
    if (get_frame_filename(filename, sizeof(filename),
                           s->path, s->img_number) < 0)
        return -EIO;
*/
    get_frame_filename(filename, sizeof(filename),
                       s->path, s->img_number);
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
        pkt->pts = ((INT64)s->img_number * s1->pts_den * FRAME_RATE_BASE) / (s1->streams[0]->codec.frame_rate * s1->pts_num);
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
    { 640, 240 },
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
    VideoData *s = s1->priv_data;
    int i, h;
    char buf[1024];
    char buf1[32];
    ByteIOContext pb1, *f = &pb1;
    AVStream *st;

    st = av_new_stream(s1, 0);
    if (!st) {
        av_free(s);
        return -ENOMEM;
    }

    strcpy(s->path, s1->filename);
    s->img_number = 0;

    /* find format */
    if (s1->iformat->flags & AVFMT_NOFILE)
        s->is_pipe = 0;
    else
        s->is_pipe = 1;
        
    if (s1->iformat == &pgmyuvpipe_iformat ||
        s1->iformat == &pgmyuv_iformat)
        s->img_fmt = IMGFMT_PGMYUV;
    else if (s1->iformat == &pgmpipe_iformat ||
             s1->iformat == &pgm_iformat)
        s->img_fmt = IMGFMT_PGM;
    else if (s1->iformat == &imgyuv_iformat)
        s->img_fmt = IMGFMT_YUV;
    else if (s1->iformat == &ppmpipe_iformat ||
             s1->iformat == &ppm_iformat)
        s->img_fmt = IMGFMT_PPM;
    else
        goto fail;

    if (!s->is_pipe) {
        /* try to find the first image */
        for(i=0;i<5;i++) {
            if (get_frame_filename(buf, sizeof(buf), s->path, s->img_number) < 0)
                goto fail;
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
    av_free(s);
    return -EIO;
}

static int img_read_close(AVFormatContext *s1)
{
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

static int yuv4mpeg_save(AVPicture *picture, int width, int height, ByteIOContext *pb, int need_stream_header, 
                        int is_yuv, int raten, int rated, int aspectn, int aspectd) 
{
    int i, n, m;
    char buf[Y4M_LINE_MAX+1], buf1[20];
    UINT8 *ptr, *ptr1, *ptr2;
    
    /* construct stream header, if this is the first frame */
    if(need_stream_header) {
        n = snprintf(buf, sizeof(buf), "%s W%d H%d F%d:%d I%s A%d:%d\n",
                Y4M_MAGIC,
                width,
                height,
                raten, rated,
                "p",			/* ffmpeg seems to only output progressive video */
                aspectn, aspectd);
        if (n < 0) {
            fprintf(stderr, "Error. YUV4MPEG stream header write failed.\n");
        } else {
            fprintf(stderr, "YUV4MPEG stream header written. FPS is %d\n", raten);
            put_buffer(pb, buf, strlen(buf));
        }
    }
    
    /* construct frame header */
    m = snprintf(buf1, sizeof(buf1), "%s \n", Y4M_FRAME_MAGIC);
    if (m < 0) {
        fprintf(stderr, "Error. YUV4MPEG frame header write failed.\n");
    } else {
        /* fprintf(stderr, "YUV4MPEG frame header written.\n"); */
        put_buffer(pb, buf1, strlen(buf1));
    }
    
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
        for(i=0;i<height;i++) {		/* Cb */
            put_buffer(pb, ptr1, width);
            ptr1 += picture->linesize[1];
        }
         for(i=0;i<height;i++) {	/* Cr */
            put_buffer(pb, ptr2, width);
            ptr2 += picture->linesize[2];
         }
    }
    put_flush_packet(pb);
    return 0;
}

static int img_write_header(AVFormatContext *s)
{
    VideoData *img = s->priv_data;

    img->img_number = 1;
    strcpy(img->path, s->filename);

    /* find format */
    if (s->oformat->flags & AVFMT_NOFILE)
        img->is_pipe = 0;
    else
        img->is_pipe = 1;
        
    if (s->oformat == &pgmyuvpipe_oformat ||
        s->oformat == &pgmyuv_oformat) {
        img->img_fmt = IMGFMT_PGMYUV;
    } else if (s->oformat == &pgmpipe_oformat ||
               s->oformat == &pgm_oformat) {
        img->img_fmt = IMGFMT_PGM;
    } else if (s->oformat == &imgyuv_oformat) {
        img->img_fmt = IMGFMT_YUV;
    } else if (s->oformat == &ppmpipe_oformat ||
               s->oformat == &ppm_oformat) {
        img->img_fmt = IMGFMT_PPM;
    } else if (s->oformat == &yuv4mpegpipe_oformat) {
        img->img_fmt = IMGFMT_YUV4MPEG;
        img->header_written = 0;    
    } else {
        goto fail;
    }
    return 0;
 fail:
    av_free(img);
    return -EIO;
}

static int img_write_packet(AVFormatContext *s, int stream_index,
                            UINT8 *buf, int size, int force_pts)
{
    VideoData *img = s->priv_data;
    AVStream *st = s->streams[stream_index];
    ByteIOContext pb1, *pb;
    AVPicture picture;
    int width, height, need_stream_header, ret, size1, raten, rated, aspectn, aspectd, fps, fps1;
    char filename[1024];

    width = st->codec.width;
    height = st->codec.height;
    
    if (img->img_number == 1) {
        need_stream_header = 1;
    } else {
        need_stream_header = 0;
    }
    
    fps = st->codec.frame_rate;
    fps1 = (((float)fps / FRAME_RATE_BASE) * 1000);
   
   /* Sorry about this messy code, but mpeg2enc is very picky about
    * the framerates it accepts. */
    switch(fps1) {
    case 23976:
        raten = 24000; /* turn the framerate into a ratio */
        rated = 1001;
        break;
    case 29970:
        raten = 30000;
        rated = 1001;
        break;
    case 25000:
        raten = 25;
        rated = 1;
        break;
    case 30000:
        raten = 30;
        rated = 1;
        break;
    case 24000:
        raten = 24;
        rated = 1;
        break;
    case 50000:
        raten = 50;
        rated = 1;
        break;
    case 59940:
        raten = 60000;
        rated = 1001;
        break;
    case 60000:
        raten = 60;
        rated = 1;
        break;
    default:
        raten = fps1; /* this setting should work, but often doesn't */
        rated = 1000;
        break;
    }
    
    aspectn = 1;
    aspectd = 1;	/* ffmpeg always uses a 1:1 aspect ratio */

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
    
/*
    This if-statement destroys pipes - I do not see why it is necessary
    if (get_frame_filename(filename, sizeof(filename), 
                           img->path, img->img_number) < 0)
        return -EIO;
*/
    get_frame_filename(filename, sizeof(filename), 
                       img->path, img->img_number);
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
    case IMGFMT_YUV4MPEG:
         ret = yuv4mpeg_save(&picture, width, height, pb,
         need_stream_header, 1, raten, rated, aspectn, aspectd);
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
    return 0;
}

static AVInputFormat pgm_iformat = {
    "pgm",
    "pgm image format",
    sizeof(VideoData),
    NULL,
    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
    AVFMT_NOFILE | AVFMT_NEEDNUMBER,
    .extensions = "pgm",
};

static AVOutputFormat pgm_oformat = {
    "pgm",
    "pgm image format",
    "",
    "pgm",
    sizeof(VideoData),
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,
    AVFMT_NOFILE | AVFMT_NEEDNUMBER,
};

static AVInputFormat pgmyuv_iformat = {
    "pgmyuv",
    "pgm with YUV content image format",
    sizeof(VideoData),
    NULL, /* no probe */
    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
    AVFMT_NOFILE | AVFMT_NEEDNUMBER,
};

static AVOutputFormat pgmyuv_oformat = {
    "pgmyuv",
    "pgm with YUV content image format",
    "",
    "pgm",
    sizeof(VideoData),
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,
    AVFMT_NOFILE | AVFMT_NEEDNUMBER,
};

static AVInputFormat ppm_iformat = {
    "ppm",
    "ppm image format",
    sizeof(VideoData),
    NULL,
    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
    AVFMT_NOFILE | AVFMT_NEEDNUMBER | AVFMT_RGB24,
    .extensions = "ppm",
};

static AVOutputFormat ppm_oformat = {
    "ppm",
    "ppm image format",
    "",
    "ppm",
    sizeof(VideoData),
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,
    AVFMT_NOFILE | AVFMT_NEEDNUMBER | AVFMT_RGB24,
};

static AVInputFormat imgyuv_iformat = {
    ".Y.U.V",
    ".Y.U.V format",
    sizeof(VideoData),
    NULL,
    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
    AVFMT_NOFILE | AVFMT_NEEDNUMBER,
    .extensions = "Y",
};

static AVOutputFormat imgyuv_oformat = {
    ".Y.U.V",
    ".Y.U.V format",
    "",
    "Y",
    sizeof(VideoData),
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,
    AVFMT_NOFILE | AVFMT_NEEDNUMBER,
};

static AVInputFormat pgmpipe_iformat = {
    "pgmpipe",
    "PGM pipe format",
    sizeof(VideoData),
    NULL, /* no probe */
    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
};

static AVOutputFormat pgmpipe_oformat = {
    "pgmpipe",
    "PGM pipe format",
    "",
    "pgm",
    sizeof(VideoData),
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,
};

static AVInputFormat pgmyuvpipe_iformat = {
    "pgmyuvpipe",
    "PGM YUV pipe format",
    sizeof(VideoData),
    NULL, /* no probe */
    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
};

static AVOutputFormat pgmyuvpipe_oformat = {
    "pgmyuvpipe",
    "PGM YUV pipe format",
    "",
    "pgm",
    sizeof(VideoData),
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,
};

static AVInputFormat ppmpipe_iformat = {
    "ppmpipe",
    "PPM pipe format",
    sizeof(VideoData),
    NULL, /* no probe */
    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
    .flags = AVFMT_RGB24,
};

static AVOutputFormat ppmpipe_oformat = {
    "ppmpipe",
    "PPM pipe format",
    "",
    "ppm",
    sizeof(VideoData),
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,
    .flags = AVFMT_RGB24,
};


static AVOutputFormat yuv4mpegpipe_oformat = {
    "yuv4mpegpipe",
    "YUV4MPEG pipe format",
    "",
    "yuv4mpeg",
    sizeof(VideoData),
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,
};


int img_init(void)
{
    av_register_input_format(&pgm_iformat);
    av_register_output_format(&pgm_oformat);

    av_register_input_format(&pgmyuv_iformat);
    av_register_output_format(&pgmyuv_oformat);

    av_register_input_format(&ppm_iformat);
    av_register_output_format(&ppm_oformat);

    av_register_input_format(&imgyuv_iformat);
    av_register_output_format(&imgyuv_oformat);
    
    av_register_input_format(&pgmpipe_iformat);
    av_register_output_format(&pgmpipe_oformat);

    av_register_input_format(&pgmyuvpipe_iformat);
    av_register_output_format(&pgmyuvpipe_oformat);

    av_register_input_format(&ppmpipe_iformat);
    av_register_output_format(&ppmpipe_oformat);
       
    av_register_output_format(&yuv4mpegpipe_oformat);
    
    return 0;
}
