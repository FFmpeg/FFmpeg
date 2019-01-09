/*
 * Copyright (c) 2018 Paul B Mahol
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
#include "bsf.h"
#include "get_bits.h"
#include "ac3_parser_internal.h"

static int eac3_core_filter(AVBSFContext *ctx, AVPacket *pkt)
{
    AC3HeaderInfo hdr;
    GetBitContext gbc;
    int ret;

    ret = ff_bsf_get_packet_ref(ctx, pkt);
    if (ret < 0)
        return ret;
    ret = init_get_bits8(&gbc, pkt->data, pkt->size);
    if (ret < 0)
        goto fail;

    ret = ff_ac3_parse_header(&gbc, &hdr);
    if (ret < 0) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    if (hdr.frame_type == EAC3_FRAME_TYPE_INDEPENDENT ||
        hdr.frame_type == EAC3_FRAME_TYPE_AC3_CONVERT) {
        pkt->size = FFMIN(hdr.frame_size, pkt->size);
    } else if (hdr.frame_type == EAC3_FRAME_TYPE_DEPENDENT && pkt->size > hdr.frame_size) {
        AC3HeaderInfo hdr2;

        ret = init_get_bits8(&gbc, pkt->data + hdr.frame_size, pkt->size - hdr.frame_size);
        if (ret < 0)
            goto fail;

        ret = ff_ac3_parse_header(&gbc, &hdr2);
        if (ret < 0) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        if (hdr2.frame_type == EAC3_FRAME_TYPE_INDEPENDENT ||
            hdr2.frame_type == EAC3_FRAME_TYPE_AC3_CONVERT) {
            pkt->size -= hdr.frame_size;
            pkt->data += hdr.frame_size;
        } else {
            pkt->size = 0;
        }
    } else {
        pkt->size = 0;
    }

    return 0;
fail:
    av_packet_unref(pkt);
    return ret;
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_EAC3, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_eac3_core_bsf = {
    .name      = "eac3_core",
    .filter    = eac3_core_filter,
    .codec_ids = codec_ids,
};
