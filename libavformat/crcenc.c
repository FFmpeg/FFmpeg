/*
 * CRC encoder (for codec/format testing)
 * Copyright (c) 2002 Fabrice Bellard
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

#include <inttypes.h>

#include "libavutil/adler32.h"
#include "avformat.h"

typedef struct CRCState {
    uint32_t crcval;
} CRCState;

static int crc_init(struct AVFormatContext *s)
{
    CRCState *crc = s->priv_data;

    /* init CRC */
    crc->crcval = 1;

    return 0;
}

static int crc_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    CRCState *crc = s->priv_data;
    crc->crcval = av_adler32_update(crc->crcval, pkt->data, pkt->size);
    return 0;
}

static int crc_write_trailer(struct AVFormatContext *s)
{
    CRCState *crc = s->priv_data;

    avio_printf(s->pb, "CRC=0x%08"PRIx32"\n", crc->crcval);

    return 0;
}

const AVOutputFormat ff_crc_muxer = {
    .name              = "crc",
    .long_name         = NULL_IF_CONFIG_SMALL("CRC testing"),
    .priv_data_size    = sizeof(CRCState),
    .audio_codec       = AV_CODEC_ID_PCM_S16LE,
    .video_codec       = AV_CODEC_ID_RAWVIDEO,
    .init              = crc_init,
    .write_packet      = crc_write_packet,
    .write_trailer     = crc_write_trailer,
    .flags             = AVFMT_NOTIMESTAMPS,
};
