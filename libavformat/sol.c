/*
 * Sierra SOL demuxer
 * Copyright Konstantin Shishkov
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

/*
 * Based on documents from Game Audio Player and own research
 */

#include "libavutil/bswap.h"
#include "avformat.h"
#include "pcm.h"

/* if we don't know the size in advance */
#define AU_UNKNOWN_SIZE ((uint32_t)(~0))

static int sol_probe(AVProbeData *p)
{
    /* check file header */
    uint16_t magic;
    magic=av_le2ne16(*((uint16_t*)p->buf));
    if ((magic == 0x0B8D || magic == 0x0C0D || magic == 0x0C8D) &&
        p->buf[2] == 'S' && p->buf[3] == 'O' &&
        p->buf[4] == 'L' && p->buf[5] == 0)
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

#define SOL_DPCM    1
#define SOL_16BIT   4
#define SOL_STEREO 16

static enum CodecID sol_codec_id(int magic, int type)
{
    if (magic == 0x0B8D)
    {
        if (type & SOL_DPCM) return CODEC_ID_SOL_DPCM;
        else return CODEC_ID_PCM_U8;
    }
    if (type & SOL_DPCM)
    {
        if (type & SOL_16BIT) return CODEC_ID_SOL_DPCM;
        else if (magic == 0x0C8D) return CODEC_ID_SOL_DPCM;
        else return CODEC_ID_SOL_DPCM;
    }
    if (type & SOL_16BIT) return CODEC_ID_PCM_S16LE;
    return CODEC_ID_PCM_U8;
}

static int sol_codec_type(int magic, int type)
{
    if (magic == 0x0B8D) return 1;//SOL_DPCM_OLD;
    if (type & SOL_DPCM)
    {
        if (type & SOL_16BIT) return 3;//SOL_DPCM_NEW16;
        else if (magic == 0x0C8D) return 1;//SOL_DPCM_OLD;
        else return 2;//SOL_DPCM_NEW8;
    }
    return -1;
}

static int sol_channels(int magic, int type)
{
    if (magic == 0x0B8D || !(type & SOL_STEREO)) return 1;
    return 2;
}

static int sol_read_header(AVFormatContext *s,
                          AVFormatParameters *ap)
{
    unsigned int magic,tag;
    AVIOContext *pb = s->pb;
    unsigned int id, channels, rate, type;
    enum CodecID codec;
    AVStream *st;

    /* check ".snd" header */
    magic = avio_rl16(pb);
    tag = avio_rl32(pb);
    if (tag != MKTAG('S', 'O', 'L', 0))
        return -1;
    rate = avio_rl16(pb);
    type = avio_r8(pb);
    avio_skip(pb, 4); /* size */
    if (magic != 0x0B8D)
        avio_r8(pb); /* newer SOLs contain padding byte */

    codec = sol_codec_id(magic, type);
    channels = sol_channels(magic, type);

    if (codec == CODEC_ID_SOL_DPCM)
        id = sol_codec_type(magic, type);
    else id = 0;

    /* now we are ready: build format streams */
    st = av_new_stream(s, 0);
    if (!st)
        return -1;
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_tag = id;
    st->codec->codec_id = codec;
    st->codec->channels = channels;
    st->codec->sample_rate = rate;
    av_set_pts_info(st, 64, 1, rate);
    return 0;
}

#define MAX_SIZE 4096

static int sol_read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    int ret;

    if (url_feof(s->pb))
        return AVERROR(EIO);
    ret= av_get_packet(s->pb, pkt, MAX_SIZE);
    pkt->stream_index = 0;

    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    return 0;
}

AVInputFormat ff_sol_demuxer = {
    "sol",
    NULL_IF_CONFIG_SMALL("Sierra SOL format"),
    0,
    sol_probe,
    sol_read_header,
    sol_read_packet,
    NULL,
    pcm_read_seek,
};
