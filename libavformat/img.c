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

/* XXX: this is a hack */
int loop_input = 0;

typedef struct {
    int width;
    int height;
    int img_first;
    int img_last;
    int img_number;
    int img_count;
    int img_size;
    AVImageFormat *img_fmt;
    int pix_fmt;
    int is_pipe;
    char path[1024];
    /* temporary usage */
    void *ptr;
} VideoData;


/* return -1 if no image found */
static int find_image_range(int *pfirst_index, int *plast_index, 
                            const char *path)
{
    char buf[1024];
    int range, last_index, range1, first_index;

    /* find the first image */
    for(first_index = 0; first_index < 5; first_index++) {
        if (get_frame_filename(buf, sizeof(buf), path, first_index) < 0)
            goto fail;
        if (url_exist(buf))
            break;
    }
    if (first_index == 5)
        goto fail;
    
    /* find the last image */
    last_index = first_index;
    for(;;) {
        range = 0;
        for(;;) {
            if (!range)
                range1 = 1;
            else
                range1 = 2 * range;
            if (get_frame_filename(buf, sizeof(buf), path, 
                                   last_index + range1) < 0)
                goto fail;
            if (!url_exist(buf))
                break;
            range = range1;
            /* just in case... */
            if (range >= (1 << 30))
                goto fail;
        }
        /* we are sure than image last_index + range exists */
        if (!range)
            break;
        last_index += range;
    }
    *pfirst_index = first_index;
    *plast_index = last_index;
    return 0;
 fail:
    return -1;
}


static int image_probe(AVProbeData *p)
{
    if (filename_number_test(p->filename) >= 0 && guess_image_format(p->filename))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int read_header_alloc_cb(void *opaque, AVImageInfo *info)
{
    VideoData *s = opaque;

    s->width = info->width;
    s->height = info->height;
    s->pix_fmt = info->pix_fmt;
    /* stop image reading but no error */
    return 1;
}

static int img_read_header(AVFormatContext *s1, AVFormatParameters *ap)
{
    VideoData *s = s1->priv_data;
    int ret, first_index, last_index;
    char buf[1024];
    ByteIOContext pb1, *f = &pb1;
    AVStream *st;

    st = av_new_stream(s1, 0);
    if (!st) {
        av_free(s);
        return -ENOMEM;
    }

    if (ap && ap->image_format)
        s->img_fmt = ap->image_format;

    strcpy(s->path, s1->filename);
    s->img_number = 0;
    s->img_count = 0;
    
    /* find format */
    if (s1->iformat->flags & AVFMT_NOFILE)
        s->is_pipe = 0;
    else
        s->is_pipe = 1;
        
    if (!ap || !ap->frame_rate) {
        st->codec.frame_rate      = 25;
        st->codec.frame_rate_base = 1;
    } else {
        st->codec.frame_rate      = ap->frame_rate;
        st->codec.frame_rate_base = ap->frame_rate_base;
    }
    
    if (!s->is_pipe) {
        if (find_image_range(&first_index, &last_index, s->path) < 0)
            goto fail;
        s->img_first = first_index;
        s->img_last = last_index;
        s->img_number = first_index;
        /* compute duration */
        st->start_time = 0;
        st->duration = ((int64_t)AV_TIME_BASE * 
                        (last_index - first_index + 1) * 
                        st->codec.frame_rate_base) / st->codec.frame_rate;
        if (get_frame_filename(buf, sizeof(buf), s->path, s->img_number) < 0)
            goto fail;
        if (url_fopen(f, buf, URL_RDONLY) < 0)
            goto fail;
    } else {
        f = &s1->pb;
    }
    
    ret = av_read_image(f, s1->filename, s->img_fmt, read_header_alloc_cb, s);
    if (ret < 0)
        goto fail1;

    if (!s->is_pipe) {
        url_fclose(f);
    } else {
        url_fseek(f, 0, SEEK_SET);
    }
    
    st->codec.codec_type = CODEC_TYPE_VIDEO;
    st->codec.codec_id = CODEC_ID_RAWVIDEO;
    st->codec.width = s->width;
    st->codec.height = s->height;
    st->codec.pix_fmt = s->pix_fmt;
    s->img_size = avpicture_get_size(s->pix_fmt, (s->width+15)&(~15), (s->height+15)&(~15));

    return 0;
 fail1:
    if (!s->is_pipe)
        url_fclose(f);
 fail:
    av_free(s);
    return AVERROR_IO;
}

static int read_packet_alloc_cb(void *opaque, AVImageInfo *info)
{
    VideoData *s = opaque;

    if (info->width != s->width ||
        info->height != s->height)
        return -1;
    avpicture_fill(&info->pict, s->ptr, info->pix_fmt, (info->width+15)&(~15), (info->height+15)&(~15));
    return 0;
}

static int img_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    VideoData *s = s1->priv_data;
    char filename[1024];
    int ret;
    ByteIOContext f1, *f;

    if (!s->is_pipe) {
        /* loop over input */
        if (loop_input && s->img_number > s->img_last) {
            s->img_number = s->img_first;
        }
        if (get_frame_filename(filename, sizeof(filename),
                               s->path, s->img_number) < 0)
            return AVERROR_IO;
        f = &f1;
        if (url_fopen(f, filename, URL_RDONLY) < 0)
            return AVERROR_IO;
    } else {
        f = &s1->pb;
        if (url_feof(f))
            return AVERROR_IO;
    }

    av_new_packet(pkt, s->img_size);
    pkt->stream_index = 0;

    s->ptr = pkt->data;
    ret = av_read_image(f, filename, s->img_fmt, read_packet_alloc_cb, s);
    if (!s->is_pipe) {
        url_fclose(f);
    }

    if (ret < 0) {
        av_free_packet(pkt);
        return AVERROR_IO; /* signal EOF */
    } else {
        /* XXX: computing this pts is not necessary as it is done in
           the generic code too */
        pkt->pts = av_rescale((int64_t)s->img_count * s1->streams[0]->codec.frame_rate_base, s1->streams[0]->time_base.den, s1->streams[0]->codec.frame_rate) / s1->streams[0]->time_base.num;
        s->img_count++;
        s->img_number++;
        return 0;
    }
}

