/*
 * Image format
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
 * Copyright (c) 2004 Michael Niedermayer
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
#include "avformat.h"
#include "avstring.h"

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
    { CODEC_ID_PNG       , "png"},
    { CODEC_ID_PNG       , "mng"},
    { CODEC_ID_PPM       , "ppm"},
    { CODEC_ID_PGM       , "pgm"},
    { CODEC_ID_PGMYUV    , "pgmyuv"},
    { CODEC_ID_PBM       , "pbm"},
    { CODEC_ID_PAM       , "pam"},
    { CODEC_ID_MPEG1VIDEO, "mpg1-img"},
    { CODEC_ID_MPEG2VIDEO, "mpg2-img"},
    { CODEC_ID_MPEG4     , "mpg4-img"},
    { CODEC_ID_FFV1      , "ffv1-img"},
    { CODEC_ID_RAWVIDEO  , "y"},
    { CODEC_ID_BMP       , "bmp"},
    { CODEC_ID_GIF       , "gif"},
    { CODEC_ID_TARGA     , "tga"},
    { CODEC_ID_TIFF      , "tiff"},
    { CODEC_ID_SGI       , "sgi"},
    { CODEC_ID_PTX       , "ptx"},
    { CODEC_ID_PCX       , "pcx"},
    { CODEC_ID_SUNRAST   , "sun"},
    { CODEC_ID_SUNRAST   , "ras"},
    { CODEC_ID_SUNRAST   , "rs"},
    { CODEC_ID_SUNRAST   , "im1"},
    { CODEC_ID_SUNRAST   , "im8"},
    { CODEC_ID_SUNRAST   , "im24"},
    { CODEC_ID_SUNRAST   , "sunras"},
    {0, NULL}
};

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
static enum CodecID av_str2id(const IdStrMap *tags, const char *str)
{
    str= strrchr(str, '.');
    if(!str) return CODEC_ID_NONE;
    str++;

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

/* return -1 if no image found */
static int find_image_range(int *pfirst_index, int *plast_index,
                            const char *path)
{
    char buf[1024];
    int range, last_index, range1, first_index;

    /* find the first image */
    for(first_index = 0; first_index < 5; first_index++) {
        if (av_get_frame_filename(buf, sizeof(buf), path, first_index) < 0){
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
            if (av_get_frame_filename(buf, sizeof(buf), path,
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
    if (p->filename && av_str2id(img_tags, p->filename)) {
        if (av_filename_number_test(p->filename))
            return AVPROBE_SCORE_MAX;
        else
            return AVPROBE_SCORE_MAX/2;
    }
    return 0;
}

enum CodecID av_guess_image2_codec(const char *filename){
    return av_str2id(img_tags, filename);
}

static int img_read_header(AVFormatContext *s1, AVFormatParameters *ap)
{
    VideoData *s = s1->priv_data;
    int first_index, last_index;
    AVStream *st;

    s1->ctx_flags |= AVFMTCTX_NOHEADER;

    st = av_new_stream(s1, 0);
    if (!st) {
        return AVERROR(ENOMEM);
    }

    av_strlcpy(s->path, s1->filename, sizeof(s->path));
    s->img_number = 0;
    s->img_count = 0;

    /* find format */
    if (s1->iformat->flags & AVFMT_NOFILE)
        s->is_pipe = 0;
    else{
        s->is_pipe = 1;
        st->need_parsing = AVSTREAM_PARSE_FULL;
    }

    if (!ap->time_base.num) {
        av_set_pts_info(st, 60, 1, 25);
    } else {
        av_set_pts_info(st, 60, ap->time_base.num, ap->time_base.den);
    }

    if(ap->width && ap->height){
        st->codec->width = ap->width;
        st->codec->height= ap->height;
    }

    if (!s->is_pipe) {
        if (find_image_range(&first_index, &last_index, s->path) < 0)
            return AVERROR(EIO);
        s->img_first = first_index;
        s->img_last = last_index;
        s->img_number = first_index;
        /* compute duration */
        st->start_time = 0;
        st->duration = last_index - first_index + 1;
    }

    if(ap->video_codec_id){
        st->codec->codec_type = CODEC_TYPE_VIDEO;
        st->codec->codec_id = ap->video_codec_id;
    }else if(ap->audio_codec_id){
        st->codec->codec_type = CODEC_TYPE_AUDIO;
        st->codec->codec_id = ap->audio_codec_id;
    }else{
        st->codec->codec_type = CODEC_TYPE_VIDEO;
        st->codec->codec_id = av_str2id(img_tags, s->path);
    }
    if(st->codec->codec_type == CODEC_TYPE_VIDEO && ap->pix_fmt != PIX_FMT_NONE)
        st->codec->pix_fmt = ap->pix_fmt;

    return 0;
}

static int img_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    VideoData *s = s1->priv_data;
    char filename[1024];
    int i;
    int size[3]={0}, ret[3]={0};
    ByteIOContext *f[3];
    AVCodecContext *codec= s1->streams[0]->codec;

    if (!s->is_pipe) {
        /* loop over input */
        if (s1->loop_input && s->img_number > s->img_last) {
            s->img_number = s->img_first;
        }
        if (av_get_frame_filename(filename, sizeof(filename),
                                  s->path, s->img_number)<0 && s->img_number > 1)
            return AVERROR(EIO);
        for(i=0; i<3; i++){
            if (url_fopen(&f[i], filename, URL_RDONLY) < 0)
                return AVERROR(EIO);
            size[i]= url_fsize(f[i]);

            if(codec->codec_id != CODEC_ID_RAWVIDEO)
                break;
            filename[ strlen(filename) - 1 ]= 'U' + i;
        }

        if(codec->codec_id == CODEC_ID_RAWVIDEO && !codec->width)
            infer_size(&codec->width, &codec->height, size[0]);
    } else {
        f[0] = s1->pb;
        if (url_feof(f[0]))
            return AVERROR(EIO);
        size[0]= 4096;
    }

    av_new_packet(pkt, size[0] + size[1] + size[2]);
    pkt->stream_index = 0;
    pkt->flags |= PKT_FLAG_KEY;

    pkt->size= 0;
    for(i=0; i<3; i++){
        if(size[i]){
            ret[i]= get_buffer(f[i], pkt->data + pkt->size, size[i]);
            if (!s->is_pipe)
                url_fclose(f[i]);
            if(ret[i]>0)
                pkt->size += ret[i];
        }
    }

    if (ret[0] <= 0 || ret[1]<0 || ret[2]<0) {
        av_free_packet(pkt);
        return AVERROR(EIO); /* signal EOF */
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

#ifdef CONFIG_MUXERS
/******************************************************/
/* image output */

static int img_write_header(AVFormatContext *s)
{
    VideoData *img = s->priv_data;

    img->img_number = 1;
    av_strlcpy(img->path, s->filename, sizeof(img->path));

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
    ByteIOContext *pb[3];
    char filename[1024];
    AVCodecContext *codec= s->streams[ pkt->stream_index ]->codec;
    int i;

    if (!img->is_pipe) {
        if (av_get_frame_filename(filename, sizeof(filename),
                                  img->path, img->img_number) < 0 && img->img_number>1)
            return AVERROR(EIO);
        for(i=0; i<3; i++){
            if (url_fopen(&pb[i], filename, URL_WRONLY) < 0)
                return AVERROR(EIO);

            if(codec->codec_id != CODEC_ID_RAWVIDEO)
                break;
            filename[ strlen(filename) - 1 ]= 'U' + i;
        }
    } else {
        pb[0] = s->pb;
    }

    if(codec->codec_id == CODEC_ID_RAWVIDEO){
        int ysize = codec->width * codec->height;
        put_buffer(pb[0], pkt->data        , ysize);
        put_buffer(pb[1], pkt->data + ysize, (pkt->size - ysize)/2);
        put_buffer(pb[2], pkt->data + ysize +(pkt->size - ysize)/2, (pkt->size - ysize)/2);
        put_flush_packet(pb[1]);
        put_flush_packet(pb[2]);
        url_fclose(pb[1]);
        url_fclose(pb[2]);
    }else{
        put_buffer(pb[0], pkt->data, pkt->size);
    }
    put_flush_packet(pb[0]);
    if (!img->is_pipe) {
        url_fclose(pb[0]);
    }

    img->img_number++;
    return 0;
}

#endif /* CONFIG_MUXERS */

/* input */
#ifdef CONFIG_IMAGE2_DEMUXER
AVInputFormat image2_demuxer = {
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
#endif
#ifdef CONFIG_IMAGE2PIPE_DEMUXER
AVInputFormat image2pipe_demuxer = {
    "image2pipe",
    "piped image2 sequence",
    sizeof(VideoData),
    NULL, /* no probe */
    img_read_header,
    img_read_packet,
    img_read_close,
    NULL,
};
#endif

/* output */
#ifdef CONFIG_IMAGE2_MUXER
AVOutputFormat image2_muxer = {
    "image2",
    "image2 sequence",
    "",
    "",
    sizeof(VideoData),
    CODEC_ID_NONE,
    CODEC_ID_MJPEG,
    img_write_header,
    img_write_packet,
    NULL,
    AVFMT_NOFILE,
};
#endif
#ifdef CONFIG_IMAGE2PIPE_MUXER
AVOutputFormat image2pipe_muxer = {
    "image2pipe",
    "piped image2 sequence",
    "",
    "",
    sizeof(VideoData),
    CODEC_ID_NONE,
    CODEC_ID_MJPEG,
    img_write_header,
    img_write_packet,
};
#endif
