/*
 * Resolume DXV decoder
 * Copyright (C) 2015 Vittorio Giovara <vittorio.giovara@gmail.com>
 * Copyright (C) 2018 Paul B Mahol
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

#include <stdint.h>

#include "libavutil/imgutils.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "dxv.h"
#include "lzf.h"
#include "texturedsp.h"
#include "thread.h"

typedef struct DXVContext {
    TextureDSPContext texdsp;
    GetByteContext gbc;

    uint8_t *tex_data;   // Compressed texture
    uint8_t *ctex_data;  // Compressed chroma texture

    int64_t tex_size;    // Texture size
    int64_t ctex_size;   // Chroma texture size

    uint8_t *op_data[4]; // Opcodes
    int64_t op_size[4];  // Opcodes size
} DXVContext;

/* This scheme addresses already decoded elements depending on 2-bit status:
 *   0 -> copy new element
 *   1 -> copy one element from position -x
 *   2 -> copy one element from position -(get_byte() + 2) * x
 *   3 -> copy one element from position -(get_16le() + 0x102) * x
 * x is always 2 for dxt1 and 4 for dxt5. */
#define CHECKPOINT(x)                                                         \
    do {                                                                      \
        if (state == 0) {                                                     \
            if (bytestream2_get_bytes_left(gbc) < 4)                          \
                return AVERROR_INVALIDDATA;                                   \
            value = bytestream2_get_le32(gbc);                                \
            state = 16;                                                       \
        }                                                                     \
        op = value & 0x3;                                                     \
        value >>= 2;                                                          \
        state--;                                                              \
        switch (op) {                                                         \
        case 1:                                                               \
            idx = x;                                                          \
            break;                                                            \
        case 2:                                                               \
            idx = (bytestream2_get_byte(gbc) + 2) * x;                        \
            if (idx > pos) {                                                  \
                av_log(avctx, AV_LOG_ERROR, "idx %d > %d\n", idx, pos);       \
                return AVERROR_INVALIDDATA;                                   \
            }                                                                 \
            break;                                                            \
        case 3:                                                               \
            idx = (bytestream2_get_le16(gbc) + 0x102) * x;                    \
            if (idx > pos) {                                                  \
                av_log(avctx, AV_LOG_ERROR, "idx %d > %d\n", idx, pos);       \
                return AVERROR_INVALIDDATA;                                   \
            }                                                                 \
            break;                                                            \
        }                                                                     \
    } while(0)

static int dxv_decompress_dxt1(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;
    GetByteContext *gbc = &ctx->gbc;
    uint32_t value, prev, op;
    int idx = 0, state = 0;
    int pos = 2;

    /* Copy the first two elements */
    AV_WL32(ctx->tex_data, bytestream2_get_le32(gbc));
    AV_WL32(ctx->tex_data + 4, bytestream2_get_le32(gbc));

    /* Process input until the whole texture has been filled */
    while (pos + 2 <= ctx->tex_size / 4) {
        CHECKPOINT(2);

        /* Copy two elements from a previous offset or from the input buffer */
        if (op) {
            prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;

            prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
        } else {
            CHECKPOINT(2);

            if (op)
                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            else
                prev = bytestream2_get_le32(gbc);
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;

            CHECKPOINT(2);

            if (op)
                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            else
                prev = bytestream2_get_le32(gbc);
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
        }
    }

    return 0;
}

typedef struct OpcodeTable {
    int16_t next;
    uint8_t val1;
    uint8_t val2;
} OpcodeTable;

static int fill_ltable(GetByteContext *gb, uint32_t *table, int *nb_elements)
{
    unsigned half = 512, bits = 1023, left = 1024, input, mask;
    int value, counter = 0, rshift = 10, lshift = 30;

    mask = bytestream2_get_le32(gb) >> 2;
    while (left) {
        if (counter >= 256)
            return AVERROR_INVALIDDATA;
        value = bits & mask;
        left -= bits & mask;
        mask >>= rshift;
        lshift -= rshift;
        table[counter++] = value;
        if (lshift < 16) {
            if (bytestream2_get_bytes_left(gb) <= 0)
                return AVERROR_INVALIDDATA;

            input = bytestream2_get_le16(gb);
            mask += input << lshift;
            lshift += 16;
        }
        if (left < half) {
            half >>= 1;
            bits >>= 1;
            rshift--;
        }
    }

    for (; !table[counter - 1]; counter--)
        if (counter <= 0)
            return AVERROR_INVALIDDATA;

    *nb_elements = counter;

    if (counter < 256)
        memset(&table[counter], 0, 4 * (256 - counter));

    if (lshift >= 16)
        bytestream2_seek(gb, -2, SEEK_CUR);

    return 0;
}

