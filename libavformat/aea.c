/*
 * MD STUDIO audio demuxer
 *
 * Copyright (c) 2009 Benjamin Larsson
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
#include "pcm.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/audioconvert.h"

#define AT1_SU_SIZE     212

static int aea_read_probe(AVProbeData *p)
{
    if (p->buf_size <= 2048+212)
        return 0;

    /* Magic is '00 08 00 00' in Little Endian*/
    if (AV_RL32(p->buf)==0x800) {
        int bsm_s, bsm_e, inb_s, inb_e, ch;
        ch    = p->buf[264];
        bsm_s = p->buf[2048];
        inb_s = p->buf[2048+1];
        inb_e = p->buf[2048+210];
        bsm_e = p->buf[2048+211];

        if (ch != 1 && ch != 2)
            return 0;

        /* Check so that the redundant bsm bytes and info bytes are valid
         * the block size mode bytes have to be the same
         * the info bytes have to be the same
         */
        if (bsm_s == bsm_e && inb_s == inb_e)
            return AVPROBE_SCORE_MAX / 4 + 1;
    }
    return 0;
}

static int aea_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    AVStream *st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    /* Parse the amount of channels and skip to pos 2048(0x800) */
    avio_skip(s->pb, 264);
    st->codec->channels = avio_r8(s->pb);
    avio_skip(s->pb, 1783);


    st->codec->codec_type     = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id       = CODEC_ID_ATRAC1;
    st->codec->sample_rate    = 44100;
    st->codec->bit_rate       = 292000;

    if (st->codec->channels != 1 && st->codec->channels != 2) {
        av_log(s,AV_LOG_ERROR,"Channels %d not supported!\n",st->codec->channels);
        return -1;
    }

    st->codec->channel_layout = (st->codec->channels == 1) ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;

    st->codec->block_align = AT1_SU_SIZE * st->codec->channels;
    return 0;
}

static int aea_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret = av_get_packet(s->pb, pkt, s->streams[0]->codec->block_align);

    pkt->stream_index = 0;
    if (ret <= 0)
        return AVERROR(EIO);

    return ret;
}

AVInputFormat ff_aea_demuxer = {
    "aea",
    NULL_IF_CONFIG_SMALL("MD STUDIO audio"),
    0,
    aea_read_probe,
    aea_read_header,
    aea_read_packet,
    0,
    pcm_read_seek,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "aea",
};

