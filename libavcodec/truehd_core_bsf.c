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
#include "mlp_parse.h"
#include "mlp.h"

typedef struct AccessUnit {
    uint8_t bits[4];
    uint16_t offset;
    uint16_t optional;
} AccessUnit;

typedef struct TrueHDCoreContext {
    const AVClass *class;

    MLPHeaderInfo hdr;
} TrueHDCoreContext;

static int truehd_core_filter(AVBSFContext *ctx, AVPacket *out)
{
    TrueHDCoreContext *s = ctx->priv_data;
    GetBitContext gbc;
    AccessUnit units[MAX_SUBSTREAMS];
    AVPacket *in;
    int ret, i, size, last_offset = 0;
    int in_size, out_size;
    int have_header = 0;
    int substream_bits = 0;
    int start, end;
    uint16_t dts;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;

    if (in->size < 4)
        goto fail;

    ret = init_get_bits(&gbc, in->data, 32);
    if (ret < 0)
        goto fail;

    skip_bits(&gbc, 4);
    in_size = get_bits(&gbc, 12) * 2;
    if (in_size < 4 || in_size > in->size)
        goto fail;

    out_size = in_size;
    dts = get_bits(&gbc, 16);

    ret = init_get_bits8(&gbc, in->data + 4, in->size - 4);
    if (ret < 0)
        goto fail;

    if (show_bits_long(&gbc, 32) == 0xf8726fba) {
        if ((ret = ff_mlp_read_major_sync(ctx, &s->hdr, &gbc)) != 0)
            goto fail;
        have_header = 1;
    }

    if (s->hdr.num_substreams > MAX_SUBSTREAMS)
        goto fail;

    start = get_bits_count(&gbc);
    for (i = 0; i < s->hdr.num_substreams; i++) {
        for (int j = 0; j < 4; j++)
            units[i].bits[j] = get_bits1(&gbc);

        units[i].offset = get_bits(&gbc, 12) * 2;
        if (i < FFMIN(s->hdr.num_substreams, 3)) {
            last_offset = units[i].offset;
            substream_bits += 16;
        }

        if (units[i].bits[0]) {
            units[i].optional = get_bits(&gbc, 16);
            if (i < FFMIN(s->hdr.num_substreams, 3))
                substream_bits += 16;
        }
    }
    end = get_bits_count(&gbc);

    size = ((end + 7) >> 3) + 4 + last_offset;
    if (size >= 0 && size <= in->size)
        out_size = size;
    if (out_size < in_size) {
        int bpos = 0, reduce = (end - start - substream_bits) >> 4;
        uint16_t parity_nibble = 0;
        uint16_t auheader;

        ret = av_new_packet(out, out_size);
        if (ret < 0)
            goto fail;

        AV_WB16(out->data + 2, dts);
        parity_nibble = dts;
        out->size -= reduce * 2;
        parity_nibble ^= out->size / 2;

        if (out_size > 8)
            AV_WN64(out->data + out_size - 8, 0);
        if (have_header) {
            memcpy(out->data + 4, in->data + 4, 28);
            out->data[16 + 4] = (out->data[16 + 4] & 0x0f) | (FFMIN(s->hdr.num_substreams, 3) << 4);
            out->data[25 + 4] = out->data[25 + 4] & 0xfe;
            out->data[26 + 4] = 0xff;
            out->data[27 + 4] = 0xff;
            AV_WL16(out->data + 4 + 26, ff_mlp_checksum16(out->data + 4, 26));
        }

        for (i = 0; i < FFMIN(s->hdr.num_substreams, 3); i++) {
            uint16_t substr_hdr = 0;

            substr_hdr |= (units[i].bits[0] << 15);
            substr_hdr |= (units[i].bits[1] << 14);
            substr_hdr |= (units[i].bits[2] << 13);
            substr_hdr |= (units[i].bits[3] << 12);
            substr_hdr |= (units[i].offset / 2) & 0x0FFF;

            AV_WB16(out->data + have_header * 28 + 4 + bpos, substr_hdr);

            parity_nibble ^= out->data[have_header * 28 + 4 + bpos++];
            parity_nibble ^= out->data[have_header * 28 + 4 + bpos++];

            if (units[i].bits[0]) {
                AV_WB16(out->data + have_header * 28 + 4 + bpos, units[i].optional);

                parity_nibble ^= out->data[have_header * 28 + 4 + bpos++];
                parity_nibble ^= out->data[have_header * 28 + 4 + bpos++];
            }
        }

        parity_nibble ^= parity_nibble >> 8;
        parity_nibble ^= parity_nibble >> 4;
        parity_nibble &= 0xF;

        memcpy(out->data + have_header * 28 + 4 + bpos,
               in->data + 4 + (end >> 3),
               out_size - (4 + (end >> 3)));
        auheader  = (parity_nibble ^ 0xF) << 12;
        auheader |= (out->size / 2) & 0x0fff;
        AV_WB16(out->data, auheader);

        ret = av_packet_copy_props(out, in);
    } else {
        av_packet_move_ref(out, in);
    }

fail:
    if (ret < 0)
        av_packet_unref(out);
    av_packet_free(&in);

    return ret;
}

static void truehd_core_flush(AVBSFContext *ctx)
{
    TrueHDCoreContext *s = ctx->priv_data;
    memset(&s->hdr, 0, sizeof(s->hdr));
}

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_TRUEHD, AV_CODEC_ID_NONE,
};

const AVBitStreamFilter ff_truehd_core_bsf = {
    .name           = "truehd_core",
    .priv_data_size = sizeof(TrueHDCoreContext),
    .filter         = truehd_core_filter,
    .flush          = truehd_core_flush,
    .codec_ids      = codec_ids,
};
