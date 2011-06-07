/*
 * Image format
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#include "libavutil/intreadwrite.h"
#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/parseutils.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include <strings.h>

typedef struct {
    const AVClass *class;  /**< Class for private options. */
    int img_first;
    int img_last;
    int img_number;
    int img_count;
    int is_pipe;
    int split_planes;  /**< use independent file for each Y, U, V plane */
    char path[1024];
    char *pixel_format;     /**< Set by a private option. */
    char *video_size;       /**< Set by a private option. */
    char *framerate;        /**< Set by a private option. */
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
    { CODEC_ID_PPM       , "pnm"},
    { CODEC_ID_PGM       , "pgm"},
    { CODEC_ID_PGMYUV    , "pgmyuv"},
    { CODEC_ID_PBM       , "pbm"},
    { CODEC_ID_PAM       , "pam"},
    { CODEC_ID_MPEG1VIDEO, "mpg1-img"},
    { CODEC_ID_MPEG2VIDEO, "mpg2-img"},
    { CODEC_ID_MPEG4     , "mpg4-img"},
    { CODEC_ID_FFV1      , "ffv1-img"},
    { CODEC_ID_RAWVIDEO  , "y"},
    { CODEC_ID_RAWVIDEO  , "raw"},
    { CODEC_ID_BMP       , "bmp"},
    { CODEC_ID_GIF       , "gif"},
    { CODEC_ID_TARGA     , "tga"},
    { CODEC_ID_TIFF      , "tiff"},
    { CODEC_ID_TIFF      , "tif"},
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
    { CODEC_ID_JPEG2000  , "j2k"},
    { CODEC_ID_JPEG2000  , "jp2"},
    { CODEC_ID_JPEG2000  , "jpc"},
    { CODEC_ID_DPX       , "dpx"},
    { CODEC_ID_PICTOR    , "pic"},
    { CODEC_ID_NONE      , NULL}
};

