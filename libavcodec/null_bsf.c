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

/**
 * @file
 * Null bitstream filter -- pass the input through unchanged.
 */

#include "avcodec.h"
#include "bsf.h"

static int null_filter(AVBSFContext *ctx, AVPacket *out)
{
    AVPacket *in;
    int ret;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;
    av_packet_move_ref(out, in);
    av_packet_free(&in);
    return 0;
}

const AVBitStreamFilter ff_null_bsf = {
    .name           = "null",
    .filter         = null_filter,
};
