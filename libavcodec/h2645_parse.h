/*
 * H.264/HEVC common parsing code
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_H2645_PARSE_H
#define AVCODEC_H2645_PARSE_H

#include <stdint.h>

#include "avcodec.h"
#include "get_bits.h"

typedef struct H2645NAL {
    uint8_t *rbsp_buffer;
    int rbsp_buffer_size;

    int size;
    const uint8_t *data;

    /**
     * Size, in bits, of just the data, excluding the stop bit and any trailing
     * padding. I.e. what HEVC calls SODB.
     */
    int size_bits;

    int raw_size;
    const uint8_t *raw_data;

    GetBitContext gb;

    /**
     * NAL unit type
     */
    int type;

    /**
     * HEVC only, nuh_temporal_id_plus_1 - 1
     */
    int temporal_id;

    /**
     * H.264 only, nal_ref_idc
     */
    int ref_idc;
} H2645NAL;

/* an input packet split into unescaped NAL units */
typedef struct H2645Packet {
    H2645NAL *nals;
    int nb_nals;
    int nals_allocated;
} H2645Packet;

/**
 * Extract the raw (unescaped) bitstream.
 */
int ff_h2645_extract_rbsp(const uint8_t *src, int length,
                          H2645NAL *nal);

/**
 * Split an input packet into NAL units.
 */
int ff_h2645_packet_split(H2645Packet *pkt, const uint8_t *buf, int length,
                          void *logctx, int is_nalff, int nal_length_size,
                          enum AVCodecID codec_id);

/**
 * Free all the allocated memory in the packet.
 */
void ff_h2645_packet_uninit(H2645Packet *pkt);

#endif /* AVCODEC_H2645_PARSE_H */
