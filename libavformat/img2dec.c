/*
 * Image format
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 * Copyright (c) 2004 Michael Niedermayer
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/parseutils.h"
#include "avformat.h"
#include "internal.h"

typedef struct {
    const AVClass *class;  /**< Class for private options. */
    int img_first;
    int img_last;
    int img_number;
    int img_count;
    int is_pipe;
    char path[1024];
    char *pixel_format;     /**< Set by a private option. */
    char *video_size;       /**< Set by a private option. */
    char *framerate;        /**< Set by a private option. */
    int loop;
    int start_number;
} VideoDemuxData;

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

    for (i = 0; i < FF_ARRAY_ELEMS(sizes); i++) {
        if ((sizes[i][0] * sizes[i][1]) == size) {
            *width_ptr  = sizes[i][0];
            *height_ptr = sizes[i][1];
            return 0;
        }
    }

    return -1;
}

/* return -1 if no image found */
static int find_image_range(int *pfirst_index, int *plast_index,
                            const char *path, int max_start)
{
    char buf[1024];
    int range, last_index, range1, first_index;

    /* find the first image */
    for (first_index = 0; first_index < max_start; first_index++) {
        if (av_get_frame_filename(buf, sizeof(buf), path, first_index) < 0) {
            *pfirst_index =
            *plast_index  = 1;
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
    for (;;) {
        range = 0;
        for (;;) {
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
    *plast_index  = last_index;
    return 0;

fail:
    return -1;
}

static int img_read_probe(AVProbeData *p)
{
    if (p->filename && ff_guess_image2_codec(p->filename)) {
        if (av_filename_number_test(p->filename))
            return AVPROBE_SCORE_MAX;
        else
            return AVPROBE_SCORE_EXTENSION;
    }
    return 0;
}

static int img_read_header(AVFormatContext *s1)
{
    VideoDemuxData *s = s1->priv_data;
    int first_index, last_index, ret = 0;
    int width = 0, height = 0;
    AVStream *st;
    enum AVPixelFormat pix_fmt = AV_PIX_FMT_NONE;
    AVRational framerate;

    s1->ctx_flags |= AVFMTCTX_NOHEADER;

    st = avformat_new_stream(s1, NULL);
    if (!st) {
        return AVERROR(ENOMEM);
    }

    if (s->pixel_format &&
        (pix_fmt = av_get_pix_fmt(s->pixel_format)) == AV_PIX_FMT_NONE) {
        av_log(s1, AV_LOG_ERROR, "No such pixel format: %s.\n",
               s->pixel_format);
        return AVERROR(EINVAL);
    }
    if (s->video_size &&
        (ret = av_parse_video_size(&width, &height, s->video_size)) < 0) {
        av_log(s, AV_LOG_ERROR,
               "Could not parse video size: %s.\n", s->video_size);
        return ret;
    }
    if ((ret = av_parse_video_rate(&framerate, s->framerate)) < 0) {
        av_log(s, AV_LOG_ERROR,
               "Could not parse framerate: %s.\n", s->framerate);
        return ret;
    }

    av_strlcpy(s->path, s1->filename, sizeof(s->path));
    s->img_number = 0;
    s->img_count  = 0;

    /* find format */
    if (s1->iformat->flags & AVFMT_NOFILE)
        s->is_pipe = 0;
    else {
        s->is_pipe       = 1;
        st->need_parsing = AVSTREAM_PARSE_FULL;
    }

    avpriv_set_pts_info(st, 60, framerate.den, framerate.num);

    if (width && height) {
        st->codec->width  = width;
        st->codec->height = height;
    }

    if (!s->is_pipe) {
        if (find_image_range(&first_index, &last_index, s->path,
                             FFMAX(s->start_number, 5)) < 0)
            return AVERROR(ENOENT);
        s->img_first  = first_index;
        s->img_last   = last_index;
        s->img_number = first_index;
        /* compute duration */
        st->start_time = 0;
        st->duration   = last_index - first_index + 1;
    }

    if (s1->video_codec_id) {
        st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codec->codec_id   = s1->video_codec_id;
    } else if (s1->audio_codec_id) {
        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codec->codec_id   = s1->audio_codec_id;
    } else {
        st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codec->codec_id   = ff_guess_image2_codec(s->path);
    }
    if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
        pix_fmt != AV_PIX_FMT_NONE)
        st->codec->pix_fmt = pix_fmt;

    return 0;
}

static int img_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    VideoDemuxData *s = s1->priv_data;
    char filename[1024];
    int i;
    int size[3]           = { 0 }, ret[3] = { 0 };
    AVIOContext *f[3]     = { NULL };
    AVCodecContext *codec = s1->streams[0]->codec;

    if (!s->is_pipe) {
        /* loop over input */
        if (s->loop && s->img_number > s->img_last) {
            s->img_number = s->img_first;
        }
        if (s->img_number > s->img_last)
            return AVERROR_EOF;
        if (av_get_frame_filename(filename, sizeof(filename),
                                  s->path,
                                  s->img_number) < 0 && s->img_number > 1)
            return AVERROR(EIO);
        for (i = 0; i < 3; i++) {
            if (avio_open2(&f[i], filename, AVIO_FLAG_READ,
                           &s1->interrupt_callback, NULL) < 0) {
                if (i >= 1)
                    break;
                av_log(s1, AV_LOG_ERROR, "Could not open file : %s\n",
                       filename);
                return AVERROR(EIO);
            }
            size[i] = avio_size(f[i]);

            if (codec->codec_id != AV_CODEC_ID_RAWVIDEO)
                break;
            filename[strlen(filename) - 1] = 'U' + i;
        }

        if (codec->codec_id == AV_CODEC_ID_RAWVIDEO && !codec->width)
            infer_size(&codec->width, &codec->height, size[0]);
    } else {
        f[0] = s1->pb;
        if (f[0]->eof_reached)
            return AVERROR(EIO);
        size[0] = 4096;
    }

    av_new_packet(pkt, size[0] + size[1] + size[2]);
    pkt->stream_index = 0;
    pkt->flags       |= AV_PKT_FLAG_KEY;

    pkt->size = 0;
    for (i = 0; i < 3; i++) {
        if (f[i]) {
            ret[i] = avio_read(f[i], pkt->data + pkt->size, size[i]);
            if (!s->is_pipe)
                avio_close(f[i]);
            if (ret[i] > 0)
                pkt->size += ret[i];
        }
    }

    if (ret[0] <= 0 || ret[1] < 0 || ret[2] < 0) {
        av_free_packet(pkt);
        return AVERROR(EIO); /* signal EOF */
    } else {
        s->img_count++;
        s->img_number++;
        return 0;
    }
}

#define OFFSET(x) offsetof(VideoDemuxData, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "pixel_format", "",                             OFFSET(pixel_format), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0,       DEC },
    { "video_size",   "",                             OFFSET(video_size),   AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0,       DEC },
    { "framerate",    "",                             OFFSET(framerate),    AV_OPT_TYPE_STRING, { .str = "25" }, 0, 0,       DEC },
    { "loop",         "",                             OFFSET(loop),         AV_OPT_TYPE_INT,    { .i64 = 0    }, 0, 1,       DEC },
    { "start_number", "first number in the sequence", OFFSET(start_number), AV_OPT_TYPE_INT,    { .i64 = 1    }, 1, INT_MAX, DEC },
    { NULL },
};

#if CONFIG_IMAGE2_DEMUXER
static const AVClass img2_class = {
    .class_name = "image2 demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};
AVInputFormat ff_image2_demuxer = {
    .name           = "image2",
    .long_name      = NULL_IF_CONFIG_SMALL("image2 sequence"),
    .priv_data_size = sizeof(VideoDemuxData),
    .read_probe     = img_read_probe,
    .read_header    = img_read_header,
    .read_packet    = img_read_packet,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &img2_class,
};
#endif
#if CONFIG_IMAGE2PIPE_DEMUXER
static const AVClass img2pipe_class = {
    .class_name = "image2pipe demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};
AVInputFormat ff_image2pipe_demuxer = {
    .name           = "image2pipe",
    .long_name      = NULL_IF_CONFIG_SMALL("piped image2 sequence"),
    .priv_data_size = sizeof(VideoDemuxData),
    .read_header    = img_read_header,
    .read_packet    = img_read_packet,
    .priv_class     = &img2pipe_class,
};
#endif
