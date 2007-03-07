/*
 * Creative Voice File demuxer.
 * Copyright (c) 2006  Aurelien Jacobs <aurel@gnuage.org>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "voc.h"




static int voc_probe(AVProbeData *p)
{
    int version, check;

    if (p->buf_size < 26)
        return 0;
    if (memcmp(p->buf, voc_magic, sizeof(voc_magic) - 1))
        return 0;
    version = p->buf[22] | (p->buf[23] << 8);
    check = p->buf[24] | (p->buf[25] << 8);
    if (~version + 0x1234 != check)
        return 10;

    return AVPROBE_SCORE_MAX;
}

static int voc_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    voc_dec_context_t *voc = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int header_size;
    AVStream *st;

    url_fskip(pb, 20);
    header_size = get_le16(pb) - 22;
    if (header_size != 4) {
        av_log(s, AV_LOG_ERROR, "unknown header size: %d\n", header_size);
        return AVERROR_NOTSUPP;
    }
    url_fskip(pb, header_size);
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;
    st->codec->codec_type = CODEC_TYPE_AUDIO;

    voc->remaining_size = 0;
    return 0;
}

int
voc_get_packet(AVFormatContext *s, AVPacket *pkt, AVStream *st, int max_size)
{
    voc_dec_context_t *voc = s->priv_data;
    AVCodecContext *dec = st->codec;
    ByteIOContext *pb = &s->pb;
    voc_type_t type;
    int size;
    int sample_rate = 0;
    int channels = 1;

    while (!voc->remaining_size) {
        type = get_byte(pb);
        if (type == VOC_TYPE_EOF)
            return AVERROR_IO;
        voc->remaining_size = get_le24(pb);
        max_size -= 4;

        switch (type) {
        case VOC_TYPE_VOICE_DATA:
            dec->sample_rate = 1000000 / (256 - get_byte(pb));
            if (sample_rate)
                dec->sample_rate = sample_rate;
            dec->channels = channels;
            dec->codec_id = codec_get_id(voc_codec_tags, get_byte(pb));
            dec->bits_per_sample = av_get_bits_per_sample(dec->codec_id);
            voc->remaining_size -= 2;
            max_size -= 2;
            channels = 1;
            break;

        case VOC_TYPE_VOICE_DATA_CONT:
            break;

        case VOC_TYPE_EXTENDED:
            sample_rate = get_le16(pb);
            get_byte(pb);
            channels = get_byte(pb) + 1;
            sample_rate = 256000000 / (channels * (65536 - sample_rate));
            voc->remaining_size = 0;
            max_size -= 4;
            break;

        case VOC_TYPE_NEW_VOICE_DATA:
            dec->sample_rate = get_le32(pb);
            dec->bits_per_sample = get_byte(pb);
            dec->channels = get_byte(pb);
            dec->codec_id = codec_get_id(voc_codec_tags, get_le16(pb));
            url_fskip(pb, 4);
            voc->remaining_size -= 12;
            max_size -= 12;
            break;

        default:
            url_fskip(pb, voc->remaining_size);
            max_size -= voc->remaining_size;
            voc->remaining_size = 0;
            break;
        }
    }

    dec->bit_rate = dec->sample_rate * dec->bits_per_sample;

    if (max_size <= 0)
        max_size = 2048;
    size = FFMIN(voc->remaining_size, max_size);
    voc->remaining_size -= size;
    return av_get_packet(pb, pkt, size);
}

static int voc_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    return voc_get_packet(s, pkt, s->streams[0], 0);
}

static int voc_read_close(AVFormatContext *s)
{
    return 0;
}

AVInputFormat voc_demuxer = {
    "voc",
    "Creative Voice File format",
    sizeof(voc_dec_context_t),
    voc_probe,
    voc_read_header,
    voc_read_packet,
    voc_read_close,
    .codec_tag=(const AVCodecTag*[]){voc_codec_tags, 0},
};
