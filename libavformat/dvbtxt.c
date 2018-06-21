/*
 * RAW dvb teletext demuxer
 * Copyright (c) 2016 Marton Balnt <cus@passwd.hu>
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

#include "libavcodec/dvbtxt.h"

#include "avformat.h"
#include "rawdec.h"

static int dvbtxt_probe(AVProbeData *p)
{
    const uint8_t *end = p->buf + p->buf_size;
    const uint8_t *buf;

    /* The purpose of this is demuxer is to detect DVB teletext streams in
     * mpegts, so we reject invalid buffer sizes */
    if ((p->buf_size + 45) % 184 != 0)
        return 0;

    if (!ff_data_identifier_is_teletext(p->buf[0]))
        return 0;

    for (buf = p->buf + 1; buf < end; buf += 46) {
        if (!ff_data_unit_id_is_teletext(buf[0]) && buf[0] != 0xff)
            return 0;
        if (buf[1] != 0x2c)     // data_unit_length
            return 0;
    }

    return AVPROBE_SCORE_MAX / 2;
}

FF_DEF_RAWSUB_DEMUXER(dvbtxt, "dvbtxt", dvbtxt_probe, NULL, AV_CODEC_ID_DVB_TELETEXT, 0)