static int fill_optable(unsigned *table0, OpcodeTable *table1, int nb_elements)
{
    unsigned table2[256] = { 0 };
    unsigned x = 0;
    int val0, val1, i, j = 2, k = 0;

    table2[0] = table0[0];
    for (i = 0; i < nb_elements - 1; i++, table2[i] = val0) {
        val0 = table0[i + 1] + table2[i];
    }

    if (!table2[0]) {
        do {
            k++;
        } while (!table2[k]);
    }

    j = 2;
    for (i = 1024; i > 0; i--) {
        for (table1[x].val1 = k; k < 256 && j > table2[k]; k++);
        x = (x - 383) & 0x3FF;
        j++;
    }

    if (nb_elements > 0)
        memcpy(&table2[0], table0, 4 * nb_elements);

    for (i = 0; i < 1024; i++) {
        val0 = table1[i].val1;
        val1 = table2[val0];
        table2[val0]++;
        x = 31 - ff_clz(val1);
        if (x > 10)
            return AVERROR_INVALIDDATA;
        table1[i].val2 = 10 - x;
        table1[i].next = (val1 << table1[i].val2) - 1024;
    }

    return 0;
}

static int get_opcodes(GetByteContext *gb, uint32_t *table, uint8_t *dst, int op_size, int nb_elements)
{
    OpcodeTable optable[1024];
    int sum, x, val, lshift, rshift, ret, i, idx;
    int64_t size_in_bits;
    unsigned endoffset, newoffset, offset;
    unsigned next;
    const uint8_t *src = gb->buffer;

    ret = fill_optable(table, optable, nb_elements);
    if (ret < 0)
        return ret;

    size_in_bits = bytestream2_get_le32(gb);
    endoffset = ((size_in_bits + 7) >> 3) - 4;
    if ((int)endoffset <= 0 || bytestream2_get_bytes_left(gb) < endoffset)
        return AVERROR_INVALIDDATA;

    offset = endoffset;
    next = AV_RL32(src + endoffset);
    rshift = (((size_in_bits & 0xFF) - 1) & 7) + 15;
    lshift = 32 - rshift;
    idx = (next >> rshift) & 0x3FF;
    for (i = 0; i < op_size; i++) {
        dst[i] = optable[idx].val1;
        val = optable[idx].val2;
        sum = val + lshift;
        x = (next << lshift) >> 1 >> (31 - val);
        newoffset = offset - (sum >> 3);
        lshift = sum & 7;
        idx = x + optable[idx].next;
        offset = newoffset;
        if (offset > endoffset)
            return AVERROR_INVALIDDATA;
        next = AV_RL32(src + offset);
    }

    bytestream2_skip(gb, (size_in_bits + 7 >> 3) - 4);

    return 0;
}

static int dxv_decompress_opcodes(GetByteContext *gb, void *dstp, size_t op_size)
{
    int pos = bytestream2_tell(gb);
    int flag = bytestream2_peek_byte(gb);

    if ((flag & 3) == 0) {
        bytestream2_skip(gb, 1);
        bytestream2_get_buffer(gb, dstp, op_size);
    } else if ((flag & 3) == 1) {
        bytestream2_skip(gb, 1);
        memset(dstp, bytestream2_get_byte(gb), op_size);
    } else {
        uint32_t table[256];
        int ret, elements = 0;

        ret = fill_ltable(gb, table, &elements);
        if (ret < 0)
            return ret;
        ret = get_opcodes(gb, table, dstp, op_size, elements);
        if (ret < 0)
            return ret;
    }
    return bytestream2_tell(gb) - pos;
}

