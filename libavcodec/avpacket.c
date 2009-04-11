/*
 * AVPacket functions for libavcodec
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#include "avcodec.h"


void av_destruct_packet_nofree(AVPacket *pkt)
{
    pkt->data = NULL; pkt->size = 0;
}

void av_destruct_packet(AVPacket *pkt)
{
    av_free(pkt->data);
    pkt->data = NULL; pkt->size = 0;
}

void av_init_packet(AVPacket *pkt)
{
    pkt->pts   = AV_NOPTS_VALUE;
    pkt->dts   = AV_NOPTS_VALUE;
    pkt->pos   = -1;
    pkt->duration = 0;
    pkt->convergence_duration = 0;
    pkt->flags = 0;
    pkt->stream_index = 0;
    pkt->destruct= NULL;
}

int av_new_packet(AVPacket *pkt, int size)
{
    uint8_t *data;
    if((unsigned)size > (unsigned)size + FF_INPUT_BUFFER_PADDING_SIZE)
        return AVERROR(ENOMEM);
    data = av_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!data)
        return AVERROR(ENOMEM);
    memset(data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    av_init_packet(pkt);
    pkt->data = data;
    pkt->size = size;
    pkt->destruct = av_destruct_packet;
    return 0;
}

void av_shrink_packet(AVPacket *pkt, int size)
{
    if (pkt->size <= size) return;
    pkt->size = size;
    memset(pkt->data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
}

int av_dup_packet(AVPacket *pkt)
{
    if (((pkt->destruct == av_destruct_packet_nofree) || (pkt->destruct == NULL)) && pkt->data) {
        uint8_t *data;
        /* We duplicate the packet and don't forget to add the padding again. */
        if((unsigned)pkt->size > (unsigned)pkt->size + FF_INPUT_BUFFER_PADDING_SIZE)
            return AVERROR(ENOMEM);
        data = av_malloc(pkt->size + FF_INPUT_BUFFER_PADDING_SIZE);
        if (!data) {
            return AVERROR(ENOMEM);
        }
        memcpy(data, pkt->data, pkt->size);
        memset(data + pkt->size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
        pkt->data = data;
        pkt->destruct = av_destruct_packet;
    }
    return 0;
}
