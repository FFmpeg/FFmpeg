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

#include <time.h>

#include "config_components.h"

#include "libavutil/intreadwrite.h"
#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time_internal.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "img2.h"
#include "mux.h"

typedef struct VideoMuxData {
    const AVClass *class;  /**< Class for private options. */
    int start_img_number;
    int img_number;
    int split_planes;       /**< use independent file for each Y, U, V plane */
    char tmp[4][1024];
    char target[4][1024];
    int update;
    int use_strftime;
    int frame_pts;
    const char *muxer;
    int use_rename;
    AVDictionary *protocol_opts;
} VideoMuxData;

static int write_header(AVFormatContext *s)
{
    VideoMuxData *img = s->priv_data;
    AVStream *st = s->streams[0];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(st->codecpar->format);

    if (st->codecpar->codec_id == AV_CODEC_ID_GIF) {
        img->muxer = "gif";
    } else if (st->codecpar->codec_id == AV_CODEC_ID_FITS) {
        img->muxer = "fits";
    } else if (st->codecpar->codec_id == AV_CODEC_ID_AV1) {
        img->muxer = "avif";
    } else if (st->codecpar->codec_id == AV_CODEC_ID_RAWVIDEO) {
        const char *str = strrchr(s->url, '.');
        img->split_planes =     str
                             && !av_strcasecmp(str + 1, "y")
                             && s->nb_streams == 1
                             && desc
                             &&(desc->flags & AV_PIX_FMT_FLAG_PLANAR)
                             && desc->nb_components >= 3;
    }
    img->img_number = img->start_img_number;

    return 0;
}

static int write_muxed_file(AVFormatContext *s, AVIOContext *pb, AVPacket *pkt)
{
    VideoMuxData *img = s->priv_data;
    AVCodecParameters *par = s->streams[pkt->stream_index]->codecpar;
    AVStream *st;
    AVPacket *const pkt2 = ffformatcontext(s)->pkt;
    AVFormatContext *fmt = NULL;
    int ret;

    /* URL is not used directly as we are overriding the IO context later. */
    ret = avformat_alloc_output_context2(&fmt, NULL, img->muxer, s->url);
    if (ret < 0)
        return ret;
    st = avformat_new_stream(fmt, NULL);
    if (!st) {
        ret = AVERROR(ENOMEM);
        goto out;
    }
    st->id = pkt->stream_index;

    fmt->pb = pb;

    ret = av_packet_ref(pkt2, pkt);
    if (ret < 0)
        goto out;
    pkt2->stream_index = 0;

    if ((ret = avcodec_parameters_copy(st->codecpar, par))     < 0 ||
        (ret = avformat_write_header(fmt, NULL))               < 0 ||
        (ret = av_interleaved_write_frame(fmt, pkt2))         < 0 ||
        (ret = av_write_trailer(fmt))) {}

    av_packet_unref(pkt2);
out:
    avformat_free_context(fmt);
    return ret;
}

static int write_packet_pipe(AVFormatContext *s, AVPacket *pkt)
{
    VideoMuxData *img = s->priv_data;
    if (img->muxer) {
        int ret = write_muxed_file(s, s->pb, pkt);
        if (ret < 0)
            return ret;
    } else {
        avio_write(s->pb, pkt->data, pkt->size);
    }
    img->img_number++;
    return 0;
}

