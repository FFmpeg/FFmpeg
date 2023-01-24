/*
 * AV1 common parsing code
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

#include "config.h"

#include "libavutil/mem.h"

#include "av1.h"
#include "av1_parse.h"
#include "bytestream.h"

int ff_av1_extract_obu(AV1OBU *obu, const uint8_t *buf, int length, void *logctx)
{
    int64_t obu_size;
    int start_pos, type, temporal_id, spatial_id;
    int len;

    len = parse_obu_header(buf, length, &obu_size, &start_pos,
                           &type, &temporal_id, &spatial_id);
    if (len < 0)
        return len;

    obu->type        = type;
    obu->temporal_id = temporal_id;
    obu->spatial_id  = spatial_id;

    obu->data     = buf + start_pos;
    obu->size     = obu_size;
    obu->raw_data = buf;
    obu->raw_size = len;

    av_log(logctx, AV_LOG_DEBUG,
           "obu_type: %d, temporal_id: %d, spatial_id: %d, payload size: %d\n",
           obu->type, obu->temporal_id, obu->spatial_id, obu->size);

    return len;
}

int ff_av1_packet_split(AV1Packet *pkt, const uint8_t *buf, int length, void *logctx)
{
    GetByteContext bc;
    int consumed;

    bytestream2_init(&bc, buf, length);
    pkt->nb_obus = 0;

    while (bytestream2_get_bytes_left(&bc) > 0) {
        AV1OBU *obu;

        if (pkt->obus_allocated < pkt->nb_obus + 1) {
            int new_size = pkt->obus_allocated + 1;
            AV1OBU *tmp;

            if (new_size >= INT_MAX / sizeof(*tmp))
                return AVERROR(ENOMEM);
            tmp = av_fast_realloc(pkt->obus, &pkt->obus_allocated_size, new_size * sizeof(*tmp));
            if (!tmp)
                return AVERROR(ENOMEM);

            pkt->obus = tmp;
            memset(pkt->obus + pkt->obus_allocated, 0, sizeof(*pkt->obus));
            pkt->obus_allocated = new_size;
        }
        obu = &pkt->obus[pkt->nb_obus];

        consumed = ff_av1_extract_obu(obu, bc.buffer, bytestream2_get_bytes_left(&bc), logctx);
        if (consumed < 0)
            return consumed;

        bytestream2_skip(&bc, consumed);

        obu->size_bits = get_obu_bit_length(obu->data, obu->size, obu->type);

        if (obu->size_bits < 0 ||
            (obu->size_bits == 0 && (obu->type != AV1_OBU_TEMPORAL_DELIMITER &&
                                     obu->type != AV1_OBU_PADDING))) {
            av_log(logctx, AV_LOG_ERROR, "Invalid OBU of type %d, skipping.\n", obu->type);
            continue;
        }

        pkt->nb_obus++;
    }

    return 0;
}

void ff_av1_packet_uninit(AV1Packet *pkt)
{
    av_freep(&pkt->obus);
    pkt->obus_allocated = pkt->obus_allocated_size = 0;
}

AVRational ff_av1_framerate(int64_t ticks_per_frame, int64_t units_per_tick,
                            int64_t time_scale)
{
    AVRational fr;

    if (ticks_per_frame && units_per_tick && time_scale &&
        ticks_per_frame < INT64_MAX / units_per_tick    &&
        av_reduce(&fr.den, &fr.num, units_per_tick * ticks_per_frame,
                  time_scale, INT_MAX))
        return fr;

    return (AVRational){ 0, 1 };
}
