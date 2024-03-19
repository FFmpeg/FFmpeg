/*
 * Sony OpenMG (OMA) muxer
 *
 * Copyright (c) 2011 Michael Karcher
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
#include "avio_internal.h"
#include "id3v2.h"
#include "internal.h"
#include "mux.h"
#include "oma.h"
#include "rawenc.h"

static av_cold int oma_write_header(AVFormatContext *s)
{
    AVCodecParameters *par;
    int srate_index;
    int isjointstereo;

    par = s->streams[0]->codecpar;
    /* check for support of the format first */

    for (srate_index = 0; ; srate_index++) {
        if (ff_oma_srate_tab[srate_index] == 0) {
            av_log(s, AV_LOG_ERROR, "Sample rate %d not supported in OpenMG audio\n",
                   par->sample_rate);
            return AVERROR(EINVAL);
        }

        if (ff_oma_srate_tab[srate_index] * 100 == par->sample_rate)
            break;
    }

    /* Metadata; OpenMG does not support ID3v2.4 */
    ff_id3v2_write_simple(s, 3, ID3v2_EA3_MAGIC);

    ffio_wfourcc(s->pb, "EA3\0");
    avio_w8(s->pb, EA3_HEADER_SIZE >> 7);
    avio_w8(s->pb, EA3_HEADER_SIZE & 0x7F);
    avio_wl16(s->pb, 0xFFFF);       /* not encrypted */
    ffio_fill(s->pb, 0, 6 * 4);     /* Padding + DRM id */

    switch (par->codec_tag) {
    case OMA_CODECID_ATRAC3:
        if (par->ch_layout.nb_channels != 2) {
            av_log(s, AV_LOG_ERROR, "ATRAC3 in OMA is only supported with 2 channels\n");
            return AVERROR(EINVAL);
        }
        if (par->extradata_size == 14) /* WAV format extradata */
            isjointstereo = par->extradata[6] != 0;
        else if(par->extradata_size == 10) /* RM format extradata */
            isjointstereo = par->extradata[8] == 0x12;
        else {
            av_log(s, AV_LOG_ERROR, "ATRAC3: Unsupported extradata size\n");
            return AVERROR(EINVAL);
        }
        avio_wb32(s->pb, (OMA_CODECID_ATRAC3 << 24) |
                         (isjointstereo << 17) |
                         (srate_index << 13) |
                         (par->block_align/8));
        break;
    case OMA_CODECID_ATRAC3P:
        avio_wb32(s->pb, (OMA_CODECID_ATRAC3P << 24) |
                         (srate_index << 13) |
                         (par->ch_layout.nb_channels << 10) |
                         (par->block_align/8 - 1));
        break;
    default:
        av_log(s, AV_LOG_ERROR, "unsupported codec tag %s for write\n",
               av_fourcc2str(par->codec_tag));
        return AVERROR(EINVAL);
    }
    ffio_fill(s->pb, 0, EA3_HEADER_SIZE - 36);  /* Padding */

    return 0;
}

const FFOutputFormat ff_oma_muxer = {
    .p.name            = "oma",
    .p.long_name       = NULL_IF_CONFIG_SMALL("Sony OpenMG audio"),
    .p.mime_type       = "audio/x-oma",
    .p.extensions      = "oma",
    .p.video_codec     = AV_CODEC_ID_NONE,
    .p.audio_codec     = AV_CODEC_ID_ATRAC3,
    .p.subtitle_codec  = AV_CODEC_ID_NONE,
    .write_header      = oma_write_header,
    .write_packet      = ff_raw_write_packet,
    .p.codec_tag       = ff_oma_codec_tags_list,
    .p.flags           = AVFMT_NOTIMESTAMPS,
    .flags_internal    = FF_OFMT_FLAG_MAX_ONE_OF_EACH,
};
