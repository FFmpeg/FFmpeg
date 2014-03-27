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
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"

typedef struct {
    const AVClass *class;  /**< Class for private options. */
    int img_number;
    int is_pipe;
    int split_planes;       /**< use independent file for each Y, U, V plane */
    char path[1024];
    int update;
    int use_strftime;
    const char *muxer;
} VideoMuxData;

static int write_header(AVFormatContext *s)
{
    VideoMuxData *img = s->priv_data;
    AVStream *st = s->streams[0];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(st->codec->pix_fmt);

    av_strlcpy(img->path, s->filename, sizeof(img->path));

    /* find format */
    if (s->oformat->flags & AVFMT_NOFILE)
        img->is_pipe = 0;
    else
        img->is_pipe = 1;

    if (st->codec->codec_id == AV_CODEC_ID_GIF) {
        img->muxer = "gif";
    } else if (st->codec->codec_id == AV_CODEC_ID_RAWVIDEO) {
        const char *str = strrchr(img->path, '.');
        img->split_planes =     str
                             && !av_strcasecmp(str + 1, "y")
                             && s->nb_streams == 1
                             && desc
                             &&(desc->flags & AV_PIX_FMT_FLAG_PLANAR)
                             && desc->nb_components >= 3;
    }
    return 0;
}

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    VideoMuxData *img = s->priv_data;
    AVIOContext *pb[4];
    char filename[1024];
    AVCodecContext *codec = s->streams[pkt->stream_index]->codec;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(codec->pix_fmt);
    int i;

    if (!img->is_pipe) {
        if (img->update) {
            av_strlcpy(filename, img->path, sizeof(filename));
        } else if (img->use_strftime) {
            time_t now0;
            struct tm *tm;
            time(&now0);
            tm = localtime(&now0);
            if (!strftime(filename, sizeof(filename), img->path, tm)) {
                av_log(s, AV_LOG_ERROR, "Could not get frame filename with strftime\n");
                return AVERROR(EINVAL);
            }
        } else if (av_get_frame_filename(filename, sizeof(filename), img->path, img->img_number) < 0 &&
                   img->img_number > 1) {
            av_log(s, AV_LOG_ERROR,
                   "Could not get frame filename number %d from pattern '%s' (either set updatefirst or use a pattern like %%03d within the filename pattern)\n",
                   img->img_number, img->path);
            return AVERROR(EINVAL);
        }
        for (i = 0; i < 4; i++) {
            if (avio_open2(&pb[i], filename, AVIO_FLAG_WRITE,
                           &s->interrupt_callback, NULL) < 0) {
                av_log(s, AV_LOG_ERROR, "Could not open file : %s\n", filename);
                return AVERROR(EIO);
            }

            if (!img->split_planes || i+1 >= desc->nb_components)
                break;
            filename[strlen(filename) - 1] = "UVAx"[i];
        }
    } else {
        pb[0] = s->pb;
    }

    if (img->split_planes) {
        int ysize = codec->width * codec->height;
        int usize = FF_CEIL_RSHIFT(codec->width, desc->log2_chroma_w) * FF_CEIL_RSHIFT(codec->height, desc->log2_chroma_h);
        if (desc->comp[0].depth_minus1 >= 8) {
            ysize *= 2;
            usize *= 2;
        }
        avio_write(pb[0], pkt->data                , ysize);
        avio_write(pb[1], pkt->data + ysize        , usize);
        avio_write(pb[2], pkt->data + ysize + usize, usize);
        avio_close(pb[1]);
        avio_close(pb[2]);
        if (desc->nb_components > 3) {
            avio_write(pb[3], pkt->data + ysize + 2*usize, ysize);
            avio_close(pb[3]);
        }
    } else if (img->muxer) {
        int ret;
        AVStream *st;
        AVPacket pkt2 = {0};
        AVFormatContext *fmt = NULL;

        av_assert0(!img->split_planes);

        ret = avformat_alloc_output_context2(&fmt, NULL, img->muxer, s->filename);
        if (ret < 0)
            return ret;
        st = avformat_new_stream(fmt, NULL);
        if (!st) {
            avformat_free_context(fmt);
            return AVERROR(ENOMEM);
        }
        st->id = pkt->stream_index;

        fmt->pb = pb[0];
        if ((ret = av_copy_packet(&pkt2, pkt))                            < 0 ||
            (ret = av_dup_packet(&pkt2))                                  < 0 ||
            (ret = avcodec_copy_context(st->codec, s->streams[0]->codec)) < 0 ||
            (ret = avformat_write_header(fmt, NULL))                      < 0 ||
            (ret = av_interleaved_write_frame(fmt, &pkt2))                < 0 ||
            (ret = av_write_trailer(fmt))                                 < 0) {
            av_free_packet(&pkt2);
            avformat_free_context(fmt);
            return ret;
        }
        av_free_packet(&pkt2);
        avformat_free_context(fmt);
    } else {
        avio_write(pb[0], pkt->data, pkt->size);
    }
    avio_flush(pb[0]);
    if (!img->is_pipe) {
        avio_close(pb[0]);
    }

    img->img_number++;
    return 0;
}

#define OFFSET(x) offsetof(VideoMuxData, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption muxoptions[] = {
    { "updatefirst",  "continuously overwrite one file", OFFSET(update),  AV_OPT_TYPE_INT, { .i64 = 0 }, 0,       1, ENC },
    { "update",       "continuously overwrite one file", OFFSET(update),  AV_OPT_TYPE_INT, { .i64 = 0 }, 0,       1, ENC },
    { "start_number", "set first number in the sequence", OFFSET(img_number), AV_OPT_TYPE_INT,  { .i64 = 1 }, 0, INT_MAX, ENC },
    { "strftime",     "use strftime for filename", OFFSET(use_strftime), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, ENC },
    { NULL },
};

#if CONFIG_IMAGE2_MUXER
static const AVClass img2mux_class = {
    .class_name = "image2 muxer",
    .item_name  = av_default_item_name,
    .option     = muxoptions,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_image2_muxer = {
    .name           = "image2",
    .long_name      = NULL_IF_CONFIG_SMALL("image2 sequence"),
    .extensions     = "bmp,dpx,jls,jpeg,jpg,ljpg,pam,pbm,pcx,pgm,pgmyuv,png,"
                      "ppm,sgi,tga,tif,tiff,jp2,j2c,j2k,xwd,sun,ras,rs,im1,im8,im24,"
                      "sunras,webp,xbm,xface,pix",
    .priv_data_size = sizeof(VideoMuxData),
    .video_codec    = AV_CODEC_ID_MJPEG,
    .write_header   = write_header,
    .write_packet   = write_packet,
    .flags          = AVFMT_NOTIMESTAMPS | AVFMT_NODIMENSIONS | AVFMT_NOFILE,
    .priv_class     = &img2mux_class,
};
#endif
#if CONFIG_IMAGE2PIPE_MUXER
AVOutputFormat ff_image2pipe_muxer = {
    .name           = "image2pipe",
    .long_name      = NULL_IF_CONFIG_SMALL("piped image2 sequence"),
    .priv_data_size = sizeof(VideoMuxData),
    .video_codec    = AV_CODEC_ID_MJPEG,
    .write_header   = write_header,
    .write_packet   = write_packet,
    .flags          = AVFMT_NOTIMESTAMPS | AVFMT_NODIMENSIONS
};
#endif