static int dxv_decompress_cgo(DXVContext *ctx, GetByteContext *gb,
                              uint8_t *tex_data, int tex_size,
                              uint8_t *op_data, int *oindex,
                              int op_size,
                              uint8_t **dstp, int *statep,
                              uint8_t **tab0, uint8_t **tab1,
                              int offset)
{
    uint8_t *dst = *dstp;
    uint8_t *tptr0, *tptr1, *tptr3;
    int oi = *oindex;
    int state = *statep;
    int opcode, v, vv;

    if (state <= 0) {
        if (oi >= op_size)
            return AVERROR_INVALIDDATA;
        opcode = op_data[oi++];
        if (!opcode) {
            v = bytestream2_get_byte(gb);
            if (v == 255) {
                do {
                    if (bytestream2_get_bytes_left(gb) <= 0)
                        return AVERROR_INVALIDDATA;
                    opcode = bytestream2_get_le16(gb);
                    v += opcode;
                } while (opcode == 0xFFFF);
            }
            AV_WL32(dst, AV_RL32(dst - (8 + offset)));
            AV_WL32(dst + 4, AV_RL32(dst - (4 + offset)));
            state = v + 4;
            goto done;
        }

        switch (opcode) {
        case 1:
            AV_WL32(dst, AV_RL32(dst - (8 + offset)));
            AV_WL32(dst + 4, AV_RL32(dst - (4 + offset)));
            break;
        case 2:
            vv = (8 + offset) * (bytestream2_get_le16(gb) + 1);
            if (vv < 0 || vv > dst - tex_data)
                return AVERROR_INVALIDDATA;
            tptr0 = dst - vv;
            v = AV_RL32(tptr0);
            AV_WL32(dst, AV_RL32(tptr0));
            AV_WL32(dst + 4, AV_RL32(tptr0 + 4));
            tab0[0x9E3779B1 * (uint16_t)v >> 24] = dst;
            tab1[0x9E3779B1 * (AV_RL32(dst + 2) & 0xFFFFFFu) >> 24] = dst + 2;
            break;
        case 3:
            AV_WL32(dst, bytestream2_get_le32(gb));
            AV_WL32(dst + 4, bytestream2_get_le32(gb));
            tab0[0x9E3779B1 * AV_RL16(dst) >> 24] = dst;
            tab1[0x9E3779B1 * (AV_RL32(dst + 2) & 0xFFFFFFu) >> 24] = dst + 2;
            break;
        case 4:
            tptr3 = tab1[bytestream2_get_byte(gb)];
            if (!tptr3)
                return AVERROR_INVALIDDATA;
            AV_WL16(dst, bytestream2_get_le16(gb));
            AV_WL16(dst + 2, AV_RL16(tptr3));
            dst[4] = tptr3[2];
            AV_WL16(dst + 5, bytestream2_get_le16(gb));
            dst[7] = bytestream2_get_byte(gb);
            tab0[0x9E3779B1 * AV_RL16(dst) >> 24] = dst;
            break;
        case 5:
            tptr3 = tab1[bytestream2_get_byte(gb)];
            if (!tptr3)
                return AVERROR_INVALIDDATA;
            AV_WL16(dst, bytestream2_get_le16(gb));
            AV_WL16(dst + 2, bytestream2_get_le16(gb));
            dst[4] = bytestream2_get_byte(gb);
            AV_WL16(dst + 5, AV_RL16(tptr3));
            dst[7] = tptr3[2];
            tab0[0x9E3779B1 * AV_RL16(dst) >> 24] = dst;
            tab1[0x9E3779B1 * (AV_RL32(dst + 2) & 0xFFFFFFu) >> 24] = dst + 2;
            break;
        case 6:
            tptr0 = tab1[bytestream2_get_byte(gb)];
            if (!tptr0)
                return AVERROR_INVALIDDATA;
            tptr1 = tab1[bytestream2_get_byte(gb)];
            if (!tptr1)
                return AVERROR_INVALIDDATA;
            AV_WL16(dst, bytestream2_get_le16(gb));
            AV_WL16(dst + 2, AV_RL16(tptr0));
            dst[4] = tptr0[2];
            AV_WL16(dst + 5, AV_RL16(tptr1));
            dst[7] = tptr1[2];
            tab0[0x9E3779B1 * AV_RL16(dst) >> 24] = dst;
            break;
        case 7:
            v = (8 + offset) * (bytestream2_get_le16(gb) + 1);
            if (v < 0 || v > dst - tex_data)
                return AVERROR_INVALIDDATA;
            tptr0 = dst - v;
            AV_WL16(dst, bytestream2_get_le16(gb));
            AV_WL16(dst + 2, AV_RL16(tptr0 + 2));
            AV_WL32(dst + 4, AV_RL32(tptr0 + 4));
            tab0[0x9E3779B1 * AV_RL16(dst) >> 24] = dst;
            tab1[0x9E3779B1 * (AV_RL32(dst + 2) & 0xFFFFFFu) >> 24] = dst + 2;
            break;
        case 8:
            tptr1 = tab0[bytestream2_get_byte(gb)];
            if (!tptr1)
                return AVERROR_INVALIDDATA;
            AV_WL16(dst, AV_RL16(tptr1));
            AV_WL16(dst + 2, bytestream2_get_le16(gb));
            AV_WL32(dst + 4, bytestream2_get_le32(gb));
            tab1[0x9E3779B1 * (AV_RL32(dst + 2) & 0xFFFFFFu) >> 24] = dst + 2;
            break;
        case 9:
            tptr1 = tab0[bytestream2_get_byte(gb)];
            if (!tptr1)
                return AVERROR_INVALIDDATA;
            tptr3 = tab1[bytestream2_get_byte(gb)];
            if (!tptr3)
                return AVERROR_INVALIDDATA;
            AV_WL16(dst, AV_RL16(tptr1));
            AV_WL16(dst + 2, AV_RL16(tptr3));
            dst[4] = tptr3[2];
            AV_WL16(dst + 5, bytestream2_get_le16(gb));
            dst[7] = bytestream2_get_byte(gb);
            tab1[0x9E3779B1 * (AV_RL32(dst + 2) & 0xFFFFFFu) >> 24] = dst + 2;
            break;
        case 10:
            tptr1 = tab0[bytestream2_get_byte(gb)];
            if (!tptr1)
                return AVERROR_INVALIDDATA;
            tptr3 = tab1[bytestream2_get_byte(gb)];
            if (!tptr3)
                return AVERROR_INVALIDDATA;
            AV_WL16(dst, AV_RL16(tptr1));
            AV_WL16(dst + 2, bytestream2_get_le16(gb));
            dst[4] = bytestream2_get_byte(gb);
            AV_WL16(dst + 5, AV_RL16(tptr3));
            dst[7] = tptr3[2];
            tab1[0x9E3779B1 * (AV_RL32(dst + 2) & 0xFFFFFFu) >> 24] = dst + 2;
            break;
        case 11:
            tptr0 = tab0[bytestream2_get_byte(gb)];
            if (!tptr0)
                return AVERROR_INVALIDDATA;
            tptr3 = tab1[bytestream2_get_byte(gb)];
            if (!tptr3)
                return AVERROR_INVALIDDATA;
            tptr1 = tab1[bytestream2_get_byte(gb)];
            if (!tptr1)
                return AVERROR_INVALIDDATA;
            AV_WL16(dst, AV_RL16(tptr0));
            AV_WL16(dst + 2, AV_RL16(tptr3));
            dst[4] = tptr3[2];
            AV_WL16(dst + 5, AV_RL16(tptr1));
            dst[7] = tptr1[2];
            break;
        case 12:
            tptr1 = tab0[bytestream2_get_byte(gb)];
            if (!tptr1)
                return AVERROR_INVALIDDATA;
            v = (8 + offset) * (bytestream2_get_le16(gb) + 1);
            if (v < 0 || v > dst - tex_data)
                return AVERROR_INVALIDDATA;
            tptr0 = dst - v;
            AV_WL16(dst, AV_RL16(tptr1));
            AV_WL16(dst + 2, AV_RL16(tptr0 + 2));
            AV_WL32(dst + 4, AV_RL32(tptr0 + 4));
            tab1[0x9E3779B1 * (AV_RL32(dst + 2) & 0xFFFFFFu) >> 24] = dst + 2;
            break;
        case 13:
            AV_WL16(dst, AV_RL16(dst - (8 + offset)));
            AV_WL16(dst + 2, bytestream2_get_le16(gb));
            AV_WL32(dst + 4, bytestream2_get_le32(gb));
            tab1[0x9E3779B1 * (AV_RL32(dst + 2) & 0xFFFFFFu) >> 24] = dst + 2;
            break;
        case 14:
            tptr3 = tab1[bytestream2_get_byte(gb)];
            if (!tptr3)
                return AVERROR_INVALIDDATA;
            AV_WL16(dst, AV_RL16(dst - (8 + offset)));
            AV_WL16(dst + 2, AV_RL16(tptr3));
            dst[4] = tptr3[2];
            AV_WL16(dst + 5, bytestream2_get_le16(gb));
            dst[7] = bytestream2_get_byte(gb);
            tab1[0x9E3779B1 * (AV_RL32(dst + 2) & 0xFFFFFFu) >> 24] = dst + 2;
            break;
        case 15:
            tptr3 = tab1[bytestream2_get_byte(gb)];
            if (!tptr3)
                return AVERROR_INVALIDDATA;
            AV_WL16(dst, AV_RL16(dst - (8 + offset)));
            AV_WL16(dst + 2, bytestream2_get_le16(gb));
            dst[4] = bytestream2_get_byte(gb);
            AV_WL16(dst + 5, AV_RL16(tptr3));
            dst[7] = tptr3[2];
            tab1[0x9E3779B1 * (AV_RL32(dst + 2) & 0xFFFFFFu) >> 24] = dst + 2;
            break;
        case 16:
            tptr3 = tab1[bytestream2_get_byte(gb)];
            if (!tptr3)
                return AVERROR_INVALIDDATA;
            tptr1 = tab1[bytestream2_get_byte(gb)];
            if (!tptr1)
                return AVERROR_INVALIDDATA;
            AV_WL16(dst, AV_RL16(dst - (8 + offset)));
            AV_WL16(dst + 2, AV_RL16(tptr3));
            dst[4] = tptr3[2];
            AV_WL16(dst + 5, AV_RL16(tptr1));
            dst[7] = tptr1[2];
            break;
        case 17:
            v = (8 + offset) * (bytestream2_get_le16(gb) + 1);
            if (v < 0 || v > dst - tex_data)
                return AVERROR_INVALIDDATA;
            AV_WL16(dst, AV_RL16(dst - (8 + offset)));
            AV_WL16(dst + 2, AV_RL16(&dst[-v + 2]));
            AV_WL32(dst + 4, AV_RL32(&dst[-v + 4]));
            tab1[0x9E3779B1 * (AV_RL32(dst + 2) & 0xFFFFFFu) >> 24] = dst + 2;
            break;
        default:
            break;
        }
    } else {
done:
        AV_WL32(dst, AV_RL32(dst - (8 + offset)));
        AV_WL32(dst + 4, AV_RL32(dst - (4 + offset)));
        state--;
    }
    if (dst - tex_data + 8 > tex_size)
        return AVERROR_INVALIDDATA;
    dst += 8;

    *oindex = oi;
    *dstp = dst;
    *statep = state;

    return 0;
}

