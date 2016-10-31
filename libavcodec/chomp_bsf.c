/*
 * Chomp bitstream filter
 * Copyright (c) 2010 Alex Converse <alex.converse@gmail.com>
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
#include "internal.h"

static int chomp_filter(AVBSFContext *ctx, AVPacket *out)
{
    AVPacket *in;
    int ret;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    while (in->size > 0 && !in->data[in->size - 1])
        in->size--;

    av_packet_move_ref(out, in);
    av_packet_free(&in);

    return 0;
}

/**
 * This filter removes a string of NULL bytes from the end of a packet.
 */
const AVBitStreamFilter ff_chomp_bsf = {
    .name   = "chomp",
    .filter = chomp_filter,
};