static int img_read_close(AVFormatContext *s1)
{
    return 0;
}

/******************************************************/
/* image output */

static int img_set_parameters(AVFormatContext *s, AVFormatParameters *ap)
{
    VideoData *img = s->priv_data;
    AVStream *st;
    AVImageFormat *img_fmt;
    int i;

    /* find output image format */
    if (ap && ap->image_format) {
        img_fmt = ap->image_format;
    } else {
        img_fmt = guess_image_format(s->filename);
    }
    if (!img_fmt)
        return -1;

    if (s->nb_streams != 1)
        return -1;
    
    st = s->streams[0];
    /* we select the first matching format */
    for(i=0;i<PIX_FMT_NB;i++) {
        if (img_fmt->supported_pixel_formats & (1 << i))
            break;
    }
    if (i >= PIX_FMT_NB)
        return -1;
    img->img_fmt = img_fmt;
    img->pix_fmt = i;
    st->codec.pix_fmt = img->pix_fmt;
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
        
    return 0;
}

static int img_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    VideoData *img = s->priv_data;
    AVStream *st = s->streams[pkt->stream_index];
    ByteIOContext pb1, *pb;
    AVPicture *picture;
    int width, height, ret;
    char filename[1024];
    AVImageInfo info;

    width = st->codec.width;
    height = st->codec.height;
    
    picture = (AVPicture *)pkt->data;

    if (!img->is_pipe) {
        if (get_frame_filename(filename, sizeof(filename), 
                               img->path, img->img_number) < 0)
            return AVERROR_IO;
        pb = &pb1;
        if (url_fopen(pb, filename, URL_WRONLY) < 0)
            return AVERROR_IO;
    } else {
        pb = &s->pb;
    }
    info.width = width;
    info.height = height;
    info.pix_fmt = st->codec.pix_fmt;
    info.interleaved = 0;    /* FIXME: there should be a way to set it right */
    info.pict = *picture;
    ret = av_write_image(pb, img->img_fmt, &info);
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

/* input */

static AVInputFormat image_iformat = {
    "image",
    "image sequence",
    sizeof(VideoData),
    image_probe,
    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
    NULL,
    AVFMT_NOFILE | AVFMT_NEEDNUMBER,
};

static AVInputFormat imagepipe_iformat = {
    "imagepipe",
    "piped image sequence",
    sizeof(VideoData),
    NULL, /* no probe */
    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
};


/* output */

static AVOutputFormat image_oformat = {
    "image",
    "image sequence",
    "",
    "",
    sizeof(VideoData),
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,
    AVFMT_NOFILE | AVFMT_NEEDNUMBER | AVFMT_RAWPICTURE,
    img_set_parameters,
};

static AVOutputFormat imagepipe_oformat = {
    "imagepipe",
    "piped image sequence",
    "",
    "",
    sizeof(VideoData),
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    img_write_header,
    img_write_packet,
    img_write_trailer,
    AVFMT_RAWPICTURE,
    img_set_parameters,
};

int img_init(void)
{
    av_register_input_format(&image_iformat);
    av_register_output_format(&image_oformat);

    av_register_input_format(&imagepipe_iformat);
    av_register_output_format(&imagepipe_oformat);
    
    return 0;
}