static int dxv_decompress_cocg(DXVContext *ctx, GetByteContext *gb,
                               uint8_t *tex_data, int tex_size,
                               uint8_t *op_data0, uint8_t *op_data1,
                               int max_op_size0, int max_op_size1)
{
    uint8_t *dst, *tab2[256] = { 0 }, *tab0[256] = { 0 }, *tab3[256] = { 0 }, *tab1[256] = { 0 };
    int op_offset = bytestream2_get_le32(gb);
    unsigned op_size0 = bytestream2_get_le32(gb);
    unsigned op_size1 = bytestream2_get_le32(gb);
    int data_start = bytestream2_tell(gb);
    int skip0, skip1, oi0 = 0, oi1 = 0;
    int ret, state0 = 0, state1 = 0;

    if (op_offset < 12 || op_offset - 12 > bytestream2_get_bytes_left(gb))
        return AVERROR_INVALIDDATA;

    dst = tex_data;
    bytestream2_skip(gb, op_offset - 12);
    if (op_size0 > max_op_size0)
        return AVERROR_INVALIDDATA;
    skip0 = dxv_decompress_opcodes(gb, op_data0, op_size0);
    if (skip0 < 0)
        return skip0;
    if (op_size1 > max_op_size1)
        return AVERROR_INVALIDDATA;
    skip1 = dxv_decompress_opcodes(gb, op_data1, op_size1);
    if (skip1 < 0)
        return skip1;
    bytestream2_seek(gb, data_start, SEEK_SET);

    AV_WL32(dst, bytestream2_get_le32(gb));
    AV_WL32(dst + 4, bytestream2_get_le32(gb));
    AV_WL32(dst + 8, bytestream2_get_le32(gb));
    AV_WL32(dst + 12, bytestream2_get_le32(gb));

    tab0[0x9E3779B1 * AV_RL16(dst) >> 24] = dst;
    tab1[0x9E3779B1 * (AV_RL32(dst + 2) & 0xFFFFFF) >> 24] = dst + 2;
    tab2[0x9E3779B1 * AV_RL16(dst + 8) >> 24] = dst + 8;
    tab3[0x9E3779B1 * (AV_RL32(dst + 10) & 0xFFFFFF) >> 24] = dst + 10;
    dst += 16;
    while (dst + 10 < tex_data + tex_size) {
        ret = dxv_decompress_cgo(ctx, gb, tex_data, tex_size, op_data0, &oi0, op_size0,
                                 &dst, &state0, tab0, tab1, 8);
        if (ret < 0)
            return ret;
        ret = dxv_decompress_cgo(ctx, gb, tex_data, tex_size, op_data1, &oi1, op_size1,
                                 &dst, &state1, tab2, tab3, 8);
        if (ret < 0)
            return ret;
    }

    bytestream2_seek(gb, data_start - 12 + op_offset + skip0 + skip1, SEEK_SET);

    return 0;
}

