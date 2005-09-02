/*
 * D-Cinema audio decoder.
 * Copyright (c) 2005 Reimar Döffinger.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"

static int daud_header(AVFormatContext *s, AVFormatParameters *ap) {
    AVStream *st = av_new_stream(s, 0);
    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_PCM_S24DAUD;
    st->codec->codec_tag = MKTAG('d', 'a', 'u', 'd');
    st->codec->channels = 6;
    st->codec->sample_rate = 96000;
    st->codec->bit_rate = 3 * 6 * 96000 * 8;
    st->codec->block_align = 3 * 6;
    st->codec->bits_per_sample = 24;
    return 0;
}

static int daud_packet(AVFormatContext *s, AVPacket *pkt) {
    ByteIOContext *pb = &s->pb;
    int ret, size;
    if (url_feof(pb))
        return AVERROR_IO;
    size = get_be16(pb);
    get_be16(pb); // unknown
    ret = av_get_packet(pb, pkt, size);
    pkt->stream_index = 0;
    return ret;
}

static AVInputFormat daud_iformat = {
    "daud",
    "D-Cinema audio format",
    0,
    NULL,
    daud_header,
    daud_packet,
    NULL,
    NULL,
    .extensions = "302",
};

int daud_init(void)
{
    av_register_input_format(&daud_iformat);
    return 0;
}

