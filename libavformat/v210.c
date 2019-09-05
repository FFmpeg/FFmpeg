/*
 * Raw v210 video demuxer
 * Copyright (c) 2015 Tiancheng "Timothy" Gu
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

#include "libavutil/imgutils.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "internal.h"
#include "avformat.h"

typedef struct V210DemuxerContext {
    const AVClass *class;     /**< Class for private options. */
    int width, height;        /**< Integers describing video size, set by a private option. */
    AVRational framerate;     /**< AVRational describing framerate, set by a private option. */
} V210DemuxerContext;

// v210 frame width is padded to multiples of 48
#define GET_PACKET_SIZE(w, h) (((w + 47) / 48) * 48 * h * 8 / 3)

static int v210_read_header(AVFormatContext *ctx)
{
    V210DemuxerContext *s = ctx->priv_data;
    AVStream *st;
    int ret;

    st = avformat_new_stream(ctx, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;

    st->codecpar->codec_id = ctx->iformat->raw_codec_id;

    avpriv_set_pts_info(st, 64, s->framerate.den, s->framerate.num);

    ret = av_image_check_size(s->width, s->height, 0, ctx);
    if (ret < 0)
        return ret;
    st->codecpar->width    = s->width;
    st->codecpar->height   = s->height;
    st->codecpar->format   = ctx->iformat->raw_codec_id == AV_CODEC_ID_V210 ?
                             AV_PIX_FMT_YUV422P10 : AV_PIX_FMT_YUV422P16;
    ctx->packet_size       = GET_PACKET_SIZE(s->width, s->height);
    st->codecpar->bit_rate = av_rescale_q(ctx->packet_size,
                                       (AVRational){8,1}, st->time_base);

    return 0;
}


static int v210_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    ret = av_get_packet(s->pb, pkt, s->packet_size);
    pkt->pts = pkt->dts = pkt->pos / s->packet_size;

    pkt->stream_index = 0;
    if (ret < 0)
        return ret;
    return 0;
}

#define OFFSET(x) offsetof(V210DemuxerContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption v210_options[] = {
    { "video_size", "set frame size", OFFSET(width), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, DEC },
    { "framerate", "set frame rate", OFFSET(framerate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, DEC },
    { NULL },
};

#if CONFIG_V210_DEMUXER
static const AVClass v210_demuxer_class = {
    .class_name = "v210 demuxer",
    .item_name  = av_default_item_name,
    .option     = v210_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_v210_demuxer = {
    .name           = "v210",
    .long_name      = NULL_IF_CONFIG_SMALL("Uncompressed 4:2:2 10-bit"),
    .priv_data_size = sizeof(V210DemuxerContext),
    .read_header    = v210_read_header,
    .read_packet    = v210_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "v210",
    .raw_codec_id   = AV_CODEC_ID_V210,
    .priv_class     = &v210_demuxer_class,
};
#endif // CONFIG_V210_DEMUXER

#if CONFIG_V210X_DEMUXER
static const AVClass v210x_demuxer_class = {
    .class_name = "v210x demuxer",
    .item_name  = av_default_item_name,
    .option     = v210_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_v210x_demuxer = {
    .name           = "v210x",
    .long_name      = NULL_IF_CONFIG_SMALL("Uncompressed 4:2:2 10-bit"),
    .priv_data_size = sizeof(V210DemuxerContext),
    .read_header    = v210_read_header,
    .read_packet    = v210_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "yuv10",
    .raw_codec_id   = AV_CODEC_ID_V210X,
    .priv_class     = &v210x_demuxer_class,
};
#endif // CONFIG_V210X_DEMUXER