static int dxv_decompress_yo(DXVContext *ctx, GetByteContext *gb,
                             uint8_t *tex_data, int tex_size,
                             uint8_t *op_data, int max_op_size)
{
    int op_offset = bytestream2_get_le32(gb);
    unsigned op_size = bytestream2_get_le32(gb);
    int data_start = bytestream2_tell(gb);
    uint8_t *dst, *table0[256] = { 0 }, *table1[256] = { 0 };
    int ret, state = 0, skip, oi = 0, v, vv;

    if (op_offset < 8 || op_offset - 8 > bytestream2_get_bytes_left(gb))
        return AVERROR_INVALIDDATA;

    dst = tex_data;
    bytestream2_skip(gb, op_offset - 8);
    if (op_size > max_op_size)
        return AVERROR_INVALIDDATA;
    skip = dxv_decompress_opcodes(gb, op_data, op_size);
    if (skip < 0)
        return skip;
    bytestream2_seek(gb, data_start, SEEK_SET);

    v = bytestream2_get_le32(gb);
    AV_WL32(dst, v);
    vv = bytestream2_get_le32(gb);
    table0[0x9E3779B1 * (uint16_t)v >> 24] = dst;
    AV_WL32(dst + 4, vv);
    table1[0x9E3779B1 * (AV_RL32(dst + 2) & 0xFFFFFF) >> 24] = dst + 2;
    dst += 8;

    while (dst < tex_data + tex_size) {
        ret = dxv_decompress_cgo(ctx, gb, tex_data, tex_size, op_data, &oi, op_size,
                                 &dst, &state, table0, table1, 0);
        if (ret < 0)
            return ret;
    }

    bytestream2_seek(gb, data_start + op_offset + skip - 8, SEEK_SET);

    return 0;
}

static int dxv_decompress_ycg6(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;
    GetByteContext *gb = &ctx->gbc;
    int ret;

    ret = dxv_decompress_yo(ctx, gb, ctx->tex_data, ctx->tex_size,
                            ctx->op_data[0], ctx->op_size[0]);
    if (ret < 0)
        return ret;

    return dxv_decompress_cocg(ctx, gb, ctx->ctex_data, ctx->ctex_size,
                               ctx->op_data[1], ctx->op_data[2],
                               ctx->op_size[1], ctx->op_size[2]);
}

static int dxv_decompress_yg10(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;
    GetByteContext *gb = &ctx->gbc;
    int ret;

    ret = dxv_decompress_cocg(ctx, gb, ctx->tex_data, ctx->tex_size,
                              ctx->op_data[0], ctx->op_data[3],
                              ctx->op_size[0], ctx->op_size[3]);
    if (ret < 0)
        return ret;

    return dxv_decompress_cocg(ctx, gb, ctx->ctex_data, ctx->ctex_size,
                               ctx->op_data[1], ctx->op_data[2],
                               ctx->op_size[1], ctx->op_size[2]);
}

