/*
 * Media 100 to MJPEGB bitstream filter
 * Copyright (c) 2023 Paul B Mahol
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
 * Media 100 to MJPEGB bitstream filter.
 */

#include "libavutil/intreadwrite.h"
#include "bsf.h"
#include "bsf_internal.h"
#include "bytestream.h"

static av_cold int init(AVBSFContext *ctx)
{
    ctx->par_out->codec_id = AV_CODEC_ID_MJPEGB;
    return 0;
}

static int filter(AVBSFContext *ctx, AVPacket *out)
{
    unsigned second_field_offset = 0;
    unsigned next_field = 0;
    unsigned dht_offset[2];
    unsigned dqt_offset[2];
    unsigned sod_offset[2];
    unsigned sof_offset[2];
    unsigned sos_offset[2];
    unsigned field = 0;
    GetByteContext gb;
    PutByteContext pb;
    AVPacket *in;
    int ret;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    ret = av_new_packet(out, in->size + 1024);
    if (ret < 0)
        goto fail;

    bytestream2_init(&gb, in->data, in->size);
    bytestream2_init_writer(&pb, out->data, out->size);

second_field:
    bytestream2_put_be32(&pb, 0);
    bytestream2_put_be32(&pb, AV_RB32("mjpg"));
    bytestream2_put_be32(&pb, 0);
    bytestream2_put_be32(&pb, 0);
    for (int i = 0; i < 6; i++)
        bytestream2_put_be32(&pb, 0);

    sof_offset[field] = bytestream2_tell_p(&pb);
    bytestream2_put_be16(&pb, 17);
    bytestream2_put_byte(&pb, 8);
    bytestream2_put_be16(&pb, ctx->par_in->height / 2);
    bytestream2_put_be16(&pb, ctx->par_in->width);
    bytestream2_put_byte(&pb, 3);
    bytestream2_put_byte(&pb, 1);
    bytestream2_put_byte(&pb, 0x21);
    bytestream2_put_byte(&pb, 0);
    bytestream2_put_byte(&pb, 2);
    bytestream2_put_byte(&pb, 0x11);
    bytestream2_put_byte(&pb, 1);
    bytestream2_put_byte(&pb, 3);
    bytestream2_put_byte(&pb, 0x11);
    bytestream2_put_byte(&pb, 1);

    sos_offset[field] = bytestream2_tell_p(&pb);
    bytestream2_put_be16(&pb, 12);
    bytestream2_put_byte(&pb, 3);
    bytestream2_put_byte(&pb, 1);
    bytestream2_put_byte(&pb, 0);
    bytestream2_put_byte(&pb, 2);
    bytestream2_put_byte(&pb, 0x11);
    bytestream2_put_byte(&pb, 3);
    bytestream2_put_byte(&pb, 0x11);
    bytestream2_put_byte(&pb, 0);
    bytestream2_put_byte(&pb, 0);
    bytestream2_put_byte(&pb, 0);

    dqt_offset[field] = bytestream2_tell_p(&pb);
    bytestream2_put_be16(&pb, 132);
    bytestream2_put_byte(&pb, 0);
    bytestream2_skip(&gb, 4);
    for (int i = 0; i < 64; i++)
        bytestream2_put_byte(&pb, bytestream2_get_be32(&gb));
    bytestream2_put_byte(&pb, 1);
    for (int i = 0; i < 64; i++)
        bytestream2_put_byte(&pb, bytestream2_get_be32(&gb));

    dht_offset[field] = 0;
    sod_offset[field] = bytestream2_tell_p(&pb);

    for (int i = bytestream2_tell(&gb) + 8; next_field == 0 && i < in->size - 4; i++) {
        if (AV_RB32(in->data + i) == 0x00000001) {
            next_field = i;
            break;
        }
    }

    bytestream2_skip(&gb, 8);
    bytestream2_copy_buffer(&pb, &gb, next_field - bytestream2_tell(&gb));
    bytestream2_put_be64(&pb, 0);

    if (field == 0) {
        field = 1;
        second_field_offset = bytestream2_tell_p(&pb);
        next_field = in->size;
        goto second_field;
    }

    AV_WB32(out->data +  8, second_field_offset);
    AV_WB32(out->data + 12, second_field_offset);
    AV_WB32(out->data + 16, second_field_offset);
    AV_WB32(out->data + 20, dqt_offset[0]);
    AV_WB32(out->data + 24, dht_offset[0]);
    AV_WB32(out->data + 28, sof_offset[0]);
    AV_WB32(out->data + 32, sos_offset[0]);
    AV_WB32(out->data + 36, sod_offset[0]);

    AV_WB32(out->data + second_field_offset +  8, bytestream2_tell_p(&pb) - second_field_offset);
    AV_WB32(out->data + second_field_offset + 12, bytestream2_tell_p(&pb) - second_field_offset);
    AV_WB32(out->data + second_field_offset + 16, 0);
    AV_WB32(out->data + second_field_offset + 20, dqt_offset[1] - second_field_offset);
    AV_WB32(out->data + second_field_offset + 24, dht_offset[1]);
    AV_WB32(out->data + second_field_offset + 28, sof_offset[1] - second_field_offset);
    AV_WB32(out->data + second_field_offset + 32, sos_offset[1] - second_field_offset);
    AV_WB32(out->data + second_field_offset + 36, sod_offset[1] - second_field_offset);

    out->size = bytestream2_tell_p(&pb);

    ret = av_packet_copy_props(out, in);
    if (ret < 0)
        goto fail;

fail:
    if (ret < 0)
        av_packet_unref(out);
    av_packet_free(&in);
    return ret;
}

const FFBitStreamFilter ff_media100_to_mjpegb_bsf = {
    .p.name         = "media100_to_mjpegb",
    .p.codec_ids    = (const enum AVCodecID []){ AV_CODEC_ID_MEDIA100, AV_CODEC_ID_NONE },
    .init           = init,
    .filter         = filter,
};
