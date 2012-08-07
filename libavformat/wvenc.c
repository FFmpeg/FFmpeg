/*
 * WavPack muxer
 * Copyright (c) 2012 Paul B Mahol
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

#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"
#include "apetag.h"

typedef struct{
    uint32_t duration;
    int off;
} WVMuxContext;

static int write_header(AVFormatContext *s)
{
    WVMuxContext *wc = s->priv_data;
    AVCodecContext *codec = s->streams[0]->codec;

    if (s->nb_streams > 1) {
        av_log(s, AV_LOG_ERROR, "only one stream is supported\n");
        return AVERROR(EINVAL);
    }
    if (codec->codec_id != AV_CODEC_ID_WAVPACK) {
        av_log(s, AV_LOG_ERROR, "unsupported codec\n");
        return AVERROR(EINVAL);
    }
    if (codec->extradata_size > 0) {
        av_log_missing_feature(s, "remuxing from matroska container", 0);
        return AVERROR_PATCHWELCOME;
    }
    wc->off = codec->channels > 2 ? 4 : 0;
    avpriv_set_pts_info(s->streams[0], 64, 1, codec->sample_rate);

    return 0;
}

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    WVMuxContext *wc = s->priv_data;
    AVIOContext *pb = s->pb;

    wc->duration += pkt->duration;
    ffio_wfourcc(pb, "wvpk");
    avio_wl32(pb, pkt->size + 12 + wc->off);
    avio_wl16(pb, 0x410);
    avio_w8(pb, 0);
    avio_w8(pb, 0);
    avio_wl32(pb, -1);
    avio_wl32(pb, pkt->pts);
    avio_write(s->pb, pkt->data, pkt->size);
    avio_flush(s->pb);

    return 0;
}

static int write_trailer(AVFormatContext *s)
{
    WVMuxContext *wc = s->priv_data;
    AVIOContext *pb = s->pb;

    ff_ape_write(s);

    if (pb->seekable) {
        avio_seek(pb, 12, SEEK_SET);
        avio_wl32(pb, wc->duration);
        avio_flush(pb);
    }

    return 0;
}

AVOutputFormat ff_wv_muxer = {
    .name              = "wv",
    .long_name         = NULL_IF_CONFIG_SMALL("WavPack"),
    .priv_data_size    = sizeof(WVMuxContext),
    .extensions        = "wv",
    .audio_codec       = AV_CODEC_ID_WAVPACK,
    .video_codec       = AV_CODEC_ID_NONE,
    .write_header      = write_header,
    .write_packet      = write_packet,
    .write_trailer     = write_trailer,
};
