/*
 * Brooktree ProSumer Video decoder
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

typedef struct ProSumerContext {
    GetByteContext gb;
    PutByteContext pb;

    unsigned stride;
    unsigned size;
    uint32_t lut[0x10000];
    uint8_t *table_b;
    uint8_t *decbuffer;
} ProSumerContext;

#define PAIR(high, low) (((uint64_t)(high) << 32) | low)

static int decompress(GetByteContext *gb, int size, PutByteContext *pb, const uint32_t *lut)
{
    int pos, idx, cnt, fill;
    uint32_t a, b, c;

    bytestream2_skip(gb, 32);
    cnt = 4;
    a = bytestream2_get_le32(gb);
    idx = a >> 20;
    b = lut[2 * idx];

    while (1) {
        if (((b & 0xFF00u) != 0x8000u) || (b & 0xFFu)) {
            if ((b & 0xFF00u) != 0x8000u) {
                bytestream2_put_le16(pb, b);
            } else if (b & 0xFFu) {
                idx = 0;
                for (int i = 0; i < (b & 0xFFu); i++)
                    bytestream2_put_le32(pb, 0);
            }
            c = b >> 16;
            if (c & 0xFF00u) {
                c = (((c >> 8) & 0xFFu) | (c & 0xFF00)) & 0xF00F;
                fill = lut[2 * idx + 1];
                if ((c & 0xFF00u) == 0x1000) {
                    bytestream2_put_le16(pb, fill);
                    c &= 0xFFFF00FFu;
                } else {
                    bytestream2_put_le32(pb, fill);
                    c &= 0xFFFF00FFu;
                }
            }
            while (c) {
                a <<= 4;
                cnt--;
                if (!cnt) {
                    if (bytestream2_get_bytes_left(gb) <= 0) {
                        if (!a)
                            return 0;
                        cnt = 4;
                    } else {
                        pos = bytestream2_tell(gb) ^ 2;
                        bytestream2_seek(gb, pos, SEEK_SET);
                        AV_WN16(&a, bytestream2_peek_le16(gb));
                        pos = pos ^ 2;
                        bytestream2_seek(gb, pos, SEEK_SET);
                        bytestream2_skip(gb, 2);
                        cnt = 4;
                    }
                }
                c--;
            }
            idx = a >> 20;
            b = lut[2 * idx];
            continue;
        }
        idx = 2;
        while (idx) {
            a <<= 4;
            cnt--;
            if (cnt) {
                idx--;
                continue;
            }
            if (bytestream2_get_bytes_left(gb) <= 0) {
                if (a) {
                    cnt = 4;
                    idx--;
                    continue;
                }
                return 0;
            }
            pos = bytestream2_tell(gb) ^ 2;
            bytestream2_seek(gb, pos, SEEK_SET);
            AV_WN16(&a, bytestream2_peek_le16(gb));
            pos = pos ^ 2;
            bytestream2_seek(gb, pos, SEEK_SET);
            bytestream2_skip(gb, 2);
            cnt = 4;
            idx--;
        }
        b = PAIR(4, a) >> 16;
    }

    return 0;
}

static void do_shift(uint32_t *dst, int offset, uint32_t *src, int stride, int height)
{
    uint32_t x = (0x7F7F7F7F >> 1) & 0x7F7F7F7F;

    dst += offset >> 2;

    for (int i = 0; i < height; i++) {
        for (int j = 0; j < stride >> 2; j++) {
            dst[j] = (((src[j] >> 3) + (x & dst[j])) << 3) & 0xFCFCFCFC;
        }

        dst += stride >> 2;
        src += stride >> 2;
    }
}

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *got_frame, AVPacket *avpkt)
{
    ProSumerContext *s = avctx->priv_data;
    AVFrame * const frame = data;
    int ret;

    if (avpkt->size <= 32)
        return AVERROR_INVALIDDATA;

    memset(s->decbuffer, 0, s->size);
    bytestream2_init(&s->gb, avpkt->data, avpkt->size);
    bytestream2_init_writer(&s->pb, s->decbuffer, s->size);

    decompress(&s->gb, AV_RL32(avpkt->data + 28) >> 1, &s->pb, s->lut);
    do_shift((uint32_t *)s->decbuffer, 0, (uint32_t *)s->table_b, s->stride, 1);
    do_shift((uint32_t *)s->decbuffer, s->stride, (uint32_t *)s->decbuffer, s->stride, avctx->height - 1);

    ret = ff_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;

    for (int i = avctx->height - 1; i >= 0 ; i--) {
        uint8_t *y = &frame->data[0][i * frame->linesize[0]];
        uint8_t *u = &frame->data[1][i * frame->linesize[1]];
        uint8_t *v = &frame->data[2][i * frame->linesize[2]];
        uint8_t *src = s->decbuffer + (avctx->height - 1 - i) * s->stride;

        for (int j = 0; j < avctx->width; j += 8) {
            *(u++) = *src++;
            *(y++) = *src++;
            *(v++) = *src++;
            *(y++) = *src++;

            *(u++) = *src++;
            *(y++) = *src++;
            *(v++) = *src++;
            *(y++) = *src++;

            *(y++) = *src++;
            *(y++) = *src++;
            *(y++) = *src++;
            *(y++) = *src++;
        }
    }

    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->key_frame = 1;
    *got_frame = 1;

    return avpkt->size;
}

static const uint32_t table[] = {
    0x0000, 0x10000001, 0x0101, 0x20000001, 0x0202, 0x30000001, 0xFFFF, 0x40000001, 0xFEFE, 0x50000001,
    0x0001, 0x70000001, 0x0100, 0x80000001, 0x00FF, 0x90000001, 0xFF00, 0xA0000001, 0x8001, 0x60000001,
    0x8002, 0xB0000001, 0xFCFC, 0x01000002, 0x0404, 0x03000002, 0x0002, 0xD3000002, 0xFEFC, 0x02000002,
    0xFCFE, 0x04000002, 0xFEFF, 0xD2000002, 0x0808, 0x06000002, 0xFFFE, 0x05000002, 0x0402, 0xC0000002,
    0x0204, 0xC1000002, 0xF8F8, 0xC3000002, 0x0201, 0xC4000002, 0x0102, 0xC6000002, 0x0804, 0xF3000002,
    0x0408, 0xE0000002, 0xF8FC, 0xE1000002, 0xFCF8, 0xC7000002, 0x00FE, 0xD0000002, 0xFE00, 0xD4000002,
    0xFF01, 0xD5000002, 0x01FF, 0xD6000002, 0x0200, 0xD7000002, 0xFCFF, 0xE2000002, 0x0104, 0xE3000002,
    0xF0F0, 0xE5000002, 0x0401, 0xE7000002, 0x02FE, 0xF0000002, 0xFE02, 0xF1000002, 0xFE01, 0xF2000002,
    0x01FE, 0xF4000002, 0xFF02, 0xF5000002, 0x02FF, 0xF6000002, 0x8003, 0xC2000002, 0x8004, 0x07000002,
    0x8005, 0xD1000002, 0x8006, 0xC5000002, 0x8007, 0xE6000002, 0x8008, 0xE4000002, 0x8009, 0xF7000002,
    0xFC02, 0x08000003, 0xFE04, 0x08100003, 0xFC00, 0x08200003, 0x02FC, 0x08300003, 0x1010, 0x08400003,
    0x00FC, 0x08500003, 0x0004, 0x08600003, 0x0400, 0x08700003, 0xFFFC, 0x08800003, 0x1008, 0x08900003,
    0x0810, 0x08A00003, 0x0802, 0x08B00003, 0x0208, 0x08C00003, 0xFEF8, 0x08D00003, 0xFC01, 0x08E00003,
    0x04FF, 0x08F00003, 0xF8FE, 0x09000003, 0xFC04, 0x09100003, 0x04FC, 0x09200003, 0xFF04, 0x09300003,
    0x01FC, 0x09400003, 0xF0F8, 0x09500003, 0xF8F0, 0x09600003, 0x04FE, 0x09700003, 0xF0FC, 0x09800003,
    0x0008, 0x09900003, 0x08FE, 0x09A00003, 0x01F8, 0x09B00003, 0x0800, 0x09C00003, 0x08FC, 0x09D00003,
    0xFE08, 0x09E00003, 0xFC08, 0x09F00003, 0xF800, 0x0A000003, 0x0108, 0x0A100003, 0xF802, 0x0A200003,
    0x0801, 0x0A300003, 0x00F8, 0x0A400003, 0xF804, 0x0A500003, 0xF8FF, 0x0A600003, 0xFFF8, 0x0A700003,
    0x04F8, 0x0A800003, 0x02F8, 0x0A900003, 0x1004, 0x0AA00003, 0x08F8, 0x0AB00003, 0xF808, 0x0AC00003,
    0x0410, 0x0AD00003, 0xFF08, 0x0AE00003, 0x08FF, 0x0AF00003, 0xFCF0, 0x0B000003, 0xF801, 0x0B100003,
    0xE0F0, 0x0B200003, 0xF3F3, 0x0B300003, 0xF0E0, 0x0B400003, 0xFAFA, 0x0B500003, 0xF7F7, 0x0B600003,
    0xFEF0, 0x0B700003, 0xF0FE, 0x0B800003, 0xE9E9, 0x0B900003, 0xF9F9, 0x0BA00003, 0x2020, 0x0BB00003,
    0xE0E0, 0x0BC00003, 0x02F0, 0x0BD00003, 0x04F0, 0x0BE00003, 0x2010, 0x0BF00003, 0xECEC, 0x0C000003,
    0xEFEF, 0x0C100003, 0x1020, 0x0C200003, 0xF5F5, 0x0C300003, 0xF4F4, 0x0C400003, 0xEDED, 0x0C500003,
    0xEAEA, 0x0C600003, 0xFBFB, 0x0C700003, 0x1002, 0x0C800003, 0xF2F2, 0x0C900003, 0xF6F6, 0x0CA00003,
    0xF1F1, 0x0CB00003, 0xFDFD, 0x0CC00003, 0x0210, 0x0CD00003, 0x10FF, 0x0CE00003, 0xFDFE, 0x0CF00003,
    0x10F8, 0x0D000003, 0x1000, 0x0D100003, 0xF001, 0x0D200003, 0x1001, 0x0D300003, 0x0010, 0x0D400003,
    0x10FE, 0x0D500003, 0xEBEB, 0x0D600003, 0xFE10, 0x0D700003, 0x0110, 0x0D800003, 0xF000, 0x0D900003,
    0x08F0, 0x0DA00003, 0x01F0, 0x0DB00003, 0x0303, 0x0DC00003, 0x00F0, 0x0DD00003, 0xF002, 0x0DE00003,
    0x10FC, 0x0DF00003, 0xFC10, 0x0E000003, 0xF0FF, 0x0E100003, 0xEEEE, 0x0E200003, 0xF004, 0x0E300003,
    0xFFF0, 0x0E400003, 0xF7F8, 0x0E500003, 0xF3F2, 0x0E600003, 0xF9FA, 0x0E700003, 0x0820, 0x0E800003,
    0x0302, 0x0E900003, 0xE0F8, 0x0EA00003, 0x0505, 0x0EB00003, 0x2008, 0x0EC00003, 0xE8E8, 0x0ED00003,
    0x0403, 0x0EE00003, 0xFBFC, 0x0EF00003, 0xFCFD, 0x0F000003, 0xFBFA, 0x0F100003, 0x0203, 0x0F200003,
    0xFCFB, 0x0F300003, 0x0304, 0x0F400003, 0xF810, 0x0F500003, 0xFF10, 0x0F600003, 0xF008, 0x0F700003,
    0xFEFD, 0x0F800003, 0xF7F6, 0x0F900003, 0xF2F1, 0x0FA00003, 0xF3F4, 0x0FB00003, 0xEDEC, 0x0FC00003,
    0xF4F1, 0x0FD00003, 0xF5F6, 0x0FE00003, 0xF0F1, 0x0FF00003, 0xF9F8, 0xC8000003, 0x10F0, 0xC8100003,
    0xF2F3, 0xC8200003, 0xF7F9, 0xC8300003, 0xF6F5, 0xC8400003, 0xF0EF, 0xC8500003, 0xF4F5, 0xC8600003,
    0xF6F7, 0xC8700003, 0xFAF9, 0xC8800003, 0x0405, 0xC8900003, 0xF8F9, 0xC8A00003, 0xFAFB, 0xC8B00003,
    0xF1F0, 0xC8C00003, 0xF4F3, 0xC8D00003, 0xF1F2, 0xC8E00003, 0xF8E0, 0xC8F00003, 0xF8F7, 0xC9000003,
    0xFDFC, 0xC9100003, 0xF8FA, 0xC9200003, 0xFAF6, 0xC9300003, 0xEEEF, 0xC9400003, 0xF5F7, 0xC9500003,
    0xFDFB, 0xC9600003, 0xF4F6, 0xC9700003, 0xFCFA, 0xC9800003, 0xECED, 0xC9900003, 0xF0F3, 0xC9A00003,
    0xF3F1, 0xC9B00003, 0xECEB, 0xC9C00003, 0xEDEE, 0xC9D00003, 0xF9F7, 0xC9E00003, 0x0420, 0xC9F00003,
    0xEBEA, 0xCA000003, 0xF0F4, 0xCA100003, 0xF3F5, 0xCA200003, 0xFAF7, 0xCA300003, 0x0301, 0xCA400003,
    0xF3F7, 0xCA500003, 0xF7F3, 0xCA600003, 0xEFF0, 0xCA700003, 0xF9F6, 0xCA800003, 0xEFEE, 0xCA900003,
    0xF4F7, 0xCAA00003, 0x0504, 0xCAB00003, 0xF5F4, 0xCAC00003, 0xF1F3, 0xCAD00003, 0xEBEE, 0xCAE00003,
    0xF2F5, 0xCAF00003, 0xF3EF, 0xCB000003, 0xF5F1, 0xCB100003, 0xF9F3, 0xCB200003, 0xEDF0, 0xCB300003,
    0xEEF1, 0xCB400003, 0xF6F9, 0xCB500003, 0xF8FB, 0xCB600003, 0xF010, 0xCB700003, 0xF2F6, 0xCB800003,
    0xF4ED, 0xCB900003, 0xF7FB, 0xCBA00003, 0xF8F3, 0xCBB00003, 0xEDEB, 0xCBC00003, 0xF0F2, 0xCBD00003,
    0xF2F9, 0xCBE00003, 0xF8F1, 0xCBF00003, 0xFAFC, 0xCC000003, 0xFBF8, 0xCC100003, 0xF6F0, 0xCC200003,
    0xFAF8, 0xCC300003, 0x0103, 0xCC400003, 0xF3F6, 0xCC500003, 0xF4F9, 0xCC600003, 0xF7F2, 0xCC700003,
    0x2004, 0xCC800003, 0xF2F0, 0xCC900003, 0xF4F2, 0xCCA00003, 0xEEED, 0xCCB00003, 0xFCE0, 0xCCC00003,
    0xEAE9, 0xCCD00003, 0xEAEB, 0xCCE00003, 0xF6F4, 0xCCF00003, 0xFFFD, 0xCD000003, 0xE9EA, 0xCD100003,
    0xF1F4, 0xCD200003, 0xF6EF, 0xCD300003, 0xF6F8, 0xCD400003, 0xF8F6, 0xCD500003, 0xEFF2, 0xCD600003,
    0xEFF1, 0xCD700003, 0xF7F1, 0xCD800003, 0xFBFD, 0xCD900003, 0xFEF6, 0xCDA00003, 0xFFF7, 0xCDB00003,
    0x0605, 0xCDC00003, 0xF0F5, 0xCDD00003, 0xF0FA, 0xCDE00003, 0xF1F9, 0xCDF00003, 0xF2FC, 0xCE000003,
    0xF7EE, 0xCE100003, 0xF7F5, 0xCE200003, 0xF9FC, 0xCE300003, 0xFAF5, 0xCE400003, 0xFBF1, 0xCE500003,
    0xF1EF, 0xCE600003, 0xF1FA, 0xCE700003, 0xF4F8, 0xCE800003, 0xF7F0, 0xCE900003, 0xF7F4, 0xCEA00003,
    0xF7FC, 0xCEB00003, 0xF9FB, 0xCEC00003, 0xFAF1, 0xCED00003, 0xFBF9, 0xCEE00003, 0xFDFF, 0xCEF00003,
    0xE0FC, 0xCF000003, 0xEBEC, 0xCF100003, 0xEDEF, 0xCF200003, 0xEFED, 0xCF300003, 0xF1F6, 0xCF400003,
    0xF2F7, 0xCF500003, 0xF3EE, 0xCF600003, 0xF3F8, 0xCF700003, 0xF5F2, 0xCF800003, 0xF8F2, 0xCF900003,
    0xF9F1, 0xCFA00003, 0xF9F2, 0xCFB00003, 0xFBEF, 0xCFC00003, 0x00FD, 0xCFD00003, 0xECEE, 0xCFE00003,
    0xF2EF, 0xCFF00003, 0xF2F8, 0xD8000003, 0xF5F0, 0xD8100003, 0xF6F2, 0xD8200003, 0xFCF7, 0xD8300003,
    0xFCF9, 0xD8400003, 0x0506, 0xD8500003, 0xEEEC, 0xD8600003, 0xF0F6, 0xD8700003, 0xF2F4, 0xD8800003,
    0xF6F1, 0xD8900003, 0xF8F5, 0xD8A00003, 0xF9F4, 0xD8B00003, 0xFBF7, 0xD8C00003, 0x0503, 0xD8D00003,
    0xEFEC, 0xD8E00003, 0xF3F0, 0xD8F00003, 0xF4F0, 0xD9000003, 0xF5F3, 0xD9100003, 0xF6F3, 0xD9200003,
    0xF7FA, 0xD9300003, 0x800A, 0xD9400003, 0x800B, 0xD9500003, 0x800C, 0xD9600003, 0x800D, 0xD9700003,
    0x800E, 0xD9800003, 0x800F, 0xD9900003, 0x8010, 0xD9A00003, 0x8011, 0xD9B00003, 0x8012, 0xD9C00003,
    0x8013, 0xD9D00003, 0x8014, 0xD9E00003, 0x8015, 0xD9F00003, 0x8016, 0xDA000003, 0x8017, 0xDA100003,
    0x8018, 0xDA200003, 0x8019, 0xDA300003, 0x801A, 0xDA400003, 0x801B, 0xDA500003, 0x801C, 0xDA600003,
    0x801D, 0xDA700003, 0x801E, 0xDA800003, 0x801F, 0xDA900003, 0x8020, 0xDAA00003, 0x8021, 0xDAB00003,
    0x8022, 0xDAC00003, 0x8023, 0xDAD00003, 0x8024, 0xDAE00003, 0x8025, 0xDAF00003, 0x8026, 0xDB000003,
    0x8027, 0xDB100003, 0x8028, 0xDB200003, 0x8029, 0xDB300003, 0x802A, 0xDB400003, 0x802B, 0xDB500003,
    0x802C, 0xDB600003, 0x802D, 0xDB700003, 0x802E, 0xDB800003, 0x802F, 0xDB900003, 0x80FF, 0xDBA00003,
    0x0001
};

static void fill_elements(uint32_t idx, uint32_t shift, int size, uint32_t *e0, uint32_t *e1)
{
    uint32_t a = 1, b, g = 1, h = idx << (32 - shift);

    for (int i = 0; i < size; i++) {
        if (!a || !g)
            break;
        b = 4 * (table[2 * i + 1] & 0xF);
        if (shift >= b && (h & (0xFFF00000u << (12 - b))) == (table[2 * i + 1] & 0xFFFF0000u)) {
            if (table[2 * i] >> 8 == 0x80u) {
                g = 0;
            } else {
                a = 0;
                *e1 = table[2 * i];
                *e0 = (*e0 & 0xFFFFFFu) | (((12 + b - shift) & 0xFFFFFFFCu | 0x40u) << 22);
                shift -= b;
                h <<= b;
            }
        }
    }
    a = 1;
    for (int i = 0; i < size; i++) {
        if (!a || !g)
            break;
        b = 4 * (table[2 * i + 1] & 0xF);
        if (shift >= b && (h & (0xFFF00000u << (12 - b))) == (table[2 * i + 1] & 0xFFFF0000u)) {
            if ((table[2 * i] >> 8) == 0x80u) {
                g = 0;
            } else {
                a = 0;
                *e1 |= table[2 * i] << 16;
                *e0 = (*e0 & 0xFFFFFFu) | (((12 + b - shift) & 0xFFFFFFFCu | 0x80u) << 22);
            }
        }
    }
}

static void fill_lut(uint32_t *lut)
{
    for (int i = 1; i < FF_ARRAY_ELEMS(table); i += 2) {
        uint32_t a = table[i];
        uint32_t b = a & 0xFFu;
        uint32_t c, d, e;

        if (b > 3)
            continue;

        c = (b << 16) | table[i-1];
        d = 4 * (3 - b);
        e = (((0xFFF00000u << d) & a) >> 20) & 0xFFF;
        if (d <= 0) {
            lut[2 * e] = c;
            lut[2 * e + 1] = 0;
        } else {
            for (int j = 0; j < 1 << d; j++) {
                uint32_t f = 0xFFFFFFFFu;
                c &= 0xFFFFFFu;
                if ((c & 0xFF00u) != 0x8000u)
                    fill_elements(j, d, 365, &c, &f);
                lut[2 * e + 2 * j] = c;
                lut[2 * e + 2 * j + 1] = f;
            }
        }
    }

    for (int i = 0; i < 32; i += 2) {
        lut[i  ] = 0x68000;
        lut[i+1] = 0;
    }
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    ProSumerContext *s = avctx->priv_data;

    s->stride = 3LL * FFALIGN(avctx->width, 8) >> 1;
    s->size = avctx->height * s->stride;

    avctx->pix_fmt = AV_PIX_FMT_YUV411P;

    s->table_b = av_malloc(s->stride);
    s->decbuffer = av_malloc(s->size);
    if (!s->table_b || !s->decbuffer)
        return AVERROR(ENOMEM);
    memset(s->table_b, 0x80u, s->stride);

    fill_lut(s->lut);

    return 0;
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    ProSumerContext *s = avctx->priv_data;

    av_freep(&s->table_b);
    av_freep(&s->decbuffer);

    return 0;
}

AVCodec ff_prosumer_decoder = {
    .name           = "prosumer",
    .long_name      = NULL_IF_CONFIG_SMALL("Brooktree ProSumer Video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_PROSUMER,
    .priv_data_size = sizeof(ProSumerContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .close          = decode_close,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
};
