/*
 * Maxis XA (.xa) File Demuxer
 * Copyright (c) 2008 Robert Marston
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
 * Maxis XA File Demuxer
 * by Robert Marston (rmarston@gmail.com)
 * for more information on the XA audio format see
 *   http://wiki.multimedia.cx/index.php?title=Maxis_XA
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"

#define XA00_TAG MKTAG('X', 'A', 0, 0)
#define XAI0_TAG MKTAG('X', 'A', 'I', 0)
#define XAJ0_TAG MKTAG('X', 'A', 'J', 0)

typedef struct MaxisXADemuxContext {
    uint32_t out_size;
    uint32_t sent_bytes;
    uint32_t audio_frame_counter;
} MaxisXADemuxContext;

static int xa_probe(AVProbeData *p)
{
    int channels, srate, bits_per_sample;
    if (p->buf_size < 24)
        return 0;
    switch(AV_RL32(p->buf)) {
    case XA00_TAG:
    case XAI0_TAG:
    case XAJ0_TAG:
        break;
    default:
        return 0;
    }
    channels        = AV_RL16(p->buf + 10);
    srate           = AV_RL32(p->buf + 12);
    bits_per_sample = AV_RL16(p->buf + 22);
    if (!channels || channels > 8 || !srate || srate > 192000 ||
        bits_per_sample < 4 || bits_per_sample > 32)
        return 0;
    return AVPROBE_SCORE_MAX/2;
}

static int xa_read_header(AVFormatContext *s,
               AVFormatParameters *ap)
{
    MaxisXADemuxContext *xa = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *st;

    /*Set up the XA Audio Decoder*/
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type   = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id     = CODEC_ID_ADPCM_EA_MAXIS_XA;
    url_fskip(pb, 4);       /* Skip the XA ID */
    xa->out_size            =  get_le32(pb);
    url_fskip(pb, 2);       /* Skip the tag */
    st->codec->channels     = get_le16(pb);
    st->codec->sample_rate  = get_le32(pb);
    /* Value in file is average byte rate*/
    st->codec->bit_rate     = get_le32(pb) * 8;
    st->codec->block_align  = get_le16(pb);
    st->codec->bits_per_coded_sample = get_le16(pb);

    av_set_pts_info(st, 64, 1, st->codec->sample_rate);

    return 0;
}

static int xa_read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    MaxisXADemuxContext *xa = s->priv_data;
    AVStream *st = s->streams[0];
    ByteIOContext *pb = s->pb;
    unsigned int packet_size;
    int ret;

    if(xa->sent_bytes > xa->out_size)
        return AVERROR(EIO);
    /* 1 byte header and 14 bytes worth of samples * number channels per block */
    packet_size = 15*st->codec->channels;

    ret = av_get_packet(pb, pkt, packet_size);
    if(ret < 0)
        return ret;

    pkt->stream_index = st->index;
    xa->sent_bytes += packet_size;
    pkt->pts = xa->audio_frame_counter;
    /* 14 bytes Samples per channel with 2 samples per byte */
    xa->audio_frame_counter += 28 * st->codec->channels;

    return ret;
}

AVInputFormat xa_demuxer = {
    "xa",
    NULL_IF_CONFIG_SMALL("Maxis XA File Format"),
    sizeof(MaxisXADemuxContext),
    xa_probe,
    xa_read_header,
    xa_read_packet,
};
