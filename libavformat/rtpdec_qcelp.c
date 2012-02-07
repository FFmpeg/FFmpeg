/*
 * RTP Depacketization of QCELP/PureVoice, RFC 2658
 * Copyright (c) 2010 Martin Storsjo
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

#include "rtpdec_formats.h"

static const uint8_t frame_sizes[] = {
    1, 4, 8, 17, 35
};

typedef struct {
    int pos;
    int size;
    /* The largest frame is 35 bytes, only 10 frames are allowed per
     * packet, and we return the first one immediately, so allocate
     * space for 9 frames */
    uint8_t data[35*9];
} InterleavePacket;

struct PayloadContext {
    int interleave_size;
    int interleave_index;
    InterleavePacket group[6];
    int group_finished;

    /* The maximum packet size, 10 frames of 35 bytes each, and one
     * packet header byte. */
    uint8_t  next_data[1 + 35*10];
    int      next_size;
    uint32_t next_timestamp;
};

static PayloadContext *qcelp_new_context(void)
{
    return av_mallocz(sizeof(PayloadContext));
}

static void qcelp_free_context(PayloadContext *data)
{
    av_free(data);
}

static int return_stored_frame(AVFormatContext *ctx, PayloadContext *data,
                               AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                               const uint8_t *buf, int len);

static int store_packet(AVFormatContext *ctx, PayloadContext *data,
                        AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                        const uint8_t *buf, int len)
{
    int interleave_size, interleave_index;
    int frame_size, ret;
    InterleavePacket* ip;

    if (len < 2)
        return AVERROR_INVALIDDATA;

    interleave_size  = buf[0] >> 3 & 7;
    interleave_index = buf[0]      & 7;

    if (interleave_size > 5) {
        av_log(ctx, AV_LOG_ERROR, "Invalid interleave size %d\n",
                                   interleave_size);
        return AVERROR_INVALIDDATA;
    }
    if (interleave_index > interleave_size) {
        av_log(ctx, AV_LOG_ERROR, "Invalid interleave index %d/%d\n",
                                   interleave_index, interleave_size);
        return AVERROR_INVALIDDATA;
    }
    if (interleave_size != data->interleave_size) {
        int i;
        /* First packet, or changed interleave size */
        data->interleave_size = interleave_size;
        data->interleave_index = 0;
        for (i = 0; i < 6; i++)
            data->group[i].size = 0;
    }

    if (interleave_index < data->interleave_index) {
        /* Wrapped around - missed the last packet of the previous group. */
        if (data->group_finished) {
            /* No more data in the packets in this interleaving group, just
             * start processing the next one */
            data->interleave_index = 0;
        } else {
            /* Stash away the current packet, emit everything we have of the
             * previous group. */
            for (; data->interleave_index <= interleave_size;
                 data->interleave_index++)
                data->group[data->interleave_index].size = 0;

            if (len > sizeof(data->next_data))
                return AVERROR_INVALIDDATA;
            memcpy(data->next_data, buf, len);
            data->next_size = len;
            data->next_timestamp = *timestamp;
            *timestamp = RTP_NOTS_VALUE;

            data->interleave_index = 0;
            return return_stored_frame(ctx, data, st, pkt, timestamp, buf, len);
        }
    }
    if (interleave_index > data->interleave_index) {
        /* We missed a packet */
        for (; data->interleave_index < interleave_index;
             data->interleave_index++)
            data->group[data->interleave_index].size = 0;
    }
    data->interleave_index = interleave_index;

    if (buf[1] >= FF_ARRAY_ELEMS(frame_sizes))
        return AVERROR_INVALIDDATA;
    frame_size = frame_sizes[buf[1]];
    if (1 + frame_size > len)
        return AVERROR_INVALIDDATA;

    if (len - 1 - frame_size > sizeof(data->group[0].data))
        return AVERROR_INVALIDDATA;

    if ((ret = av_new_packet(pkt, frame_size)) < 0)
        return ret;
    memcpy(pkt->data, &buf[1], frame_size);
    pkt->stream_index = st->index;

    ip = &data->group[data->interleave_index];
    ip->size = len - 1 - frame_size;
    ip->pos = 0;
    memcpy(ip->data, &buf[1 + frame_size], ip->size);
    /* Each packet must contain the same number of frames according to the
     * RFC. If there's no data left in this packet, there shouldn't be any
     * in any of the other frames in the interleaving group either. */
    data->group_finished = ip->size == 0;

    if (interleave_index == interleave_size) {
        data->interleave_index = 0;
        return !data->group_finished;
    } else {
        data->interleave_index++;
        return 0;
    }
}

static int return_stored_frame(AVFormatContext *ctx, PayloadContext *data,
                               AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                               const uint8_t *buf, int len)
{
    InterleavePacket* ip = &data->group[data->interleave_index];
    int frame_size, ret;

    if (data->group_finished && data->interleave_index == 0) {
        *timestamp = data->next_timestamp;
        ret = store_packet(ctx, data, st, pkt, timestamp, data->next_data,
                           data->next_size);
        data->next_size = 0;
        return ret;
    }

    if (ip->size == 0) {
        /* No stored data for this interleave block, output an empty packet */
        if ((ret = av_new_packet(pkt, 1)) < 0)
            return ret;
        pkt->data[0] = 0; // Blank - could also be 14, Erasure
    } else {
        if (ip->pos >= ip->size)
            return AVERROR_INVALIDDATA;
        if (ip->data[ip->pos] >= FF_ARRAY_ELEMS(frame_sizes))
            return AVERROR_INVALIDDATA;
        frame_size = frame_sizes[ip->data[ip->pos]];
        if (ip->pos + frame_size > ip->size)
            return AVERROR_INVALIDDATA;

        if ((ret = av_new_packet(pkt, frame_size)) < 0)
            return ret;
        memcpy(pkt->data, &ip->data[ip->pos], frame_size);

        ip->pos += frame_size;
        data->group_finished = ip->pos >= ip->size;
    }
    pkt->stream_index = st->index;

    if (data->interleave_index == data->interleave_size) {
        data->interleave_index = 0;
        if (!data->group_finished)
            return 1;
        else
            return data->next_size > 0;
    } else {
        data->interleave_index++;
        return 1;
    }
}

static int qcelp_parse_packet(AVFormatContext *ctx, PayloadContext *data,
                              AVStream *st, AVPacket *pkt, uint32_t *timestamp,
                              const uint8_t *buf, int len, int flags)
{
    if (buf)
        return store_packet(ctx, data, st, pkt, timestamp, buf, len);
    else
        return return_stored_frame(ctx, data, st, pkt, timestamp, buf, len);
}

RTPDynamicProtocolHandler ff_qcelp_dynamic_handler = {
    .enc_name           = "x-Purevoice",
    .codec_type         = AVMEDIA_TYPE_AUDIO,
    .codec_id           = CODEC_ID_QCELP,
    .static_payload_id  = 12,
    .alloc              = qcelp_new_context,
    .free               = qcelp_free_context,
    .parse_packet       = qcelp_parse_packet
};
