/*
 * H.264/HEVC common parsing code
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

#ifndef AVCODEC_H2645_PARSE_H
#define AVCODEC_H2645_PARSE_H

#include <stdint.h>

#include "avcodec.h"
#include "get_bits.h"

#define MAX_MBPAIR_SIZE (256*1024) // a tighter bound could be calculated if someone cares about a few bytes

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

    int skipped_bytes;
    int skipped_bytes_pos_size;
    int *skipped_bytes_pos;
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
                          H2645NAL *nal, int small_padding);

/**
 * Split an input packet into NAL units.
 */
int ff_h2645_packet_split(H2645Packet *pkt, const uint8_t *buf, int length,
                          void *logctx, int is_nalff, int nal_length_size,
                          enum AVCodecID codec_id, int small_padding);

/**
 * Free all the allocated memory in the packet.
 */
void ff_h2645_packet_uninit(H2645Packet *pkt);

static inline int get_nalsize(int nal_length_size, const uint8_t *buf,
                              int buf_size, int *buf_index, void *logctx)
{
    int i, nalsize = 0;

    if (*buf_index >= buf_size - nal_length_size) {
        // the end of the buffer is reached, refill it
        return AVERROR(EAGAIN);
    }

    for (i = 0; i < nal_length_size; i++)
        nalsize = ((unsigned)nalsize << 8) | buf[(*buf_index)++];
    if (nalsize <= 0 || nalsize > buf_size - *buf_index) {
        av_log(logctx, AV_LOG_ERROR,
               "Invalid NAL unit size (%d > %d).\n", nalsize, buf_size - *buf_index);
        return AVERROR_INVALIDDATA;
    }
    return nalsize;
}

#endif /* AVCODEC_H2645_PARSE_H */
