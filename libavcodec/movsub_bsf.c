/*
 * Copyright (c) 2008 Reimar Döffinger
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

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "bsf.h"

static int text2movsub(AVBSFContext *ctx, AVPacket *out)
{
    AVPacket *in;
    int ret = 0;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    if (in->size > 0xffff) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    ret = av_new_packet(out, in->size + 2);
    if (ret < 0) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = av_packet_copy_props(out, in);
    if (ret < 0)
        goto fail;

    AV_WB16(out->data, in->size);
    memcpy(out->data + 2, in->data, in->size);

fail:
    if (ret < 0)
        av_packet_unref(out);
    av_packet_free(&in);
    return ret;
}

const AVBitStreamFilter ff_text2movsub_bsf = {
    .name   = "text2movsub",
    .filter = text2movsub,
};

static int mov2textsub(AVBSFContext *ctx, AVPacket *out)
{
    AVPacket *in;
    int ret = 0;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    if (in->size < 2) {
       ret = AVERROR_INVALIDDATA;
       goto fail;
    }

    ret = av_new_packet(out, FFMIN(in->size - 2, AV_RB16(in->data)));
    if (ret < 0)
        goto fail;

    ret = av_packet_copy_props(out, in);
    if (ret < 0)
        goto fail;

    memcpy(out->data, in->data + 2, out->size);

fail:
    if (ret < 0)
        av_packet_unref(out);
    av_packet_free(&in);
    return ret;
}

const AVBitStreamFilter ff_mov2textsub_bsf = {
    .name   = "mov2textsub",
    .filter = mov2textsub,
};
