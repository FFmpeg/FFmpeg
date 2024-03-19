/*
 * IRCAM muxer
 * Copyright (c) 2012 Paul B Mahol
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

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "mux.h"
#include "rawenc.h"
#include "ircam.h"

static int ircam_write_header(AVFormatContext *s)
{
    AVCodecParameters *par = s->streams[0]->codecpar;
    uint32_t tag;

    tag = ff_codec_get_tag(ff_codec_ircam_le_tags, par->codec_id);
    if (!tag) {
        av_log(s, AV_LOG_ERROR, "unsupported codec\n");
        return AVERROR(EINVAL);
    }

    avio_wl32(s->pb, 0x0001A364);
    avio_wl32(s->pb, av_q2intfloat((AVRational){par->sample_rate, 1}));
    avio_wl32(s->pb, par->ch_layout.nb_channels);
    avio_wl32(s->pb, tag);
    ffio_fill(s->pb, 0, 1008);
    return 0;
}

const FFOutputFormat ff_ircam_muxer = {
    .p.name         = "ircam",
    .p.extensions   = "sf,ircam",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Berkeley/IRCAM/CARL Sound Format"),
    .p.audio_codec  = AV_CODEC_ID_PCM_S16LE,
    .p.video_codec  = AV_CODEC_ID_NONE,
    .p.subtitle_codec = AV_CODEC_ID_NONE,
    .flags_internal   = FF_OFMT_FLAG_MAX_ONE_OF_EACH,
    .write_header   = ircam_write_header,
    .write_packet   = ff_raw_write_packet,
    .p.codec_tag    = (const AVCodecTag *const []){ ff_codec_ircam_le_tags, 0 },
};
