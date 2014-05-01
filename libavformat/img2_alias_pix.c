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
#include "libavcodec/bytestream.h"

static int brender_read_probe(AVProbeData *p)
{
    const uint8_t *b = p->buf;
    const uint8_t *end = b + p->buf_size;
    int width  = bytestream_get_be16(&b);
    int height = bytestream_get_be16(&b);
    av_unused int ox = bytestream_get_be16(&b);
    av_unused int oy = bytestream_get_be16(&b);
    int bpp    = bytestream_get_be16(&b);
    int x, y;

    if (!width || !height)
        return 0;

    if (bpp != 24 && bpp != 8)
        return 0;

    for (y=0; y<2 && y<height; y++) {
        for (x=0; x<width; ) {
            int count = *b++;
            if (count == 0 || x + count > width)
                return 0;
            if (b > end)
                return AVPROBE_SCORE_MAX / 8;
            b += bpp / 8;
            x += count;
        }
    }

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
