/*
 * Image format
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
 * Copyright (c) 2004 Michael Niedermayer
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

typedef struct {
    int img_first;
    int img_last;
    int img_number;
    int img_count;
    int is_pipe;
    char path[1024];
} VideoData;

typedef struct {
    enum CodecID id;
    const char *str;
} IdStrMap;

static const IdStrMap img_tags[] = {
    { CODEC_ID_MJPEG     , "jpeg"},
    { CODEC_ID_MJPEG     , "jpg"},
    { CODEC_ID_LJPEG     , "ljpg"},
    { CODEC_ID_MPEG1VIDEO, "mpg1-img"},
    { CODEC_ID_MPEG2VIDEO, "mpg2-img"},
    { CODEC_ID_MPEG4     , "mpg4-img"},
    { CODEC_ID_FFV1      , "ffv1-img"},
    {0, NULL}
};

static enum CodecID av_str2id(const IdStrMap *tags, const char *str)
{
    while(*str && *str!='.') str++;
    if(*str) str++;

    while (tags->id) {
        int i;
        for(i=0; toupper(tags->str[i]) == toupper(str[i]); i++){
            if(tags->str[i]==0 && str[i]==0)
                return tags->id;
        }

        tags++;
    }
    return CODEC_ID_NONE;
}

static const char *av_id2str(const IdStrMap *tags, enum CodecID id)
{
    while (tags->id) {
        if(tags->id == id)
            return tags->str;
        tags++;
    }
    return NULL;
}

/* return -1 if no image found */
static int find_image_range(int *pfirst_index, int *plast_index, 
                            const char *path)
{
    char buf[1024];
    int range, last_index, range1, first_index;

    /* find the first image */
    for(first_index = 0; first_index < 5; first_index++) {
        if (get_frame_filename(buf, sizeof(buf), path, first_index) < 0){
            *pfirst_index = 
            *plast_index = 1;
            return 0;
        }
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
    if (filename_number_test(p->filename) >= 0 && av_str2id(img_tags, p->filename))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int img_read_header(AVFormatContext *s1, AVFormatParameters *ap)
{
    VideoData *s = s1->priv_data;
    int first_index, last_index;
    AVStream *st;

    s1->ctx_flags |= AVFMTCTX_NOHEADER;

    st = av_new_stream(s1, 0);
    if (!st) {
        av_free(s);
        return -ENOMEM;
    }

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
    }
    
    st->codec.codec_type = CODEC_TYPE_VIDEO;
    st->codec.codec_id = av_str2id(img_tags, s->path);

    return 0;
 
 fail:
    av_free(s);
    return AVERROR_IO;
}

static int img_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    VideoData *s = s1->priv_data;
    char filename[1024];
    int ret;
    ByteIOContext f1, *f;

    if (!s->is_pipe) {
        /* loop over input */
/*        if (loop_input && s->img_number > s->img_last) {
            s->img_number = s->img_first;
        }*/
        if (get_frame_filename(filename, sizeof(filename),
                               s->path, s->img_number)<0 && s->img_number > 1)
            return AVERROR_IO;
        f = &f1;
        if (url_fopen(f, filename, URL_RDONLY) < 0)
            return AVERROR_IO;
    } else {
        f = &s1->pb;
        if (url_feof(f))
            return AVERROR_IO;
    }

    if (s->is_pipe) {
        av_new_packet(pkt, 4096);
    }else{
        av_new_packet(pkt, url_filesize(url_fileno(f)));
    }
    pkt->stream_index = 0;
    pkt->flags |= PKT_FLAG_KEY;

    ret = get_buffer(f, pkt->data, pkt->size);
    if (!s->is_pipe) {
        url_fclose(f);
    }

    if (ret <= 0) {
        av_free_packet(pkt);
        return AVERROR_IO; /* signal EOF */
    } else {
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
    ByteIOContext pb1, *pb;
    char filename[1024];

    if (!img->is_pipe) {
        if (get_frame_filename(filename, sizeof(filename), 
                               img->path, img->img_number) < 0 && img->img_number>1)
            return AVERROR_IO;
        pb = &pb1;
        if (url_fopen(pb, filename, URL_WRONLY) < 0)
            return AVERROR_IO;
    } else {
        pb = &s->pb;
    }

    put_buffer(pb, pkt->data, pkt->size);
    put_flush_packet(pb);
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

static AVInputFormat image2_iformat = {
    "image2",
    "image2 sequence",
    sizeof(VideoData),
    image_probe,
    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
    NULL,
    AVFMT_NOFILE,
};

static AVInputFormat image2pipe_iformat = {
    "image2pipe",
    "piped image2 sequence",
    sizeof(VideoData),
    NULL, /* no probe */
    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
};


/* output */

static AVOutputFormat image2_oformat = {
    "image2",
    "image2 sequence",
    "",
    "",
    sizeof(VideoData),
    CODEC_ID_NONE,
    CODEC_ID_MJPEG,
    img_write_header,
    img_write_packet,
    img_write_trailer,
    AVFMT_NOFILE,
};

static AVOutputFormat image2pipe_oformat = {
    "image2pipe",
    "piped image2 sequence",
    "",
    "",
    sizeof(VideoData),
    CODEC_ID_NONE,
    CODEC_ID_MJPEG,
    img_write_header,
    img_write_packet,
    img_write_trailer,
};

int img2_init(void)
{
    av_register_input_format(&image2_iformat);
    av_register_output_format(&image2_oformat);

    av_register_input_format(&image2pipe_iformat);
    av_register_output_format(&image2pipe_oformat);
    
    return 0;
}
