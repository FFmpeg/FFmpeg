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
#include "internal.h"

#define RAND_TAG MKBETAG('R','a','n','d')

typedef struct FilmstripDemuxContext {
    int leading;
} FilmstripDemuxContext;

static int read_header(AVFormatContext *s)
{
    FilmstripDemuxContext *film = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;

    if (!s->pb->seekable)
        return AVERROR(EIO);

    avio_seek(pb, avio_size(pb) - 36, SEEK_SET);
    if (avio_rb32(pb) != RAND_TAG) {
        av_log(s, AV_LOG_ERROR, "magic number not found\n");
        return AVERROR_INVALIDDATA;
    }

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->nb_frames = avio_rb32(pb);
    if (avio_rb16(pb) != 0) {
        avpriv_request_sample(s, "Unsupported packing method");
        return AVERROR_PATCHWELCOME;
    }

    avio_skip(pb, 2);
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_RAWVIDEO;
    st->codecpar->format     = AV_PIX_FMT_RGBA;
    st->codecpar->codec_tag  = 0; /* no fourcc */
    st->codecpar->width      = avio_rb16(pb);
    st->codecpar->height     = avio_rb16(pb);
    film->leading         = avio_rb16(pb);

    if (st->codecpar->width * 4LL * st->codecpar->height >= INT_MAX) {
        av_log(s, AV_LOG_ERROR, "dimensions too large\n");
        return AVERROR_PATCHWELCOME;
    }

    avpriv_set_pts_info(st, 64, 1, avio_rb16(pb));

    avio_seek(pb, 0, SEEK_SET);

    return 0;
}

static int read_packet(AVFormatContext *s,
                       AVPacket *pkt)
{
    FilmstripDemuxContext *film = s->priv_data;
    AVStream *st = s->streams[0];

    if (avio_feof(s->pb))
        return AVERROR(EIO);
    pkt->dts = avio_tell(s->pb) / (st->codecpar->width * (int64_t)(st->codecpar->height + film->leading) * 4);
    pkt->size = av_get_packet(s->pb, pkt, st->codecpar->width * st->codecpar->height * 4);
    avio_skip(s->pb, st->codecpar->width * (int64_t) film->leading * 4);
    if (pkt->size < 0)
        return pkt->size;
    pkt->flags |= AV_PKT_FLAG_KEY;
    return 0;
}

static int read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    AVStream *st = s->streams[stream_index];
    if (avio_seek(s->pb, FFMAX(timestamp, 0) * st->codecpar->width * st->codecpar->height * 4, SEEK_SET) < 0)
        return -1;
    return 0;
}

AVInputFormat ff_filmstrip_demuxer = {
    .name           = "filmstrip",
    .long_name      = NULL_IF_CONFIG_SMALL("Adobe Filmstrip"),
    .priv_data_size = sizeof(FilmstripDemuxContext),
    .read_header    = read_header,
    .read_packet    = read_packet,
    .read_seek      = read_seek,
    .extensions     = "flm",
};
