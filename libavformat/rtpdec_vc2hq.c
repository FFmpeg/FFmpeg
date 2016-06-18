/*
 * RTP parser for VC-2 HQ payload format (draft version 1) - experimental
 * Copyright (c) 2016 Thomas Volkert <thomas@netzeal.de>
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
#include "libavcodec/dirac.h"

#include "avio_internal.h"
#include "rtpdec_formats.h"

#define RTP_VC2HQ_PL_HEADER_SIZE             4

#define DIRAC_DATA_UNIT_HEADER_SIZE          13
#define DIRAC_PIC_NR_SIZE                    4
#define DIRAC_RTP_PCODE_HQ_PIC_FRAGMENT      0xEC

struct PayloadContext {
    AVIOContext *buf;
    uint32_t    frame_size;
    uint32_t    frame_nr;
    uint32_t    timestamp;
    uint32_t    last_unit_size;
    int         seen_sequence_header;
};

static const uint8_t start_sequence[] = { 'B', 'B', 'C', 'D' };

static void fill_parse_info_header(PayloadContext *pl_ctx, uint8_t *buf,
                                   uint8_t parse_code, uint32_t data_unit_size)
{
    memcpy(buf, start_sequence, sizeof(start_sequence));
    buf[4]  = parse_code;
    AV_WB32(&buf[5], data_unit_size);
    AV_WB32(&buf[9], pl_ctx->last_unit_size);

    pl_ctx->last_unit_size = data_unit_size;
}

static int vc2hq_handle_sequence_header(PayloadContext *pl_ctx, AVStream *st, AVPacket *pkt,
                                        const uint8_t *buf, int len)
{
    int res;
    uint32_t size = DIRAC_DATA_UNIT_HEADER_SIZE + len;

    if ((res = av_new_packet(pkt, DIRAC_DATA_UNIT_HEADER_SIZE + len)) < 0)
        return res;

    fill_parse_info_header(pl_ctx, pkt->data, 0x00, size);
    /* payload of seq. header */
    memcpy(pkt->data + DIRAC_DATA_UNIT_HEADER_SIZE, buf, len);
    pkt->stream_index = st->index;

    pl_ctx->seen_sequence_header = 1;

    return 0;
}

static int vc2hq_mark_end_of_sequence(PayloadContext *pl_ctx, AVStream *st, AVPacket *pkt)
{
    int res;
    uint32_t size = 0;

    /* create A/V packet */
    if ((res = av_new_packet(pkt, DIRAC_DATA_UNIT_HEADER_SIZE)) < 0)
        return res;

    fill_parse_info_header(pl_ctx, pkt->data, 0x10, size);
    pkt->stream_index = st->index;

    pl_ctx->seen_sequence_header = 0;

    return 0;
}

