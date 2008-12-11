/*
 * Creative Voice File muxer.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "voc.h"


typedef struct voc_enc_context {
    int param_written;
} VocEncContext;

static int voc_write_header(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    const int header_size = 26;
    const int version = 0x0114;

    if (s->nb_streams != 1
        || s->streams[0]->codec->codec_type != CODEC_TYPE_AUDIO)
        return AVERROR_PATCHWELCOME;

    put_buffer(pb, ff_voc_magic, sizeof(ff_voc_magic) - 1);
    put_le16(pb, header_size);
    put_le16(pb, version);
    put_le16(pb, ~version + 0x1234);

    return 0;
}

static int voc_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    VocEncContext *voc = s->priv_data;
    AVCodecContext *enc = s->streams[0]->codec;
    ByteIOContext *pb = s->pb;

    if (!voc->param_written) {
        if (enc->codec_tag > 0xFF) {
            put_byte(pb, VOC_TYPE_NEW_VOICE_DATA);
            put_le24(pb, pkt->size + 12);
            put_le32(pb, enc->sample_rate);
            put_byte(pb, enc->bits_per_coded_sample);
            put_byte(pb, enc->channels);
            put_le16(pb, enc->codec_tag);
            put_le32(pb, 0);
        } else {
            if (s->streams[0]->codec->channels > 1) {
                put_byte(pb, VOC_TYPE_EXTENDED);
                put_le24(pb, 4);
                put_le16(pb, 65536-256000000/(enc->sample_rate*enc->channels));
                put_byte(pb, enc->codec_tag);
                put_byte(pb, enc->channels - 1);
            }
            put_byte(pb, VOC_TYPE_VOICE_DATA);
            put_le24(pb, pkt->size + 2);
            put_byte(pb, 256 - 1000000 / enc->sample_rate);
            put_byte(pb, enc->codec_tag);
        }
        voc->param_written = 1;
    } else {
        put_byte(pb, VOC_TYPE_VOICE_DATA_CONT);
        put_le24(pb, pkt->size);
    }

    put_buffer(pb, pkt->data, pkt->size);
    return 0;
}

static int voc_write_trailer(AVFormatContext *s)
{
    put_byte(s->pb, 0);
    return 0;
}

AVOutputFormat voc_muxer = {
    "voc",
    NULL_IF_CONFIG_SMALL("Creative Voice file format"),
    "audio/x-voc",
    "voc",
    sizeof(VocEncContext),
    CODEC_ID_PCM_U8,
    CODEC_ID_NONE,
    voc_write_header,
    voc_write_packet,
    voc_write_trailer,
    .codec_tag=(const AVCodecTag* const []){ff_voc_codec_tags, 0},
};
