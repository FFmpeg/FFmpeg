/*
 * SER demuxer
 * Copyright (c) 2018 Paul B Mahol
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

#define SER_MAGIC "LUCAM-RECORDER"

typedef struct SERDemuxerContext {
    const AVClass *class;
    int width, height;
    AVRational framerate;
    int64_t end;
} SERDemuxerContext;

static int ser_probe(const AVProbeData *pd)
{
    if (memcmp(pd->buf, SER_MAGIC, 14) == 0)
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int ser_read_header(AVFormatContext *s)
{
    SERDemuxerContext *ser = s->priv_data;
    enum AVPixelFormat pix_fmt;
    int depth, color_id, endian;
    int packet_size;
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(s->pb, 14);
    avio_skip(s->pb, 4);
    color_id = avio_rl32(s->pb);
    endian = avio_rl32(s->pb);
    ser->width = avio_rl32(s->pb);
    ser->height = avio_rl32(s->pb);
    depth = avio_rl32(s->pb);
    st->nb_frames = st->duration = avio_rl32(s->pb);
    avio_skip(s->pb, 120);
    avio_skip(s->pb, 8);
    avio_skip(s->pb, 8);

    switch (color_id) {
    case   0: pix_fmt = depth <= 8 ? AV_PIX_FMT_GRAY8       : endian ? AV_PIX_FMT_GRAY16BE       : AV_PIX_FMT_GRAY16LE;       break;
    case   8: pix_fmt = depth <= 8 ? AV_PIX_FMT_BAYER_RGGB8 : endian ? AV_PIX_FMT_BAYER_RGGB16BE : AV_PIX_FMT_BAYER_RGGB16LE; break;
    case   9: pix_fmt = depth <= 8 ? AV_PIX_FMT_BAYER_GRBG8 : endian ? AV_PIX_FMT_BAYER_GRBG16BE : AV_PIX_FMT_BAYER_GRBG16LE; break;
    case  10: pix_fmt = depth <= 8 ? AV_PIX_FMT_BAYER_GBRG8 : endian ? AV_PIX_FMT_BAYER_GBRG16BE : AV_PIX_FMT_BAYER_GBRG16LE; break;
    case  11: pix_fmt = depth <= 8 ? AV_PIX_FMT_BAYER_BGGR8 : endian ? AV_PIX_FMT_BAYER_BGGR16BE : AV_PIX_FMT_BAYER_BGGR16LE; break;
    case 100: pix_fmt = depth <= 8 ? AV_PIX_FMT_RGB24       : endian ? AV_PIX_FMT_RGB48BE        : AV_PIX_FMT_RGB48LE;        break;
    case 101: pix_fmt = depth <= 8 ? AV_PIX_FMT_BGR24       : endian ? AV_PIX_FMT_BGR48BE        : AV_PIX_FMT_BGR48LE;        break;
    default:
        return AVERROR_PATCHWELCOME;
    }

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = s->iformat->raw_codec_id;

    avpriv_set_pts_info(st, 64, ser->framerate.den, ser->framerate.num);

    st->codecpar->width  = ser->width;
    st->codecpar->height = ser->height;
    st->codecpar->format = pix_fmt;
    packet_size = av_image_get_buffer_size(st->codecpar->format, ser->width, ser->height, 1);
    if (packet_size < 0)
        return packet_size;
    ser->end = 178 + st->nb_frames * packet_size;
    s->packet_size = packet_size;
    st->codecpar->bit_rate = av_rescale_q(s->packet_size,
                                       (AVRational){8,1}, st->time_base);

    return 0;
}


static int ser_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SERDemuxerContext *ser = s->priv_data;
    int64_t pos;
    int ret;

    pos = avio_tell(s->pb);
    if (pos >= ser->end)
        return AVERROR_EOF;

    ret = av_get_packet(s->pb, pkt, s->packet_size);
    pkt->pts = pkt->dts = (pkt->pos - ffformatcontext(s)->data_offset) / s->packet_size;

    pkt->stream_index = 0;
    if (ret < 0)
        return ret;
    return 0;
}

#define OFFSET(x) offsetof(SERDemuxerContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption ser_options[] = {
    { "framerate", "set frame rate", OFFSET(framerate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, DEC },
    { NULL },
};

static const AVClass ser_demuxer_class = {
    .class_name = "ser demuxer",
    .item_name  = av_default_item_name,
    .option     = ser_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVInputFormat ff_ser_demuxer = {
    .name           = "ser",
    .long_name      = NULL_IF_CONFIG_SMALL("SER (Simple uncompressed video format for astronomical capturing)"),
    .priv_data_size = sizeof(SERDemuxerContext),
    .read_probe     = ser_probe,
    .read_header    = ser_read_header,
    .read_packet    = ser_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "ser",
    .raw_codec_id   = AV_CODEC_ID_RAWVIDEO,
    .priv_class     = &ser_demuxer_class,
};
