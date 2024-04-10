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

#include "bsf.h"
#include "bsf_internal.h"
#include "get_bits.h"
#include "mlp_parse.h"
#include "mlp.h"

typedef struct AccessUnit {
    uint8_t bits[4];
    uint16_t offset;
    uint16_t optional;
} AccessUnit;

typedef struct TrueHDCoreContext {
    MLPHeaderInfo hdr;
} TrueHDCoreContext;

static int truehd_core_filter(AVBSFContext *ctx, AVPacket *pkt)
{
    TrueHDCoreContext *s = ctx->priv_data;
    GetBitContext gbc;
    AccessUnit units[MAX_SUBSTREAMS];
    int ret, i, last_offset = 0;
    int in_size, out_size;
    int have_header = 0;
    int substream_bytes = 0;
    int end;

    ret = ff_bsf_get_packet_ref(ctx, pkt);
    if (ret < 0)
        return ret;

    if (pkt->size < 4) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    in_size = (AV_RB16(pkt->data) & 0xFFF) * 2;
    if (in_size < 4 || in_size > pkt->size) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    ret = init_get_bits8(&gbc, pkt->data + 4, pkt->size - 4);
    if (ret < 0)
        goto fail;

    if (show_bits_long(&gbc, 32) == 0xf8726fba) {
        if ((ret = ff_mlp_read_major_sync(ctx, &s->hdr, &gbc)) < 0)
            goto fail;
        have_header = 1;
    }

    if (s->hdr.num_substreams > MAX_SUBSTREAMS) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    for (i = 0; i < s->hdr.num_substreams; i++) {
        for (int j = 0; j < 4; j++)
            units[i].bits[j] = get_bits1(&gbc);

        units[i].offset = get_bits(&gbc, 12);
        if (i < 3) {
            last_offset = units[i].offset * 2;
            substream_bytes += 2;
        }

        if (units[i].bits[0]) {
            units[i].optional = get_bits(&gbc, 16);
            if (i < 3)
                substream_bytes += 2;
        }
    }
    end = get_bits_count(&gbc) >> 3;

    out_size = end + 4 + last_offset;
    if (out_size < in_size) {
        int bpos = 0, reduce = end - have_header * 28 - substream_bytes;
        uint16_t parity_nibble, dts = AV_RB16(pkt->data + 2);
        uint16_t auheader;
        uint8_t header[28];

        av_assert1(reduce >= 0 && reduce % 2 == 0);

        if (have_header) {
            memcpy(header, pkt->data + 4, 28);
            header[16]  = (header[16] & 0x0c) | (FFMIN(s->hdr.num_substreams, 3) << 4);
            header[17] &= 0x7f;
            header[25] &= 0xfe;
            AV_WL16(header + 26, ff_mlp_checksum16(header, 26));
        }

        pkt->data += reduce;
        out_size  -= reduce;
        pkt->size  = out_size;

        ret = av_packet_make_writable(pkt);
        if (ret < 0)
            goto fail;

        AV_WB16(pkt->data + 2, dts);
        parity_nibble = dts;
        parity_nibble ^= out_size / 2;

        for (i = 0; i < FFMIN(s->hdr.num_substreams, 3); i++) {
            uint16_t substr_hdr = 0;

            substr_hdr |= (units[i].bits[0] << 15);
            substr_hdr |= (units[i].bits[1] << 14);
            substr_hdr |= (units[i].bits[2] << 13);
            substr_hdr |= (units[i].bits[3] << 12);
            substr_hdr |=  units[i].offset;

            AV_WB16(pkt->data + have_header * 28 + 4 + bpos, substr_hdr);

            parity_nibble ^= substr_hdr;
            bpos          += 2;

            if (units[i].bits[0]) {
                AV_WB16(pkt->data + have_header * 28 + 4 + bpos, units[i].optional);

                parity_nibble ^= units[i].optional;
                bpos          += 2;
            }
        }

        parity_nibble ^= parity_nibble >> 8;
        parity_nibble ^= parity_nibble >> 4;
        parity_nibble &= 0xF;

        auheader  = (parity_nibble ^ 0xF) << 12;
        auheader |= (out_size / 2) & 0x0fff;
        AV_WB16(pkt->data, auheader);

        if (have_header)
            memcpy(pkt->data + 4, header, 28);
    }

fail:
    if (ret < 0)
        av_packet_unref(pkt);

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

const FFBitStreamFilter ff_truehd_core_bsf = {
    .p.name         = "truehd_core",
    .p.codec_ids    = codec_ids,
    .priv_data_size = sizeof(TrueHDCoreContext),
    .filter         = truehd_core_filter,
    .flush          = truehd_core_flush,
};
