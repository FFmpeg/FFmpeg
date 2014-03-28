/*
 * Alias PIX image demuxer
 * Copyright (c) 2014 Michael Niedermayer
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

#include "img2.h"
#include "libavutil/intreadwrite.h"

static int brender_read_probe(AVProbeData *p)
{
    int width  = AV_RB16(p->buf);
    int height = AV_RB16(p->buf+2);
    int ox     = AV_RB16(p->buf+4);
    int oy     = AV_RB16(p->buf+6);
    int bpp    = AV_RB16(p->buf+8);
    int count  = p->buf[10];

    if (!count || !height || count > width)
        return 0;

    if (bpp != 24 && bpp != 8)
        return 0;

    return AVPROBE_SCORE_EXTENSION + 1;
}

AVInputFormat ff_image2_alias_pix_demuxer = {
    .name           = "alias_pix",
    .long_name      = NULL_IF_CONFIG_SMALL("Alias/Wavefront PIX image"),
    .priv_data_size = sizeof(VideoDemuxData),
    .read_probe     = brender_read_probe,
    .read_header    = ff_img_read_header,
    .read_packet    = ff_img_read_packet,
    .raw_codec_id   = AV_CODEC_ID_ALIAS_PIX,
};