static int dxv_decompress_dxt5(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;
    GetByteContext *gbc = &ctx->gbc;
    uint32_t value, op, prev;
    int idx, state = 0;
    int pos = 4;
    int run = 0;
    int probe, check;

    /* Copy the first four elements */
    AV_WL32(ctx->tex_data +  0, bytestream2_get_le32(gbc));
    AV_WL32(ctx->tex_data +  4, bytestream2_get_le32(gbc));
    AV_WL32(ctx->tex_data +  8, bytestream2_get_le32(gbc));
    AV_WL32(ctx->tex_data + 12, bytestream2_get_le32(gbc));

    /* Process input until the whole texture has been filled */
    while (pos + 2 <= ctx->tex_size / 4) {
        if (run) {
            run--;

            prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
            prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
        } else {
            if (bytestream2_get_bytes_left(gbc) < 1)
                return AVERROR_INVALIDDATA;
            if (state == 0) {
                value = bytestream2_get_le32(gbc);
                state = 16;
            }
            op = value & 0x3;
            value >>= 2;
            state--;

            switch (op) {
            case 0:
                /* Long copy */
                check = bytestream2_get_byte(gbc) + 1;
                if (check == 256) {
                    do {
                        probe = bytestream2_get_le16(gbc);
                        check += probe;
                    } while (probe == 0xFFFF);
                }
                while (check && pos + 4 <= ctx->tex_size / 4) {
                    prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                    AV_WL32(ctx->tex_data + 4 * pos, prev);
                    pos++;

                    prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                    AV_WL32(ctx->tex_data + 4 * pos, prev);
                    pos++;

                    prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                    AV_WL32(ctx->tex_data + 4 * pos, prev);
                    pos++;

                    prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                    AV_WL32(ctx->tex_data + 4 * pos, prev);
                    pos++;

                    check--;
                }

                /* Restart (or exit) the loop */
                continue;
                break;
            case 1:
                /* Load new run value */
                run = bytestream2_get_byte(gbc);
                if (run == 255) {
                    do {
                        probe = bytestream2_get_le16(gbc);
                        run += probe;
                    } while (probe == 0xFFFF);
                }

                /* Copy two dwords from previous data */
                prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;

                prev = AV_RL32(ctx->tex_data + 4 * (pos - 4));
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;
                break;
            case 2:
                /* Copy two dwords from a previous index */
                idx = 8 + 4 * bytestream2_get_le16(gbc);
                if (idx > pos || (unsigned int)(pos - idx) + 2 > ctx->tex_size / 4)
                    return AVERROR_INVALIDDATA;
                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;

                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;
                break;
            case 3:
                /* Copy two dwords from input */
                prev = bytestream2_get_le32(gbc);
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;

                prev = bytestream2_get_le32(gbc);
                AV_WL32(ctx->tex_data + 4 * pos, prev);
                pos++;
                break;
            }
        }

        CHECKPOINT(4);
        if (pos + 2 > ctx->tex_size / 4)
            return AVERROR_INVALIDDATA;

        /* Copy two elements from a previous offset or from the input buffer */
        if (op) {
            if (idx > pos || (unsigned int)(pos - idx) + 2 > ctx->tex_size / 4)
                return AVERROR_INVALIDDATA;
            prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;

            prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
        } else {
            CHECKPOINT(4);

            if (op && (idx > pos || (unsigned int)(pos - idx) + 2 > ctx->tex_size / 4))
                return AVERROR_INVALIDDATA;
            if (op)
                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            else
                prev = bytestream2_get_le32(gbc);
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;

            CHECKPOINT(4);

            if (op)
                prev = AV_RL32(ctx->tex_data + 4 * (pos - idx));
            else
                prev = bytestream2_get_le32(gbc);
            AV_WL32(ctx->tex_data + 4 * pos, prev);
            pos++;
        }
    }

    return 0;
}

static int dxv_decompress_lzf(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;
    return ff_lzf_uncompress(&ctx->gbc, &ctx->tex_data, &ctx->tex_size);
}

static int dxv_decompress_raw(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;
    GetByteContext *gbc = &ctx->gbc;

    if (bytestream2_get_bytes_left(gbc) < ctx->tex_size)
        return AVERROR_INVALIDDATA;

    bytestream2_get_buffer(gbc, ctx->tex_data, ctx->tex_size);
    return 0;
}

