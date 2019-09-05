/*
 * RAW Dirac demuxer
 * Copyright (c) 2007 Marco Gerards <marco@gnu.org>
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
#include "rawdec.h"

static int dirac_probe(const AVProbeData *p)
{
    unsigned size;
    if (AV_RL32(p->buf) != MKTAG('B', 'B', 'C', 'D'))
        return 0;

    size = AV_RB32(p->buf+5);
    if (size < 13)
        return 0;
    if (size + 13LL > p->buf_size)
        return AVPROBE_SCORE_MAX / 4;
    if (AV_RL32(p->buf + size) != MKTAG('B', 'B', 'C', 'D'))
        return 0;

    return AVPROBE_SCORE_MAX;
}

FF_DEF_RAWVIDEO_DEMUXER(dirac, "raw Dirac", dirac_probe, NULL, AV_CODEC_ID_DIRAC)
