/*
 * Electronic Arts .cdata file Demuxer
 * Copyright (c) 2007 Peter Ross
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
 * Electronic Arts cdata Format Demuxer
 * by Peter Ross (pross@xvid.org)
 *
 * Technical details here:
 *  http://wiki.multimedia.cx/index.php?title=EA_Command_And_Conquer_3_Audio_Codec
 */

#include "avformat.h"
#include "internal.h"

typedef struct {
  unsigned int channels;
  unsigned int audio_pts;
} CdataDemuxContext;

static int cdata_probe(AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (b[0] == 0x04 && (b[1] == 0x00 || b[1] == 0x04 || b[1] == 0x0C || b[1] == 0x14))
        return AVPROBE_SCORE_MAX/8;
    return 0;
}

static int cdata_read_header(AVFormatContext *s)
{
    CdataDemuxContext *cdata = s->priv_data;
    AVIOContext *pb = s->pb;
    unsigned int sample_rate, header;
    AVStream *st;
    int64_t channel_layout = 0;

    header = avio_rb16(pb);
    switch (header) {
        case 0x0400: cdata->channels = 1; break;
        case 0x0404: cdata->channels = 2; break;
        case 0x040C: cdata->channels = 4; channel_layout = AV_CH_LAYOUT_QUAD;         break;
        case 0x0414: cdata->channels = 6; channel_layout = AV_CH_LAYOUT_5POINT1_BACK; break;
        default:
            av_log(s, AV_LOG_INFO, "unknown header 0x%04x\n", header);
            return -1;
    };

    sample_rate = avio_rb16(pb);
    avio_skip(pb, (avio_r8(pb) & 0x20) ? 15 : 11);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_tag = 0; /* no fourcc */
    st->codec->codec_id = CODEC_ID_ADPCM_EA_XAS;
    st->codec->channels = cdata->channels;
    st->codec->channel_layout = channel_layout;
    st->codec->sample_rate = sample_rate;
    st->codec->sample_fmt = AV_SAMPLE_FMT_S16;
    avpriv_set_pts_info(st, 64, 1, sample_rate);

    cdata->audio_pts = 0;
    return 0;
}

static int cdata_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    CdataDemuxContext *cdata = s->priv_data;
    int packet_size = 76*cdata->channels;

    int ret = av_get_packet(s->pb, pkt, packet_size);
    if (ret < 0)
        return ret;
    pkt->pts = cdata->audio_pts++;
    return 0;
}

AVInputFormat ff_ea_cdata_demuxer = {
    .name           = "ea_cdata",
    .long_name      = NULL_IF_CONFIG_SMALL("Electronic Arts cdata"),
    .priv_data_size = sizeof(CdataDemuxContext),
    .read_probe     = cdata_probe,
    .read_header    = cdata_read_header,
    .read_packet    = cdata_read_packet,
    .extensions = "cdata",
};