static int dxv_decode(AVCodecContext *avctx, AVFrame *frame,
                      int *got_frame, AVPacket *avpkt)
{
    DXVContext *ctx = avctx->priv_data;
    GetByteContext *gbc = &ctx->gbc;
    TextureDSPThreadContext texdsp_ctx, ctexdsp_ctx;
    int (*decompress_tex)(AVCodecContext *avctx);
    const char *msgcomp, *msgtext;
    uint32_t tag;
    int version_major, version_minor = 0;
    int size = 0, old_type = 0;
    int ret;

    bytestream2_init(gbc, avpkt->data, avpkt->size);

    avctx->pix_fmt = AV_PIX_FMT_RGBA;
    avctx->colorspace = AVCOL_SPC_RGB;

    tag = bytestream2_get_le32(gbc);
    switch (tag) {
    case DXV_FMT_DXT1:
        decompress_tex = dxv_decompress_dxt1;
        texdsp_ctx.tex_funct = ctx->texdsp.dxt1_block;
        texdsp_ctx.tex_ratio = 8;
        texdsp_ctx.raw_ratio = 16;
        msgcomp = "DXTR1";
        msgtext = "DXT1";
        break;
    case DXV_FMT_DXT5:
        decompress_tex = dxv_decompress_dxt5;
        /* DXV misnomers DXT5, alpha is premultiplied so use DXT4 instead */
        texdsp_ctx.tex_funct = ctx->texdsp.dxt4_block;
        texdsp_ctx.tex_ratio = 16;
        texdsp_ctx.raw_ratio = 16;
        msgcomp = "DXTR5";
        msgtext = "DXT5";
        break;
    case DXV_FMT_YCG6:
        decompress_tex = dxv_decompress_ycg6;
        texdsp_ctx.tex_funct  = ctx->texdsp.rgtc1u_gray_block;
        texdsp_ctx.tex_ratio  = 8;
        texdsp_ctx.raw_ratio  = 4;
        ctexdsp_ctx.tex_funct = ctx->texdsp.rgtc1u_gray_block;
        ctexdsp_ctx.tex_ratio = 16;
        ctexdsp_ctx.raw_ratio = 4;
        msgcomp = "YOCOCG6";
        msgtext = "YCG6";
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;
        avctx->colorspace = AVCOL_SPC_YCOCG;
        break;
    case DXV_FMT_YG10:
        decompress_tex = dxv_decompress_yg10;
        texdsp_ctx.tex_funct  = ctx->texdsp.rgtc1u_gray_block;
        texdsp_ctx.tex_ratio  = 16;
        texdsp_ctx.raw_ratio  = 4;
        ctexdsp_ctx.tex_funct = ctx->texdsp.rgtc1u_gray_block;
        ctexdsp_ctx.tex_ratio = 16;
        ctexdsp_ctx.raw_ratio = 4;
        msgcomp = "YAOCOCG10";
        msgtext = "YG10";
        avctx->pix_fmt = AV_PIX_FMT_YUVA420P;
        avctx->colorspace = AVCOL_SPC_YCOCG;
        break;
    default:
        /* Old version does not have a real header, just size and type. */
        size = tag & 0x00FFFFFF;
        old_type = tag >> 24;
        version_major = (old_type & 0x0F) - 1;

        if (old_type & 0x80) {
            msgcomp = "RAW";
            decompress_tex = dxv_decompress_raw;
        } else {
            msgcomp = "LZF";
            decompress_tex = dxv_decompress_lzf;
        }

        if (old_type & 0x40) {
            tag = DXV_FMT_DXT5;
            msgtext = "DXT5";

            texdsp_ctx.tex_funct = ctx->texdsp.dxt4_block;
            texdsp_ctx.tex_ratio = 16;
            texdsp_ctx.raw_ratio = 16;
        } else if (old_type & 0x20 || version_major == 1) {
            tag = DXV_FMT_DXT1;
            msgtext = "DXT1";

            texdsp_ctx.tex_funct = ctx->texdsp.dxt1_block;
            texdsp_ctx.tex_ratio = 8;
            texdsp_ctx.raw_ratio = 16;
        } else {
            av_log(avctx, AV_LOG_ERROR, "Unsupported header (0x%08"PRIX32")\n.", tag);
            return AVERROR_INVALIDDATA;
        }
        break;
    }

    texdsp_ctx.slice_count  = av_clip(avctx->thread_count, 1,
                                      avctx->coded_height / TEXTURE_BLOCK_H);
    ctexdsp_ctx.slice_count = av_clip(avctx->thread_count, 1,
                                      avctx->coded_height / 2 / TEXTURE_BLOCK_H);

    /* New header is 12 bytes long. */
    if (!old_type) {
        version_major = bytestream2_get_byte(gbc) - 1;
        version_minor = bytestream2_get_byte(gbc);

        /* Encoder copies texture data when compression is not advantageous. */
        if (bytestream2_get_byte(gbc)) {
            msgcomp = "RAW";
            decompress_tex = dxv_decompress_raw;
        }

        bytestream2_skip(gbc, 1); // unknown
        size = bytestream2_get_le32(gbc);
    }
    av_log(avctx, AV_LOG_DEBUG,
           "%s compression with %s texture (version %d.%d)\n",
           msgcomp, msgtext, version_major, version_minor);

    if (size != bytestream2_get_bytes_left(gbc)) {
        av_log(avctx, AV_LOG_ERROR,
               "Incomplete or invalid file (header %d, left %u).\n",
               size, bytestream2_get_bytes_left(gbc));
        return AVERROR_INVALIDDATA;
    }

    ctx->tex_size = avctx->coded_width  / (texdsp_ctx.raw_ratio / (avctx->pix_fmt == AV_PIX_FMT_RGBA ? 4 : 1)) *
                    avctx->coded_height / TEXTURE_BLOCK_H *
                    texdsp_ctx.tex_ratio;
    ret = av_reallocp(&ctx->tex_data, ctx->tex_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (ret < 0)
        return ret;

    if (avctx->pix_fmt != AV_PIX_FMT_RGBA) {
        int i;

        ctx->ctex_size = avctx->coded_width  / 2 / ctexdsp_ctx.raw_ratio *
                         avctx->coded_height / 2 / TEXTURE_BLOCK_H *
                         ctexdsp_ctx.tex_ratio;

        ctx->op_size[0] = avctx->coded_width * avctx->coded_height / 16;
        ctx->op_size[1] = avctx->coded_width * avctx->coded_height / 32;
        ctx->op_size[2] = avctx->coded_width * avctx->coded_height / 32;
        ctx->op_size[3] = avctx->coded_width * avctx->coded_height / 16;

        ret = av_reallocp(&ctx->ctex_data, ctx->ctex_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (ret < 0)
            return ret;
        for (i = 0; i < 4; i++) {
            ret = av_reallocp(&ctx->op_data[i], ctx->op_size[i]);
            if (ret < 0)
                return ret;
        }
    }

    /* Decompress texture out of the intermediate compression. */
    ret = decompress_tex(avctx);
    if (ret < 0)
        return ret;

    ret = ff_thread_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;

    texdsp_ctx.width   = avctx->coded_width;
    texdsp_ctx.height  = avctx->coded_height;
    ctexdsp_ctx.width  = avctx->coded_width  / 2;
    ctexdsp_ctx.height = avctx->coded_height / 2;
    switch (tag) {
    case DXV_FMT_YG10:
        /* BC5 texture with alpha in the second half of each block */
        texdsp_ctx.tex_data.in    = ctx->tex_data + texdsp_ctx.tex_ratio / 2;
        texdsp_ctx.frame_data.out = frame->data[3];
        texdsp_ctx.stride         = frame->linesize[3];
        ret = ff_texturedsp_exec_decompress_threads(avctx, &texdsp_ctx);
        if (ret < 0)
            return ret;
        /* fallthrough */
    case DXV_FMT_YCG6:
        /* BC5 texture with Co in the first half of each block and Cg in the second */
        ctexdsp_ctx.tex_data.in    = ctx->ctex_data;
        ctexdsp_ctx.frame_data.out = frame->data[2];
        ctexdsp_ctx.stride         = frame->linesize[2];
        ret = ff_texturedsp_exec_decompress_threads(avctx, &ctexdsp_ctx);
        if (ret < 0)
            return ret;
        ctexdsp_ctx.tex_data.in    = ctx->ctex_data + ctexdsp_ctx.tex_ratio / 2;
        ctexdsp_ctx.frame_data.out = frame->data[1];
        ctexdsp_ctx.stride         = frame->linesize[1];
        ret = ff_texturedsp_exec_decompress_threads(avctx, &ctexdsp_ctx);
        if (ret < 0)
            return ret;
        /* fallthrough */
    case DXV_FMT_DXT1:
    case DXV_FMT_DXT5:
        /* For DXT1 and DXT5, self explanatory
         * For YCG6, BC4 texture for Y
         * For YG10, BC5 texture with Y in the first half of each block */
        texdsp_ctx.tex_data.in    = ctx->tex_data;
        texdsp_ctx.frame_data.out = frame->data[0];
        texdsp_ctx.stride         = frame->linesize[0];
        ret = ff_texturedsp_exec_decompress_threads(avctx, &texdsp_ctx);
        if (ret < 0)
            return ret;
        break;
    }

    /* Frame is ready to be output. */
    *got_frame = 1;

    return avpkt->size;
}

static int dxv_init(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;
    int ret = av_image_check_size(avctx->width, avctx->height, 0, avctx);

    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid image size %dx%d.\n",
               avctx->width, avctx->height);
        return ret;
    }

    /* Since codec is based on 4x4 blocks, size is aligned to 4 */
    avctx->coded_width  = FFALIGN(avctx->width,  TEXTURE_BLOCK_W);
    avctx->coded_height = FFALIGN(avctx->height, TEXTURE_BLOCK_H);

    ff_texturedsp_init(&ctx->texdsp);

    return 0;
}

static int dxv_close(AVCodecContext *avctx)
{
    DXVContext *ctx = avctx->priv_data;

    av_freep(&ctx->tex_data);
    av_freep(&ctx->ctex_data);
    av_freep(&ctx->op_data[0]);
    av_freep(&ctx->op_data[1]);
    av_freep(&ctx->op_data[2]);
    av_freep(&ctx->op_data[3]);

    return 0;
}

const FFCodec ff_dxv_decoder = {
    .p.name         = "dxv",
    CODEC_LONG_NAME("Resolume DXV"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_DXV,
    .init           = dxv_init,
    FF_CODEC_DECODE_CB(dxv_decode),
    .close          = dxv_close,
    .priv_data_size = sizeof(DXVContext),
    .p.capabilities = AV_CODEC_CAP_DR1 |
                      AV_CODEC_CAP_SLICE_THREADS |
                      AV_CODEC_CAP_FRAME_THREADS,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
