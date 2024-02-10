/*
 * BRender PIX image demuxer
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

#include "demux.h"
#include "img2.h"
#include "libavutil/intreadwrite.h"

static int brender_read_probe(const AVProbeData *p)
{
    static const uint8_t brender_magic[16] = {
        0,0,0,0x12,0,0,0,8,0,0,0,2,0,0,0,2
    };

    if (memcmp(p->buf, brender_magic, sizeof(brender_magic)))
        return 0;

    if (AV_RB32(p->buf+16) != 0x03 &&
        AV_RB32(p->buf+16) != 0x3D)
        return 0;

    return AVPROBE_SCORE_MAX-10;
}

static const AVClass image2_brender_pix_class = {
    .class_name = "brender_pix demuxer",
    .item_name  = av_default_item_name,
    .option     = ff_img_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFInputFormat ff_image2_brender_pix_demuxer = {
    .p.name         = "brender_pix",
    .p.long_name    = NULL_IF_CONFIG_SMALL("BRender PIX image"),
    .p.priv_class   = &image2_brender_pix_class,
    .priv_data_size = sizeof(VideoDemuxData),
    .read_probe     = brender_read_probe,
    .read_header    = ff_img_read_header,
    .read_packet    = ff_img_read_packet,
    .raw_codec_id   = AV_CODEC_ID_BRENDER_PIX,
};
