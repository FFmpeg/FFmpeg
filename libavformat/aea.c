/*
 * MD STUDIO audio demuxer
 *
 * Copyright (c) 2009 Benjamin Larsson
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "pcm.h"

#define AT1_SU_SIZE     212

static int aea_read_probe(AVProbeData *p)
{
    if (p->buf_size <= 2048+212)
        return 0;

    /* Magic is '00 08 00 00' in little-endian*/
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

static int aea_read_header(AVFormatContext *s)
{
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    /* Parse the amount of channels and skip to pos 2048(0x800) */
    avio_skip(s->pb, 264);
    st->codecpar->channels = avio_r8(s->pb);
    avio_skip(s->pb, 1783);


    st->codecpar->codec_type     = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id       = AV_CODEC_ID_ATRAC1;
    st->codecpar->sample_rate    = 44100;
    st->codecpar->bit_rate       = 292000;

    if (st->codecpar->channels != 1 && st->codecpar->channels != 2) {
        av_log(s, AV_LOG_ERROR, "Channels %d not supported!\n", st->codecpar->channels);
        return AVERROR_INVALIDDATA;
    }

    st->codecpar->channel_layout = (st->codecpar->channels == 1) ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;

    st->codecpar->block_align = AT1_SU_SIZE * st->codecpar->channels;
    return 0;
}

static int aea_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret = av_get_packet(s->pb, pkt, s->streams[0]->codecpar->block_align);

    pkt->stream_index = 0;
    if (ret <= 0)
        return AVERROR(EIO);

    return ret;
}

AVInputFormat ff_aea_demuxer = {
    .name           = "aea",
    .long_name      = NULL_IF_CONFIG_SMALL("MD STUDIO audio"),
    .read_probe     = aea_read_probe,
    .read_header    = aea_read_header,
    .read_packet    = aea_read_packet,
    .read_seek      = ff_pcm_read_seek,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "aea",
};