static int write_and_close(AVFormatContext *s, AVIOContext **pb, const unsigned char *buf, int size)
{
    avio_write(*pb, buf, size);
    avio_flush(*pb);
    return ff_format_io_close(s, pb);
}

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    VideoMuxData *img = s->priv_data;
    AVIOContext *pb[4] = {0};
    char filename[1024];
    AVCodecParameters *par = s->streams[pkt->stream_index]->codecpar;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(par->format);
    int ret, i;
    int nb_renames = 0;
    AVDictionary *options = NULL;

    if (img->update) {
        av_strlcpy(filename, s->url, sizeof(filename));
    } else if (img->use_strftime) {
        time_t now0;
        struct tm *tm, tmpbuf;
        time(&now0);
        tm = localtime_r(&now0, &tmpbuf);
        if (!strftime(filename, sizeof(filename), s->url, tm)) {
            av_log(s, AV_LOG_ERROR, "Could not get frame filename with strftime\n");
            return AVERROR(EINVAL);
        }
    } else if (img->frame_pts) {
        if (ff_get_frame_filename(filename, sizeof(filename), s->url, pkt->pts, AV_FRAME_FILENAME_FLAGS_MULTIPLE) < 0) {
            av_log(s, AV_LOG_ERROR, "Cannot write filename by pts of the frames.");
            return AVERROR(EINVAL);
        }
    } else if (ff_get_frame_filename(filename, sizeof(filename), s->url,
                                     img->img_number,
                                     AV_FRAME_FILENAME_FLAGS_MULTIPLE) < 0) {
        if (img->img_number == img->start_img_number) {
            av_log(s, AV_LOG_WARNING, "The specified filename '%s' does not contain an image sequence pattern or a pattern is invalid.\n", s->url);
            av_log(s, AV_LOG_WARNING,
                   "Use a pattern such as %%03d for an image sequence or "
                   "use the -update option (with -frames:v 1 if needed) to write a single image.\n");
            av_strlcpy(filename, s->url, sizeof(filename));
        } else {
            av_log(s, AV_LOG_ERROR, "Cannot write more than one file with the same name. Are you missing the -update option or a sequence pattern?\n");
            return AVERROR(EINVAL);
        }
    }
    for (i = 0; i < 4; i++) {
        av_dict_copy(&options, img->protocol_opts, 0);
        snprintf(img->tmp[i], sizeof(img->tmp[i]), "%s.tmp", filename);
        av_strlcpy(img->target[i], filename, sizeof(img->target[i]));
        if (s->io_open(s, &pb[i], img->use_rename ? img->tmp[i] : filename, AVIO_FLAG_WRITE, &options) < 0) {
            av_log(s, AV_LOG_ERROR, "Could not open file : %s\n", img->use_rename ? img->tmp[i] : filename);
            ret = AVERROR(EIO);
            goto fail;
        }
        if (options) {
            av_log(s, AV_LOG_ERROR, "Could not recognize some protocol options\n");
            ret = AVERROR(EINVAL);
            goto fail;
        }

        if (!img->split_planes || i+1 >= desc->nb_components)
            break;
        filename[strlen(filename) - 1] = "UVAx"[i];
    }
    if (img->use_rename)
        nb_renames = i + 1;

    if (img->split_planes) {
        int ysize = par->width * par->height;
        int usize = AV_CEIL_RSHIFT(par->width, desc->log2_chroma_w) * AV_CEIL_RSHIFT(par->height, desc->log2_chroma_h);
        if (desc->comp[0].depth >= 9) {
            ysize *= 2;
            usize *= 2;
        }
        if ((ret = write_and_close(s, &pb[0], pkt->data                , ysize)) < 0 ||
            (ret = write_and_close(s, &pb[1], pkt->data + ysize        , usize)) < 0 ||
            (ret = write_and_close(s, &pb[2], pkt->data + ysize + usize, usize)) < 0)
            goto fail;
        if (desc->nb_components > 3)
            ret = write_and_close(s, &pb[3], pkt->data + ysize + 2*usize, ysize);
    } else if (img->muxer) {
        if ((ret = write_muxed_file(s, pb[0], pkt)) < 0)
            goto fail;
        ret = ff_format_io_close(s, &pb[0]);
    } else {
        ret = write_and_close(s, &pb[0], pkt->data, pkt->size);
    }
    if (ret < 0)
        goto fail;

    for (i = 0; i < nb_renames; i++) {
        int ret = ff_rename(img->tmp[i], img->target[i], s);
        if (ret < 0)
            return ret;
    }

    img->img_number++;
    return 0;

fail:
    av_dict_free(&options);
    for (i = 0; i < FF_ARRAY_ELEMS(pb); i++)
        if (pb[i])
            ff_format_io_close(s, &pb[i]);
    return ret;
}

static int query_codec(enum AVCodecID id, int std_compliance)
{
    int i;
    for (i = 0; ff_img_tags[i].id != AV_CODEC_ID_NONE; i++)
        if (ff_img_tags[i].id == id)
            return 1;

    // Anything really can be stored in img2
    return std_compliance < FF_COMPLIANCE_NORMAL;
}

#define OFFSET(x) offsetof(VideoMuxData, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption muxoptions[] = {
    { "update",       "continuously overwrite one file", OFFSET(update),  AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0,       1, ENC },
    { "start_number", "set first number in the sequence", OFFSET(start_img_number), AV_OPT_TYPE_INT,  { .i64 = 1 }, 0, INT_MAX, ENC },
    { "strftime",     "use strftime for filename", OFFSET(use_strftime),  AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, ENC },
    { "frame_pts",    "use current frame pts for filename", OFFSET(frame_pts),  AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, ENC },
    { "atomic_writing", "write files atomically (using temporary files and renames)", OFFSET(use_rename), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, ENC },
    { "protocol_opts", "specify protocol options for the opened files", OFFSET(protocol_opts), AV_OPT_TYPE_DICT, {0}, 0, 0, ENC },
    { NULL },
};

#if CONFIG_IMAGE2_MUXER
static const AVClass img2mux_class = {
    .class_name = "image2 muxer",
    .item_name  = av_default_item_name,
    .option     = muxoptions,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFOutputFormat ff_image2_muxer = {
    .p.name         = "image2",
    .p.long_name    = NULL_IF_CONFIG_SMALL("image2 sequence"),
    .p.extensions   = "bmp,dpx,exr,jls,jpeg,jpg,jxl,ljpg,pam,pbm,pcx,pfm,pgm,pgmyuv,phm,"
                      "png,ppm,sgi,tga,tif,tiff,jp2,j2c,j2k,xwd,sun,ras,rs,im1,im8,"
                      "im24,sunras,vbn,xbm,xface,pix,y,avif,qoi,hdr,wbmp",
    .priv_data_size = sizeof(VideoMuxData),
    .p.video_codec  = AV_CODEC_ID_MJPEG,
    .write_header   = write_header,
    .write_packet   = write_packet,
    .query_codec    = query_codec,
    .p.flags        = AVFMT_NOTIMESTAMPS | AVFMT_NODIMENSIONS | AVFMT_NOFILE,
    .p.priv_class   = &img2mux_class,
};
#endif
#if CONFIG_IMAGE2PIPE_MUXER
const FFOutputFormat ff_image2pipe_muxer = {
    .p.name         = "image2pipe",
    .p.long_name    = NULL_IF_CONFIG_SMALL("piped image2 sequence"),
    .priv_data_size = sizeof(VideoMuxData),
    .p.video_codec  = AV_CODEC_ID_MJPEG,
    .write_header   = write_header,
    .write_packet   = write_packet_pipe,
    .query_codec    = query_codec,
    .p.flags        = AVFMT_NOTIMESTAMPS | AVFMT_NODIMENSIONS
};
#endif
