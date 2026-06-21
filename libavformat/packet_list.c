/*
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

#include <string.h>

#include "libavutil/avutil.h"
#include "libavutil/error.h"
#include "libavutil/mem.h"
#include "libavutil/rational.h"

#include "libavcodec/packet.h"

#include "packet_internal.h"

static void get_packet_defaults(AVPacket *pkt)
{
    memset(pkt, 0, sizeof(*pkt));

    pkt->pts             = AV_NOPTS_VALUE;
    pkt->dts             = AV_NOPTS_VALUE;
    pkt->pos             = -1;
    pkt->time_base       = av_make_q(0, 1);
}

int ff_packet_list_put(PacketList *packet_buffer,
                       AVPacket      *pkt,
                       int (*copy)(AVPacket *dst, const AVPacket *src),
                       int flags)
{
    PacketListEntry *pktl = av_malloc(sizeof(*pktl));
    unsigned int update_end_point = 1;
    int ret;

    if (!pktl)
        return AVERROR(ENOMEM);

    if (copy) {
        get_packet_defaults(&pktl->pkt);
        ret = copy(&pktl->pkt, pkt);
        if (ret < 0) {
            av_free(pktl);
            return ret;
        }
    } else {
        ret = av_packet_make_refcounted(pkt);
        if (ret < 0) {
            av_free(pktl);
            return ret;
        }
        av_packet_move_ref(&pktl->pkt, pkt);
    }

    pktl->next = NULL;

    if (packet_buffer->head) {
        if (flags & FF_PACKETLIST_FLAG_PREPEND) {
            pktl->next = packet_buffer->head;
            packet_buffer->head = pktl;
            update_end_point = 0;
        } else {
            packet_buffer->tail->next = pktl;
        }
    } else
        packet_buffer->head = pktl;

    if (update_end_point) {
        /* Add the packet in the buffered packet list. */
        packet_buffer->tail = pktl;
    }

    return 0;
}

int ff_packet_list_get(PacketList *pkt_buffer,
                       AVPacket      *pkt)
{
    PacketListEntry *pktl = pkt_buffer->head;
    if (!pktl)
        return AVERROR(EAGAIN);
    *pkt        = pktl->pkt;
    pkt_buffer->head = pktl->next;
    if (!pkt_buffer->head)
        pkt_buffer->tail = NULL;
    av_freep(&pktl);
    return 0;
}

void ff_packet_list_free(PacketList *pkt_buf)
{
    PacketListEntry *tmp = pkt_buf->head;

    while (tmp) {
        PacketListEntry *pktl = tmp;
        tmp = pktl->next;
        av_packet_unref(&pktl->pkt);
        av_freep(&pktl);
    }
    pkt_buf->head = pkt_buf->tail = NULL;
}
