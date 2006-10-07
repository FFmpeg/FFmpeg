/*
 * CRC decoder (for codec/format testing)
 * Copyright (c) 2002 Fabrice Bellard.
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
#include "adler32.h"

#ifdef CONFIG_CRC_MUXER
typedef struct CRCState {
    uint32_t crcval;
} CRCState;

static int crc_write_header(struct AVFormatContext *s)
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
    char buf[64];

    snprintf(buf, sizeof(buf), "CRC=0x%08x\n", crc->crcval);
    put_buffer(&s->pb, buf, strlen(buf));
    put_flush_packet(&s->pb);
    return 0;
}
#endif

#ifdef CONFIG_FRAMECRC_MUXER
static int framecrc_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    uint32_t crc = av_adler32_update(0, pkt->data, pkt->size);
    char buf[256];

    snprintf(buf, sizeof(buf), "%d, %"PRId64", %d, 0x%08x\n", pkt->stream_index, pkt->dts, pkt->size, crc);
    put_buffer(&s->pb, buf, strlen(buf));
    put_flush_packet(&s->pb);
    return 0;
}
#endif

#ifdef CONFIG_CRC_MUXER
AVOutputFormat crc_muxer = {
    "crc",
    "crc testing format",
    NULL,
    "",
    sizeof(CRCState),
    CODEC_ID_PCM_S16LE,
    CODEC_ID_RAWVIDEO,
    crc_write_header,
    crc_write_packet,
    crc_write_trailer,
};
#endif
#ifdef CONFIG_FRAMECRC_MUXER
AVOutputFormat framecrc_muxer = {
    "framecrc",
    "framecrc testing format",
    NULL,
    "",
    0,
    CODEC_ID_PCM_S16LE,
    CODEC_ID_RAWVIDEO,
    NULL,
    framecrc_write_packet,
    NULL,
};
#endif
