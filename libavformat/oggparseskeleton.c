/*
 * Copyright (C) 2010 David Conrad
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

#include "libavcodec/bytestream.h"
#include "avformat.h"
#include "internal.h"
#include "oggdec.h"

static int skeleton_header(AVFormatContext *s, int idx)
{
    struct ogg *ogg = s->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    AVStream *st = s->streams[idx];
    uint8_t *buf = os->buf + os->pstart;
    int version_major, version_minor;
    int64_t start_num, start_den, start_granule;
    int target_idx, start_time;

    strcpy(st->codec->codec_name, "skeleton");
    st->codec->codec_type = AVMEDIA_TYPE_DATA;

    if (os->psize < 8)
        return -1;

    if (!strncmp(buf, "fishead", 8)) {
        if (os->psize < 64)
            return -1;

        version_major = AV_RL16(buf+8);
        version_minor = AV_RL16(buf+10);

        if (version_major != 3) {
            av_log(s, AV_LOG_WARNING, "Unknown skeleton version %d.%d\n",
                   version_major, version_minor);
            return -1;
        }

        // This is the overall start time. We use it for the start time of
        // of the skeleton stream since if left unset lavf assumes 0,
        // which we don't want since skeleton is timeless
        // FIXME: the real meaning of this field is "start playback at
        // this time which can be in the middle of a packet
        start_num = AV_RL64(buf+12);
        start_den = AV_RL64(buf+20);

        if (start_den) {
            int base_den;
            av_reduce(&start_time, &base_den, start_num, start_den, INT_MAX);
            avpriv_set_pts_info(st, 64, 1, base_den);
            os->lastpts =
            st->start_time = start_time;
        }
    } else if (!strncmp(buf, "fisbone", 8)) {
        if (os->psize < 52)
            return -1;

        target_idx = ogg_find_stream(ogg, AV_RL32(buf+12));
        start_granule = AV_RL64(buf+36);
        if (target_idx >= 0 && start_granule != -1) {
            ogg->streams[target_idx].lastpts =
            s->streams[target_idx]->start_time = ogg_gptopts(s, target_idx, start_granule, NULL);
        }
    }

    return 1;
}

const struct ogg_codec ff_skeleton_codec = {
    .magic = "fishead",
    .magicsize = 8,
    .header = skeleton_header,
};
