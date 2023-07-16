/*
 * DOVI ISO Media common code
 *
 * Copyright (c) 2020 Vacing Fang <vacingfang@tencent.com>
 * Copyright (c) 2021 quietvoid
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

#include "libavutil/dovi_meta.h"

#include "libavcodec/put_bits.h"

#include "avformat.h"
#include "dovi_isom.h"

int ff_isom_parse_dvcc_dvvc(void *logctx, AVStream *st,
                            const uint8_t *buf_ptr, uint64_t size)
{
    uint32_t buf;
    AVDOVIDecoderConfigurationRecord *dovi;
    size_t dovi_size;

    if (size > (1 << 30) || size < 4)
        return AVERROR_INVALIDDATA;

    dovi = av_dovi_alloc(&dovi_size);
    if (!dovi)
        return AVERROR(ENOMEM);

    dovi->dv_version_major = *buf_ptr++;    // 8 bits
    dovi->dv_version_minor = *buf_ptr++;    // 8 bits

    buf = *buf_ptr++ << 8;
    buf |= *buf_ptr++;

    dovi->dv_profile        = (buf >> 9) & 0x7f;    // 7 bits
    dovi->dv_level          = (buf >> 3) & 0x3f;    // 6 bits
    dovi->rpu_present_flag  = (buf >> 2) & 0x01;    // 1 bit
    dovi->el_present_flag   = (buf >> 1) & 0x01;    // 1 bit
    dovi->bl_present_flag   =  buf       & 0x01;    // 1 bit

    // Has enough remaining data
    if (size >= 5) {
        dovi->dv_bl_signal_compatibility_id = ((*buf_ptr++) >> 4) & 0x0f; // 4 bits
    } else {
        // 0 stands for None
        // Dolby Vision V1.2.93 profiles and levels
        dovi->dv_bl_signal_compatibility_id = 0;
    }

    if (!av_packet_side_data_add(&st->codecpar->coded_side_data, &st->codecpar->nb_coded_side_data,
                                 AV_PKT_DATA_DOVI_CONF, (uint8_t *)dovi, dovi_size, 0)) {
        av_free(dovi);
        return AVERROR(ENOMEM);
    }

    av_log(logctx, AV_LOG_TRACE, "DOVI in dvcC/dvvC/dvwC box, version: %d.%d, profile: %d, level: %d, "
           "rpu flag: %d, el flag: %d, bl flag: %d, compatibility id: %d\n",
           dovi->dv_version_major, dovi->dv_version_minor,
           dovi->dv_profile, dovi->dv_level,
           dovi->rpu_present_flag,
           dovi->el_present_flag,
           dovi->bl_present_flag,
           dovi->dv_bl_signal_compatibility_id);

    return 0;
}

void ff_isom_put_dvcc_dvvc(void *logctx, uint8_t out[ISOM_DVCC_DVVC_SIZE],
                           const AVDOVIDecoderConfigurationRecord *dovi)
{
    PutBitContext pb;

    init_put_bits(&pb, out, ISOM_DVCC_DVVC_SIZE);

    put_bits(&pb, 8, dovi->dv_version_major);
    put_bits(&pb, 8, dovi->dv_version_minor);
    put_bits(&pb, 7, dovi->dv_profile & 0x7f);
    put_bits(&pb, 6, dovi->dv_level & 0x3f);
    put_bits(&pb, 1, !!dovi->rpu_present_flag);
    put_bits(&pb, 1, !!dovi->el_present_flag);
    put_bits(&pb, 1, !!dovi->bl_present_flag);
    put_bits(&pb, 4, dovi->dv_bl_signal_compatibility_id & 0x0f);

    put_bits(&pb, 28, 0); /* reserved */
    put_bits32(&pb, 0); /* reserved */
    put_bits32(&pb, 0); /* reserved */
    put_bits32(&pb, 0); /* reserved */
    put_bits32(&pb, 0); /* reserved */

    flush_put_bits(&pb);

    av_log(logctx, AV_LOG_DEBUG,
           "DOVI in %s box, version: %d.%d, profile: %d, level: %d, "
           "rpu flag: %d, el flag: %d, bl flag: %d, compatibility id: %d\n",
           dovi->dv_profile > 10 ? "dvwC" : (dovi->dv_profile > 7 ? "dvvC" : "dvcC"),
           dovi->dv_version_major, dovi->dv_version_minor,
           dovi->dv_profile, dovi->dv_level,
           dovi->rpu_present_flag,
           dovi->el_present_flag,
           dovi->bl_present_flag,
           dovi->dv_bl_signal_compatibility_id);
}
