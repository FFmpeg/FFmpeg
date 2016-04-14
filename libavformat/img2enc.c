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

#include "libavutil/intreadwrite.h"
#include "libavutil/avstring.h"
#include "libavutil/log.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "libavutil/opt.h"

typedef struct RenameIO {
    AVIOContext *pb;
    char filename[1024];
    char tmp[1024];
} RenameIO;

typedef struct VideoMuxData {
    const AVClass *class;  /**< Class for private options. */
    int img_number;
    int is_pipe;
    char path[1024];
    int update;
} VideoMuxData;

static int write_header(AVFormatContext *s)
{
    VideoMuxData *img = s->priv_data;

    av_strlcpy(img->path, s->filename, sizeof(img->path));

    /* find format */
    if (s->oformat->flags & AVFMT_NOFILE)
        img->is_pipe = 0;
    else
        img->is_pipe = 1;

    return 0;
}

static int open_temporary(AVFormatContext *s, RenameIO *out, const char *filename)
{
    snprintf(out->tmp, sizeof(out->tmp), "%s.tmp", filename);
    av_strlcpy(out->filename, filename, sizeof(out->filename));
    if (s->io_open(s, &out->pb, out->tmp, AVIO_FLAG_WRITE, NULL) < 0) {
        av_log(s, AV_LOG_ERROR, "Could not open file : %s\n", out->tmp);
        return AVERROR(EIO);
    }

    return 0;
}

static void close_and_rename(AVFormatContext *s, RenameIO *out)
{
    ff_format_io_close(s, &out->pb);
    ff_rename(out->tmp, out->filename);
}

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    VideoMuxData *img = s->priv_data;
    RenameIO out[3];
    char filename[1024];
    AVCodecParameters *par = s->streams[pkt->stream_index]->codecpar;
    int i;

    if (!img->is_pipe) {
        if (img->update) {
            av_strlcpy(filename, img->path, sizeof(filename));
        } else if (av_get_frame_filename(filename, sizeof(filename), img->path, img->img_number) < 0 &&
                   img->img_number > 1) {
            av_log(s, AV_LOG_ERROR,
                   "Could not get frame filename number %d from pattern '%s'\n",
                   img->img_number, img->path);
            return AVERROR(EIO);
        }
        for (i = 0; i < 3; i++) {
            int ret = open_temporary(s, &out[i], filename);
            if (ret < 0)
                return ret;
            if (par->codec_id != AV_CODEC_ID_RAWVIDEO)
                break;
            filename[strlen(filename) - 1] = 'U' + i;
        }
    } else {
        out[0].pb = s->pb;
    }

    if (par->codec_id == AV_CODEC_ID_RAWVIDEO) {
        int ysize = par->width * par->height;
        avio_write(out[0].pb, pkt->data, ysize);
        avio_write(out[1].pb, pkt->data + ysize,                           (pkt->size - ysize) / 2);
        avio_write(out[2].pb, pkt->data + ysize + (pkt->size - ysize) / 2, (pkt->size - ysize) / 2);
        close_and_rename(s, &out[1]);
        close_and_rename(s, &out[2]);
    } else {
        if (ff_guess_image2_codec(s->filename) == AV_CODEC_ID_JPEG2000) {
            AVStream *st = s->streams[0];
            if (st->codecpar->extradata_size > 8 &&
                AV_RL32(st->codecpar->extradata + 4) == MKTAG('j', 'p', '2', 'h')) {
                if (pkt->size < 8 ||
                    AV_RL32(pkt->data + 4) != MKTAG('j', 'p', '2', 'c'))
                    goto error;
                avio_wb32(out[0].pb, 12);
                ffio_wfourcc(out[0].pb, "jP  ");
                avio_wb32(out[0].pb, 0x0D0A870A); // signature
                avio_wb32(out[0].pb, 20);
                ffio_wfourcc(out[0].pb, "ftyp");
                ffio_wfourcc(out[0].pb, "jp2 ");
                avio_wb32(out[0].pb, 0);
                ffio_wfourcc(out[0].pb, "jp2 ");
                avio_write(out[0].pb, st->codecpar->extradata, st->codecpar->extradata_size);
            } else if (pkt->size < 8 ||
                       (!st->codecpar->extradata_size &&
                        AV_RL32(pkt->data + 4) != MKTAG('j', 'P', ' ', ' '))) { // signature
error:
                av_log(s, AV_LOG_ERROR, "malformed JPEG 2000 codestream\n");
                return -1;
            }
        }
        avio_write(out[0].pb, pkt->data, pkt->size);
    }
    avio_flush(out[0].pb);
    if (!img->is_pipe) {
        close_and_rename(s, &out[0]);
    }

    img->img_number++;
    return 0;
}

#define OFFSET(x) offsetof(VideoMuxData, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption muxoptions[] = {
    { "start_number", "first number in the sequence", OFFSET(img_number), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, INT_MAX, ENC },
    { "update",       "continuously overwrite one file", OFFSET(update),  AV_OPT_TYPE_INT, { .i64 = 0 }, 0,       1, ENC },
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
    .extensions     = "bmp,dpx,jpeg,jpg,ljpg,pam,pbm,pcx,pgm,pgmyuv,png,"
                      "ppm,sgi,tga,tif,tiff,jp2,xwd,sun,ras,rs,im1,im8,im24,"
                      "sunras,webp,xbm,j2c,pix",
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
