/*
 * Copyright (c) 2016 Paul B Mahol
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
#include "bytestream.h"
#include "dca_syncwords.h"
#include "libavutil/mem.h"

static int dca_core_filter(AVBSFContext *ctx, AVPacket *out)
{
    AVPacket *in;
    GetByteContext gb;
    uint32_t syncword;
    int core_size = 0, ret;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    bytestream2_init(&gb, in->data, in->size);
    syncword = bytestream2_get_be32(&gb);
    bytestream2_skip(&gb, 1);

    switch (syncword) {
    case DCA_SYNCWORD_CORE_BE:
        core_size = ((bytestream2_get_be24(&gb) >> 4) & 0x3fff) + 1;
        break;
    }

    av_packet_move_ref(out, in);
    av_packet_free(&in);

    if (core_size > 0 && core_size <= out->size) {
        out->size = core_size;
    }

    return 0;
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_DTS, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_dca_core_bsf = {
    .name      = "dca_core",
    .filter    = dca_core_filter,
    .codec_ids = codec_ids,
};
