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
#include "internal.h"


typedef struct voc_enc_context {
    int param_written;
} VocEncContext;

static int voc_write_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    const int header_size = 26;
    const int version = 0x0114;

    if (s->nb_streams != 1
        || s->streams[0]->codec->codec_type != AVMEDIA_TYPE_AUDIO)
        return AVERROR_PATCHWELCOME;

    avio_write(pb, ff_voc_magic, sizeof(ff_voc_magic) - 1);
    avio_wl16(pb, header_size);
    avio_wl16(pb, version);
    avio_wl16(pb, ~version + 0x1234);

    return 0;
}

static int voc_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    VocEncContext *voc = s->priv_data;
    AVCodecContext *enc = s->streams[0]->codec;
    AVIOContext *pb = s->pb;

    if (!voc->param_written) {
        if (enc->codec_tag > 0xFF) {
            avio_w8(pb, VOC_TYPE_NEW_VOICE_DATA);
            avio_wl24(pb, pkt->size + 12);
            avio_wl32(pb, enc->sample_rate);
            avio_w8(pb, enc->bits_per_coded_sample);
            avio_w8(pb, enc->channels);
            avio_wl16(pb, enc->codec_tag);
            avio_wl32(pb, 0);
        } else {
            if (s->streams[0]->codec->channels > 1) {
                avio_w8(pb, VOC_TYPE_EXTENDED);
                avio_wl24(pb, 4);
                avio_wl16(pb, 65536-256000000/(enc->sample_rate*enc->channels));
                avio_w8(pb, enc->codec_tag);
                avio_w8(pb, enc->channels - 1);
            }
            avio_w8(pb, VOC_TYPE_VOICE_DATA);
            avio_wl24(pb, pkt->size + 2);
            avio_w8(pb, 256 - 1000000 / enc->sample_rate);
            avio_w8(pb, enc->codec_tag);
        }
        voc->param_written = 1;
    } else {
        avio_w8(pb, VOC_TYPE_VOICE_DATA_CONT);
        avio_wl24(pb, pkt->size);
    }

    avio_write(pb, pkt->data, pkt->size);
    return 0;
}

static int voc_write_trailer(AVFormatContext *s)
{
    avio_w8(s->pb, 0);
    return 0;
}

AVOutputFormat ff_voc_muxer = {
    .name              = "voc",
    .long_name         = NULL_IF_CONFIG_SMALL("Creative Voice file format"),
    .mime_type         = "audio/x-voc",
    .extensions        = "voc",
    .priv_data_size    = sizeof(VocEncContext),
    .audio_codec       = CODEC_ID_PCM_U8,
    .video_codec       = CODEC_ID_NONE,
    .write_header      = voc_write_header,
    .write_packet      = voc_write_packet,
    .write_trailer     = voc_write_trailer,
    .codec_tag=(const AVCodecTag* const []){ff_voc_codec_tags, 0},
};
