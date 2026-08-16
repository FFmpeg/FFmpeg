/*
 * ASTC demuxer (.astc container)
 * Copyright (c) 2026 Jun Zhao
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

/**
 * @file
 * ASTC (Adaptive Scalable Texture Compression) demuxer.
 *
 * Reads the 16-byte .astc header, exposes it as extradata to the decoder,
 * and emits the raw ASTC bitstream as a single packet.
 */

#include "config_components.h"

#include <inttypes.h>
#include <limits.h>

#include "avformat.h"
#include "avio.h"
#include "avio_internal.h"
#include "demux.h"
#include "internal.h"
#include "libavutil/intreadwrite.h"

#define ASTC_HEADER_SIZE 16
#define ASTC_BLOCK_BYTES 16
static const uint8_t astc_magic[4] = { 0x13, 0xAB, 0xA1, 0x5C };

typedef struct ASTCDemuxerContext {
    uint64_t image_size;
    int      truncated;
} ASTCDemuxerContext;

static int astc_probe(const AVProbeData *p)
{
    if (p->buf_size >= ASTC_HEADER_SIZE &&
        !memcmp(p->buf, astc_magic, sizeof(astc_magic)))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int astc_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    ASTCDemuxerContext *astc = s->priv_data;
    AVStream *st;
    uint8_t hdr[ASTC_HEADER_SIZE];
    unsigned int bx, by, bz, w, h, dz;
    uint64_t expected;

    if (ffio_read_size(pb, hdr, ASTC_HEADER_SIZE) < 0)
        return AVERROR_INVALIDDATA;

    if (memcmp(hdr, astc_magic, sizeof(astc_magic))) {
        av_log(s, AV_LOG_ERROR, "Not an ASTC file (bad magic).\n");
        return AVERROR_INVALIDDATA;
    }

    bx = hdr[4];
    by = hdr[5];
    bz = hdr[6];
    w  = AV_RL24(hdr + 7);
    h  = AV_RL24(hdr + 10);
    dz = AV_RL24(hdr + 13);

    /* Validate the geometry before trusting it to size the packet: the
     * decoder only handles 2D images, and a zero block size would divide
     * by zero when deriving the block count. */
    if (!bx || !by || !bz || !w || !h || dz != 1) {
        av_log(s, AV_LOG_ERROR,
               "Invalid ASTC header: block %ux%ux%u, image %ux%ux%u.\n",
               bx, by, bz, w, h, dz);
        return AVERROR_INVALIDDATA;
    }

    /* Multiply the block counts in 64 bits: the dimensions fit 24 bits, but
     * their product overflows 32 bits for large images, and a wrapped result
     * would under-size the packet, or wrap to zero and look like EOF. */
    expected = (((uint64_t)w  + bx - 1) / bx) *
               (((uint64_t)h  + by - 1) / by) *
               (((uint64_t)dz + bz - 1) / bz) * ASTC_BLOCK_BYTES;
    if (expected > INT_MAX) {
        av_log(s, AV_LOG_ERROR, "ASTC image is too large (%"PRIu64" bytes).\n",
               expected);
        return AVERROR_INVALIDDATA;
    }
    astc->image_size = expected;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_ASTC;
    st->codecpar->width      = w;
    st->codecpar->height     = h;
    st->codecpar->format     = AV_PIX_FMT_RGBA;

    if (ff_alloc_extradata(st->codecpar, ASTC_HEADER_SIZE) < 0)
        return AVERROR(ENOMEM);
    memcpy(st->codecpar->extradata, hdr, ASTC_HEADER_SIZE);

    /* Single image container. */
    st->duration = 1;
    avpriv_set_pts_info(st, 64, 1, 1);

    return 0;
}

static int astc_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ASTCDemuxerContext *astc = s->priv_data;
    int ret;

    /* Keep reporting a detected truncation instead of falling through to EOF:
     * a caller that stops at the first error must not see the broken image as
     * a clean end of stream. */
    if (astc->truncated)
        return AVERROR_INVALIDDATA;

    if (!astc->image_size)
        return AVERROR_EOF;

    /* Read exactly one image worth of blocks; a short read means the input
     * is truncated, and any trailing data is not part of this image. A payload
     * that is missing entirely reads as EOF, which has to be reported as the
     * truncation it is rather than as the end of a valid stream. */
    ret = av_get_packet(s->pb, pkt, astc->image_size);
    if (ret == AVERROR_EOF)
        ret = 0;
    if (ret < 0)
        return ret;
    if (ret != astc->image_size) {
        av_log(s, AV_LOG_ERROR,
               "Truncated ASTC image: got %d of %"PRIu64" bytes.\n",
               ret, astc->image_size);
        av_packet_unref(pkt);
        astc->truncated = 1;
        return AVERROR_INVALIDDATA;
    }

    astc->image_size = 0;
    pkt->stream_index = 0;
    pkt->flags |= AV_PKT_FLAG_KEY;
    return 0;
}

const FFInputFormat ff_astc_demuxer = {
    .p.name         = "astc",
    .p.long_name    = NULL_IF_CONFIG_SMALL("ASTC (Adaptive Scalable Texture Compression)"),
    .p.mime_type    = "image/astc",
    .p.extensions   = "astc",
    .p.flags        = AVFMT_NOTIMESTAMPS,
    .priv_data_size = sizeof(ASTCDemuxerContext),
    .read_probe     = astc_probe,
    .read_header    = astc_read_header,
    .read_packet    = astc_read_packet,
};
