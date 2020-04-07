/*
 * MJPEG A dump header bitstream filter
 * Copyright (c) 2006 Baptiste Coudurier
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

/**
 * @file
 * MJPEG A dump header bitstream filter
 * modifies bitstream to be decoded by quicktime
 */

#include "bsf.h"
#include "bsf_internal.h"
#include "bytestream.h"
#include "mjpeg.h"


static int mjpega_dump_header(AVBSFContext *ctx, AVPacket *out)
{
    AVPacket *in;
    uint8_t *out_buf;
    unsigned dqt = 0, dht = 0, sof0 = 0;
    int ret = 0, i;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    ret = av_new_packet(out, in->size + 44);
    if (ret < 0)
        goto fail;

    ret = av_packet_copy_props(out, in);
    if (ret < 0)
        goto fail;

    out_buf = out->data;
    bytestream_put_byte(&out_buf, 0xff);
    bytestream_put_byte(&out_buf, SOI);
    bytestream_put_byte(&out_buf, 0xff);
    bytestream_put_byte(&out_buf, APP1);
    bytestream_put_be16(&out_buf, 42); /* size */
    bytestream_put_be32(&out_buf, 0);
    bytestream_put_buffer(&out_buf, "mjpg", 4);
    bytestream_put_be32(&out_buf, in->size + 44); /* field size */
    bytestream_put_be32(&out_buf, in->size + 44); /* pad field size */
    bytestream_put_be32(&out_buf, 0);             /* next ptr */

    for (i = 0; i < in->size - 1; i++) {
        if (in->data[i] == 0xff) {
            switch (in->data[i + 1]) {
            case DQT:  dqt  = i + 46; break;
            case DHT:  dht  = i + 46; break;
            case SOF0: sof0 = i + 46; break;
            case SOS:
                bytestream_put_be32(&out_buf, dqt); /* quant off */
                bytestream_put_be32(&out_buf, dht); /* huff off */
                bytestream_put_be32(&out_buf, sof0); /* image off */
                bytestream_put_be32(&out_buf, i + 46); /* scan off */
                bytestream_put_be32(&out_buf, i + 46 + AV_RB16(in->data + i + 2)); /* data off */
                bytestream_put_buffer(&out_buf, in->data + 2, in->size - 2); /* skip already written SOI */

                out->size = out_buf - out->data;
                av_packet_free(&in);
                return 0;
            case APP1:
                if (i + 8 < in->size && AV_RL32(in->data + i + 8) == AV_RL32("mjpg")) {
                    av_log(ctx, AV_LOG_ERROR, "bitstream already formatted\n");
                    av_packet_unref(out);
                    av_packet_move_ref(out, in);
                    av_packet_free(&in);
                    return 0;
                }
            }
        }
    }
    av_log(ctx, AV_LOG_ERROR, "could not find SOS marker in bitstream\n");
fail:
    av_packet_unref(out);
    av_packet_free(&in);
    return AVERROR_INVALIDDATA;
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_MJPEG, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_mjpega_dump_header_bsf = {
    .name      = "mjpegadump",
    .filter    = mjpega_dump_header,
    .codec_ids = codec_ids,
};
