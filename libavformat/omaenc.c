/*
 * Sony OpenMG (OMA) muxer
 *
 * Copyright (c) 2011 Michael Karcher
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

#include "avformat.h"
#include "avio_internal.h"
#include "id3v2.h"
#include "internal.h"
#include "oma.h"
#include "rawenc.h"

static av_cold int oma_write_header(AVFormatContext *s)
{
    int i;
    AVCodecContext *format;
    int srate_index;
    int isjointstereo;

    format = s->streams[0]->codec;
    /* check for support of the format first */

    for (srate_index = 0; ; srate_index++) {
        if (ff_oma_srate_tab[srate_index] == 0) {
            av_log(s, AV_LOG_ERROR, "Sample rate %d not supported in OpenMG audio\n",
                   format->sample_rate);
            return AVERROR(EINVAL);
        }

        if (ff_oma_srate_tab[srate_index] * 100 == format->sample_rate)
            break;
    }

    /* Metadata; OpenMG does not support ID3v2.4 */
    ff_id3v2_write(s, 3, ID3v2_EA3_MAGIC);

    ffio_wfourcc(s->pb, "EA3\0");
    avio_w8(s->pb, EA3_HEADER_SIZE >> 7);
    avio_w8(s->pb, EA3_HEADER_SIZE & 0x7F);
    avio_wl16(s->pb, 0xFFFF);       /* not encrypted */
    for (i = 0; i < 6; i++)
        avio_wl32(s->pb, 0);        /* Padding + DRM id */

    switch(format->codec_tag) {
    case OMA_CODECID_ATRAC3:
        if (format->channels != 2) {
            av_log(s, AV_LOG_ERROR, "ATRAC3 in OMA is only supported with 2 channels");
            return AVERROR(EINVAL);
        }
        if (format->extradata_size == 14) /* WAV format extradata */
            isjointstereo = format->extradata[6] != 0;
        else if(format->extradata_size == 10) /* RM format extradata */
            isjointstereo = format->extradata[8] == 0x12;
        else {
            av_log(s, AV_LOG_ERROR, "ATRAC3: Unsupported extradata size\n");
            return AVERROR(EINVAL);
        }
        avio_wb32(s->pb, (OMA_CODECID_ATRAC3 << 24) |
                         (isjointstereo << 17) |
                         (srate_index << 13) |
                         (format->block_align/8));
        break;
    case OMA_CODECID_ATRAC3P:
        avio_wb32(s->pb, (OMA_CODECID_ATRAC3P << 24) |
                         (srate_index << 13) |
                         (format->channels << 10) |
                         (format->block_align/8 - 1));
        break;
    default:
        av_log(s, AV_LOG_ERROR, "OMA: unsupported codec tag %d for write\n",
               format->codec_tag);
    }
    for (i = 0; i < (EA3_HEADER_SIZE - 36)/4; i++)
        avio_wl32(s->pb, 0);        /* Padding */

    return 0;
}

AVOutputFormat ff_oma_muxer = {
    .name              = "oma",
    .long_name         = NULL_IF_CONFIG_SMALL("Sony OpenMG audio"),
    .mime_type         = "audio/x-oma",
    .extensions        = "oma",
    .audio_codec       = CODEC_ID_ATRAC3,
    .write_header      = oma_write_header,
    .write_packet      = ff_raw_write_packet,
    .codec_tag         = (const AVCodecTag* const []){ff_oma_codec_tags, 0},
};
