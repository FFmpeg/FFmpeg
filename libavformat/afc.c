/*
 * AFC demuxer
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

#include "libavutil/channel_layout.h"
#include "avformat.h"
#include "internal.h"

typedef struct AFCDemuxContext {
    int64_t    data_end;
} AFCDemuxContext;

static int afc_read_header(AVFormatContext *s)
{
    AFCDemuxContext *c = s->priv_data;
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id   = AV_CODEC_ID_ADPCM_AFC;
    st->codec->channels   = 2;
    st->codec->channel_layout = AV_CH_LAYOUT_STEREO;
    st->codec->extradata_size = 1;

    st->codec->extradata = av_mallocz(1 + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!st->codec->extradata)
        return AVERROR(ENOMEM);
    st->codec->extradata[0] = 8 * st->codec->channels;

    c->data_end = avio_rb32(s->pb) + 32LL;
    st->duration = avio_rb32(s->pb);
    st->codec->sample_rate = avio_rb16(s->pb);
    avio_skip(s->pb, 22);
    avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);

    return 0;
}

static int afc_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AFCDemuxContext *c = s->priv_data;
    int64_t size;
    int ret;

    size = FFMIN(c->data_end - avio_tell(s->pb), 18 * 128);
    if (size <= 0)
        return AVERROR_EOF;

    ret = av_get_packet(s->pb, pkt, size);
    pkt->stream_index = 0;
    return ret;
}

AVInputFormat ff_afc_demuxer = {
    .name           = "afc",
    .long_name      = NULL_IF_CONFIG_SMALL("AFC"),
    .priv_data_size = sizeof(AFCDemuxContext),
    .read_header    = afc_read_header,
    .read_packet    = afc_read_packet,
    .extensions     = "afc",
    .flags          = AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK,
};
