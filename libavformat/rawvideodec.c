/*
 * RAW video demuxer
 * Copyright (c) 2001 Fabrice Bellard
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

#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "internal.h"
#include "avformat.h"

typedef struct RawVideoDemuxerContext {
    const AVClass *class;     /**< Class for private options. */
    int width, height;        /**< Integers describing video size, set by a private option. */
    char *pixel_format;       /**< Set by a private option. */
    AVRational framerate;     /**< AVRational describing framerate, set by a private option. */
} RawVideoDemuxerContext;


static int rawvideo_read_header(AVFormatContext *ctx)
{
    RawVideoDemuxerContext *s = ctx->priv_data;
    enum AVPixelFormat pix_fmt;
    AVStream *st;

    st = avformat_new_stream(ctx, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;

    st->codec->codec_id = ctx->iformat->raw_codec_id;

    if ((pix_fmt = av_get_pix_fmt(s->pixel_format)) == AV_PIX_FMT_NONE) {
        av_log(ctx, AV_LOG_ERROR, "No such pixel format: %s.\n",
               s->pixel_format);
        return AVERROR(EINVAL);
    }

    avpriv_set_pts_info(st, 64, s->framerate.den, s->framerate.num);

    st->codec->width  = s->width;
    st->codec->height = s->height;
    st->codec->pix_fmt = pix_fmt;
    st->codec->bit_rate = av_rescale_q(avpicture_get_size(st->codec->pix_fmt, s->width, s->height),
                                       (AVRational){8,1}, st->time_base);

    return 0;
}


static int rawvideo_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int packet_size, ret, width, height;
    AVStream *st = s->streams[0];

    width = st->codec->width;
    height = st->codec->height;

    packet_size = avpicture_get_size(st->codec->pix_fmt, width, height);
    if (packet_size < 0)
        return -1;

    ret = av_get_packet(s->pb, pkt, packet_size);
    pkt->pts = pkt->dts = pkt->pos / packet_size;

    pkt->stream_index = 0;
    if (ret < 0)
        return ret;
    return 0;
}

#define OFFSET(x) offsetof(RawVideoDemuxerContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption rawvideo_options[] = {
    { "video_size", "set frame size", OFFSET(width), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, DEC },
    { "pixel_format", "set pixel format", OFFSET(pixel_format), AV_OPT_TYPE_STRING, {.str = "yuv420p"}, 0, 0, DEC },
    { "framerate", "set frame rate", OFFSET(framerate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, 0, DEC },
    { NULL },
};

static const AVClass rawvideo_demuxer_class = {
    .class_name = "rawvideo demuxer",
    .item_name  = av_default_item_name,
    .option     = rawvideo_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_rawvideo_demuxer = {
    .name           = "rawvideo",
    .long_name      = NULL_IF_CONFIG_SMALL("raw video"),
    .priv_data_size = sizeof(RawVideoDemuxerContext),
    .read_header    = rawvideo_read_header,
    .read_packet    = rawvideo_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "yuv,cif,qcif,rgb",
    .raw_codec_id   = AV_CODEC_ID_RAWVIDEO,
    .priv_class     = &rawvideo_demuxer_class,
};
