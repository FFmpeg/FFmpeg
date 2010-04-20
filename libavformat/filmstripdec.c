/*
 * Adobe Filmstrip demuxer
 * Copyright (c) 2010 Peter Ross
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

/**
 * @file
 * Adobe Filmstrip demuxer
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"

#define RAND_TAG MKBETAG('R','a','n','d')

typedef struct {
    int leading;
} FilmstripDemuxContext;

static int read_header(AVFormatContext *s,
                       AVFormatParameters *ap)
{
    FilmstripDemuxContext *film = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *st;

    if (url_is_streamed(s->pb))
        return AVERROR(EIO);

    url_fseek(pb, url_fsize(pb) - 36, SEEK_SET);
    if (get_be32(pb) != RAND_TAG) {
        av_log(s, AV_LOG_ERROR, "magic number not found");
        return AVERROR_INVALIDDATA;
    }

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    st->nb_frames = get_be32(pb);
    if (get_be16(pb) != 0) {
        av_log_ask_for_sample(s, "unsupported packing method\n");
        return AVERROR_INVALIDDATA;
    }

    url_fskip(pb, 2);
    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id   = CODEC_ID_RAWVIDEO;
    st->codec->pix_fmt    = PIX_FMT_RGBA;
    st->codec->codec_tag  = 0; /* no fourcc */
    st->codec->width      = get_be16(pb);
    st->codec->height     = get_be16(pb);
    film->leading         = get_be16(pb);
    av_set_pts_info(st, 64, 1, get_be16(pb));

    url_fseek(pb, 0, SEEK_SET);

    return 0;
}

static int read_packet(AVFormatContext *s,
                       AVPacket *pkt)
{
    FilmstripDemuxContext *film = s->priv_data;
    AVStream *st = s->streams[0];

    if (url_feof(s->pb))
        return AVERROR(EIO);
    pkt->dts = url_ftell(s->pb) / (st->codec->width * (st->codec->height + film->leading) * 4);
    pkt->size = av_get_packet(s->pb, pkt, st->codec->width * st->codec->height * 4);
    url_fskip(s->pb, st->codec->width * film->leading * 4);
    if (pkt->size < 0)
        return pkt->size;
    pkt->flags |= AV_PKT_FLAG_KEY;
    return 0;
}

static int read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    AVStream *st = s->streams[stream_index];
    url_fseek(s->pb, FFMAX(timestamp, 0) * st->codec->width * st->codec->height * 4, SEEK_SET);
    return 0;
}

AVInputFormat filmstrip_demuxer = {
    "filmstrip",
    NULL_IF_CONFIG_SMALL("Adobe Filmstrip"),
    sizeof(FilmstripDemuxContext),
    NULL,
    read_header,
    read_packet,
    NULL,
    read_seek,
    .extensions = "flm",
};
