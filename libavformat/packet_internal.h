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

#ifndef AVFORMAT_PACKET_INTERNAL_H
#define AVFORMAT_PACKET_INTERNAL_H

#include "libavcodec/packet.h"

typedef struct PacketListEntry {
    struct PacketListEntry *next;
    AVPacket pkt;
} PacketListEntry;

typedef struct PacketList {
    PacketListEntry *head, *tail;
} PacketList;

#define FF_PACKETLIST_FLAG_PREPEND (1 << 0) /**< Prepend created AVPacketList instead of appending */

/**
 * Append an AVPacket to the list.
 *
 * @param list  A PacketList
 * @param pkt   The packet being appended. The data described in it will
 *              be made reference counted if it isn't already.
 * @param copy  A callback to copy the contents of the packet to the list.
                May be null, in which case the packet's reference will be
                moved to the list.
 * @return 0 on success, negative AVERROR value on failure. On failure,
           the packet and the list are unchanged.
 */
int ff_packet_list_put(PacketList *list, AVPacket *pkt,
                       int (*copy)(AVPacket *dst, const AVPacket *src),
                       int flags);

/**
 * Remove the oldest AVPacket in the list and return it.
 *
 * @note The pkt will be overwritten completely on success. The caller
 *       owns the packet and must unref it by itself.
 *
 * @param head A pointer to a PacketList struct
 * @param pkt  Pointer to an AVPacket struct
 * @return 0 on success, and a packet is returned. AVERROR(EAGAIN) if
 *         the list was empty.
 */
int ff_packet_list_get(PacketList *list, AVPacket *pkt);

/**
 * Wipe the list and unref all the packets in it.
 */
void ff_packet_list_free(PacketList *list);

#endif // AVFORMAT_PACKET_INTERNAL_H