static int vc2hq_handle_frame_fragment(AVFormatContext *ctx, PayloadContext *pl_ctx, AVStream *st,
                                       AVPacket *pkt, uint32_t *timestamp, const uint8_t *buf, int len,
                                       int flags)
{
    int res;
    uint32_t pic_nr;
    uint16_t frag_len;
    uint16_t no_slices;

    /* sanity check for size of input packet: 16 bytes header in any case as minimum */
    if (len < 16) {
        av_log(ctx, AV_LOG_ERROR, "Too short RTP/VC2hq packet, got %d bytes\n", len);
        return AVERROR_INVALIDDATA;
    }

    pic_nr = AV_RB32(&buf[4]);
    frag_len = AV_RB16(&buf[12]);
    no_slices = AV_RB16(&buf[14]);

    if (pl_ctx->buf && pl_ctx->frame_nr != pic_nr) {
        av_log(ctx, AV_LOG_WARNING, "Dropping buffered RTP/VC2hq packet fragments - non-continuous picture numbers\n");
        ffio_free_dyn_buf(&pl_ctx->buf);
    }

    /* transform parameters? */
    if (no_slices == 0) {
        if (len < frag_len + 16) {
            av_log(ctx, AV_LOG_ERROR, "Too short RTP/VC2hq packet, got %d bytes\n", len);
            return AVERROR_INVALIDDATA;
        }

        /* start frame buffering with new dynamic buffer */
        if (!pl_ctx->buf) {

            res = avio_open_dyn_buf(&pl_ctx->buf);
            if (res < 0)
                return res;

            /* reserve memory for frame header */
            res = avio_seek(pl_ctx->buf, DIRAC_DATA_UNIT_HEADER_SIZE + DIRAC_PIC_NR_SIZE, SEEK_SET);
            if (res < 0)
                return res;

            pl_ctx->frame_nr = pic_nr;
            pl_ctx->timestamp = *timestamp;
            pl_ctx->frame_size = DIRAC_DATA_UNIT_HEADER_SIZE + DIRAC_PIC_NR_SIZE;
        }

        avio_write(pl_ctx->buf, buf + 16 /* skip pl header */, frag_len);
        pl_ctx->frame_size += frag_len;

        return AVERROR(EAGAIN);
    } else {
        if (len < frag_len + 20) {
            av_log(ctx, AV_LOG_ERROR, "Too short RTP/VC2hq packet, got %d bytes\n", len);
            return AVERROR_INVALIDDATA;
        }

        /* transform parameters were missed, no buffer available */
        if (!pl_ctx->buf)
            return AVERROR_INVALIDDATA;

        avio_write(pl_ctx->buf, buf + 20 /* skip pl header */, frag_len);
        pl_ctx->frame_size += frag_len;

        /* RTP marker bit means: last fragment of current frame was received;
           otherwise, an additional fragment is needed for the current frame */
        if (!(flags & RTP_FLAG_MARKER))
            return AVERROR(EAGAIN);
    }

    /* close frame buffering and create A/V packet */
    res = ff_rtp_finalize_packet(pkt, &pl_ctx->buf, st->index);
    if (res < 0)
        return res;

    fill_parse_info_header(pl_ctx, pkt->data, DIRAC_PCODE_PICTURE_HQ, pl_ctx->frame_size);
    AV_WB32(&pkt->data[13], pl_ctx->frame_nr);

    pl_ctx->frame_size = 0;

    return 0;
}

static int vc2hq_handle_packet(AVFormatContext *ctx, PayloadContext *pl_ctx,
                               AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                               const uint8_t *buf, int len, uint16_t seq,
                               int flags)
{
    uint8_t parse_code = 0;
    int res = 0;

    if (pl_ctx->buf && pl_ctx->timestamp != *timestamp) {
        av_log(ctx, AV_LOG_WARNING, "Dropping buffered RTP/VC2hq packet fragments - non-continuous timestamps\n");
        ffio_free_dyn_buf(&pl_ctx->buf);
        pl_ctx->frame_size = 0;
    }

    /* sanity check for size of input packet: needed header data as minimum */
    if (len < RTP_VC2HQ_PL_HEADER_SIZE) {
        av_log(ctx, AV_LOG_ERROR, "Too short RTP/VC2hq packet, got %d bytes\n", len);
        return AVERROR_INVALIDDATA;
    }

    parse_code = buf[3];

    /* wait for next sequence header? */
    if (pl_ctx->seen_sequence_header || parse_code == DIRAC_PCODE_SEQ_HEADER) {
        switch(parse_code) {
        /* sequence header */
        case DIRAC_PCODE_SEQ_HEADER:
            res = vc2hq_handle_sequence_header(pl_ctx, st, pkt, buf + RTP_VC2HQ_PL_HEADER_SIZE, len - RTP_VC2HQ_PL_HEADER_SIZE);
            break;
        /* end of sequence */
        case DIRAC_PCODE_END_SEQ:
            res = vc2hq_mark_end_of_sequence(pl_ctx, st, pkt);
            break;
        /* HQ picture fragment */
        case DIRAC_RTP_PCODE_HQ_PIC_FRAGMENT:
            res = vc2hq_handle_frame_fragment(ctx, pl_ctx, st, pkt, timestamp, buf, len, flags);
            break;
        }
    }

    return res;
}

RTPDynamicProtocolHandler ff_vc2hq_dynamic_handler = {
    .enc_name         = "VC2",
    .codec_type       = AVMEDIA_TYPE_VIDEO,
    .codec_id         = AV_CODEC_ID_DIRAC,
    .priv_data_size   = sizeof(PayloadContext),
    .parse_packet     = vc2hq_handle_packet
};