static const int sizes[][2] = {
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

    for(i=0;i<FF_ARRAY_ELEMS(sizes);i++) {
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
        if (!strcasecmp(str, tags->str))
            return tags->id;

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
            if (avio_check(buf, AVIO_FLAG_READ) > 0)
                return 0;
            return -1;
        }
        if (avio_check(buf, AVIO_FLAG_READ) > 0)
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
            if (avio_check(buf, AVIO_FLAG_READ) <= 0)
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


static int read_probe(AVProbeData *p)
{
    if (p->filename && av_str2id(img_tags, p->filename)) {
        if (av_filename_number_test(p->filename))
            return AVPROBE_SCORE_MAX;
        else
            return AVPROBE_SCORE_MAX/2;
    }
    return 0;
}

enum CodecID ff_guess_image2_codec(const char *filename)
{
    return av_str2id(img_tags, filename);
}

#if FF_API_GUESS_IMG2_CODEC
enum CodecID av_guess_image2_codec(const char *filename){
    return av_str2id(img_tags, filename);
}
#endif

static int read_header(AVFormatContext *s1, AVFormatParameters *ap)
{
    VideoData *s = s1->priv_data;
    int first_index, last_index, ret = 0;
    int width = 0, height = 0;
    AVStream *st;
    enum PixelFormat pix_fmt = PIX_FMT_NONE;
    AVRational framerate;

    s1->ctx_flags |= AVFMTCTX_NOHEADER;

    st = av_new_stream(s1, 0);
    if (!st) {
        return AVERROR(ENOMEM);
    }

    if (s->pixel_format && (pix_fmt = av_get_pix_fmt(s->pixel_format)) == PIX_FMT_NONE) {
        av_log(s1, AV_LOG_ERROR, "No such pixel format: %s.\n", s->pixel_format);
        return AVERROR(EINVAL);
    }
    if (s->video_size && (ret = av_parse_video_size(&width, &height, s->video_size)) < 0) {
        av_log(s, AV_LOG_ERROR, "Could not parse video size: %s.\n", s->video_size);
        return ret;
    }
    if ((ret = av_parse_video_rate(&framerate, s->framerate)) < 0) {
        av_log(s, AV_LOG_ERROR, "Could not parse framerate: %s.\n", s->framerate);
        return ret;
    }
#if FF_API_FORMAT_PARAMETERS
    if (ap->pix_fmt != PIX_FMT_NONE)
        pix_fmt = ap->pix_fmt;
    if (ap->width > 0)
        width = ap->width;
    if (ap->height > 0)
        height = ap->height;
    if (ap->time_base.num)
        framerate = (AVRational){ap->time_base.den, ap->time_base.num};
#endif

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

    av_set_pts_info(st, 60, framerate.den, framerate.num);

    if (width && height) {
        st->codec->width  = width;
        st->codec->height = height;
    }

    if (!s->is_pipe) {
        if (find_image_range(&first_index, &last_index, s->path) < 0)
            return AVERROR(ENOENT);
        s->img_first = first_index;
        s->img_last = last_index;
        s->img_number = first_index;
        /* compute duration */
        st->start_time = 0;
        st->duration = last_index - first_index + 1;
    }

    if(s1->video_codec_id){
        st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codec->codec_id = s1->video_codec_id;
    }else if(s1->audio_codec_id){
        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codec->codec_id = s1->audio_codec_id;
    }else{
        const char *str= strrchr(s->path, '.');
        s->split_planes = str && !strcasecmp(str + 1, "y");
        st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codec->codec_id = av_str2id(img_tags, s->path);
    }
    if(st->codec->codec_type == AVMEDIA_TYPE_VIDEO && pix_fmt != PIX_FMT_NONE)
        st->codec->pix_fmt = pix_fmt;

    return 0;
}

static int read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    VideoData *s = s1->priv_data;
    char filename[1024];
    int i;
    int size[3]={0}, ret[3]={0};
    AVIOContext *f[3];
    AVCodecContext *codec= s1->streams[0]->codec;

    if (!s->is_pipe) {
        /* loop over input */
        if (s1->loop_input && s->img_number > s->img_last) {
            s->img_number = s->img_first;
        }
        if (s->img_number > s->img_last)
            return AVERROR_EOF;
        if (av_get_frame_filename(filename, sizeof(filename),
                                  s->path, s->img_number)<0 && s->img_number > 1)
            return AVERROR(EIO);
        for(i=0; i<3; i++){
            if (avio_open(&f[i], filename, AVIO_FLAG_READ) < 0) {
                if(i==1)
                    break;
                av_log(s1, AV_LOG_ERROR, "Could not open file : %s\n",filename);
                return AVERROR(EIO);
            }
            size[i]= avio_size(f[i]);

            if(!s->split_planes)
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
    pkt->flags |= AV_PKT_FLAG_KEY;

    pkt->size= 0;
    for(i=0; i<3; i++){
        if(size[i]){
            ret[i]= avio_read(f[i], pkt->data + pkt->size, size[i]);
            if (!s->is_pipe)
                avio_close(f[i]);
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

#if CONFIG_IMAGE2_MUXER || CONFIG_IMAGE2PIPE_MUXER
/******************************************************/
/* image output */

static int write_header(AVFormatContext *s)
{
    VideoData *img = s->priv_data;
    const char *str;

    img->img_number = 1;
    av_strlcpy(img->path, s->filename, sizeof(img->path));

    /* find format */
    if (s->oformat->flags & AVFMT_NOFILE)
        img->is_pipe = 0;
    else
        img->is_pipe = 1;

    str = strrchr(img->path, '.');
    img->split_planes = str && !strcasecmp(str + 1, "y");
    return 0;
}

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    VideoData *img = s->priv_data;
    AVIOContext *pb[3];
    char filename[1024];
    AVCodecContext *codec= s->streams[ pkt->stream_index ]->codec;
    int i;

    if (!img->is_pipe) {
        if (av_get_frame_filename(filename, sizeof(filename),
                                  img->path, img->img_number) < 0 && img->img_number>1) {
            av_log(s, AV_LOG_ERROR,
                   "Could not get frame filename number %d from pattern '%s'\n",
                   img->img_number, img->path);
            return AVERROR(EINVAL);
        }
        for(i=0; i<3; i++){
            if (avio_open(&pb[i], filename, AVIO_FLAG_WRITE) < 0) {
                av_log(s, AV_LOG_ERROR, "Could not open file : %s\n",filename);
                return AVERROR(EIO);
            }

            if(!img->split_planes)
                break;
            filename[ strlen(filename) - 1 ]= 'U' + i;
        }
    } else {
        pb[0] = s->pb;
    }

    if(img->split_planes){
        int ysize = codec->width * codec->height;
        avio_write(pb[0], pkt->data        , ysize);
        avio_write(pb[1], pkt->data + ysize, (pkt->size - ysize)/2);
        avio_write(pb[2], pkt->data + ysize +(pkt->size - ysize)/2, (pkt->size - ysize)/2);
        avio_flush(pb[1]);
        avio_flush(pb[2]);
        avio_close(pb[1]);
        avio_close(pb[2]);
    }else{
        if(av_str2id(img_tags, s->filename) == CODEC_ID_JPEG2000){
            AVStream *st = s->streams[0];
            if(st->codec->extradata_size > 8 &&
               AV_RL32(st->codec->extradata+4) == MKTAG('j','p','2','h')){
                if(pkt->size < 8 || AV_RL32(pkt->data+4) != MKTAG('j','p','2','c'))
                    goto error;
                avio_wb32(pb[0], 12);
                ffio_wfourcc(pb[0], "jP  ");
                avio_wb32(pb[0], 0x0D0A870A); // signature
                avio_wb32(pb[0], 20);
                ffio_wfourcc(pb[0], "ftyp");
                ffio_wfourcc(pb[0], "jp2 ");
                avio_wb32(pb[0], 0);
                ffio_wfourcc(pb[0], "jp2 ");
                avio_write(pb[0], st->codec->extradata, st->codec->extradata_size);
            }else if(pkt->size < 8 ||
                     (!st->codec->extradata_size &&
                      AV_RL32(pkt->data+4) != MKTAG('j','P',' ',' '))){ // signature
            error:
                av_log(s, AV_LOG_ERROR, "malformated jpeg2000 codestream\n");
                return -1;
            }
        }
        avio_write(pb[0], pkt->data, pkt->size);
    }
    avio_flush(pb[0]);
    if (!img->is_pipe) {
        avio_close(pb[0]);
    }

    img->img_number++;
    return 0;
}

#endif /* CONFIG_IMAGE2_MUXER || CONFIG_IMAGE2PIPE_MUXER */

#define OFFSET(x) offsetof(VideoData, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "pixel_format", "", OFFSET(pixel_format), FF_OPT_TYPE_STRING, {.str = NULL}, 0, 0, DEC },
    { "video_size",   "", OFFSET(video_size),   FF_OPT_TYPE_STRING, {.str = NULL}, 0, 0, DEC },
    { "framerate",    "", OFFSET(framerate),    FF_OPT_TYPE_STRING, {.str = "25"}, 0, 0, DEC },
    { NULL },
};

static const AVClass img2_class = {
    .class_name = "image2 demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

/* input */
#if CONFIG_IMAGE2_DEMUXER
AVInputFormat ff_image2_demuxer = {
    .name           = "image2",
    .long_name      = NULL_IF_CONFIG_SMALL("image2 sequence"),
    .priv_data_size = sizeof(VideoData),
    .read_probe     = read_probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &img2_class,
};
#endif
#if CONFIG_IMAGE2PIPE_DEMUXER
AVInputFormat ff_image2pipe_demuxer = {
    .name           = "image2pipe",
    .long_name      = NULL_IF_CONFIG_SMALL("piped image2 sequence"),
    .priv_data_size = sizeof(VideoData),
    .read_header    = read_header,
    .read_packet    = read_packet,
    .priv_class     = &img2_class,
};
#endif

/* output */
#if CONFIG_IMAGE2_MUXER
AVOutputFormat ff_image2_muxer = {
    .name           = "image2",
    .long_name      = NULL_IF_CONFIG_SMALL("image2 sequence"),
    .extensions     = "bmp,dpx,jpeg,jpg,ljpg,pam,pbm,pcx,pgm,pgmyuv,png,"
                      "ppm,sgi,tga,tif,tiff,jp2",
    .priv_data_size = sizeof(VideoData),
    .video_codec    = CODEC_ID_MJPEG,
    .write_header   = write_header,
    .write_packet   = write_packet,
    .flags          = AVFMT_NOTIMESTAMPS | AVFMT_NODIMENSIONS | AVFMT_NOFILE
};
#endif
#if CONFIG_IMAGE2PIPE_MUXER
AVOutputFormat ff_image2pipe_muxer = {
    .name           = "image2pipe",
    .long_name      = NULL_IF_CONFIG_SMALL("piped image2 sequence"),
    .priv_data_size = sizeof(VideoData),
    .video_codec    = CODEC_ID_MJPEG,
    .write_header   = write_header,
    .write_packet   = write_packet,
    .flags          = AVFMT_NOTIMESTAMPS | AVFMT_NODIMENSIONS
};
#endif
