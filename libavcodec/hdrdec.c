/*
 * Radiance HDR image format
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
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "thread.h"

#define MINELEN 8
#define MAXELEN 0x7fff

static int hdr_get_line(GetByteContext *gb, uint8_t *buffer, int size)
{
    int n = 0, c;

    memset(buffer, 0, size);

    do {
        c = bytestream2_get_byte(gb);
        if (n < size - 1)
            buffer[n++] = c;
    } while (bytestream2_get_bytes_left(gb) > 0 && c != '\n');

    return 0;
}

static float convert(int expo, int val)
{
    if (expo == -128) {
        return 0.f;
    } else {
        const float v = val / 256.f;

        return ldexpf(v, expo);
    }
}

static int decompress(uint8_t *scanline, int w, GetByteContext *gb, const uint8_t *start)
{
    int rshift = 0;

    while (w > 0) {
        if (bytestream2_get_bytes_left(gb) < 4)
            return AVERROR_INVALIDDATA;
        scanline[0] = bytestream2_get_byte(gb);
        scanline[1] = bytestream2_get_byte(gb);
        scanline[2] = bytestream2_get_byte(gb);
        scanline[3] = bytestream2_get_byte(gb);

        if (scanline[0] == 1 &&
            scanline[1] == 1 &&
            scanline[2] == 1) {
            int run = scanline[3];
            for (int i = run << rshift; i > 0 && w > 0 && scanline >= start + 4; i--) {
                memcpy(scanline, scanline - 4, 4);
                scanline += 4;
                w -= 4;
            }
            rshift += 8;
            if (rshift > 16)
                break;
        } else {
            scanline += 4;
            w--;
            rshift = 0;
        }
    }

    return 1;
}

static int hdr_decode_frame(AVCodecContext *avctx, AVFrame *p,
                            int *got_frame, AVPacket *avpkt)
{
    int width = 0, height = 0;
    GetByteContext gb;
    uint8_t line[512];
    float sar;
    int ret;

    bytestream2_init(&gb, avpkt->data, avpkt->size);
    hdr_get_line(&gb, line, sizeof(line));
    if (memcmp("#?RADIANCE\n", line, 11))
        return AVERROR_INVALIDDATA;

    do {
        hdr_get_line(&gb, line, sizeof(line));
        if (sscanf(line, "PIXASPECT=%f\n", &sar) == 1)
            avctx->sample_aspect_ratio = p->sample_aspect_ratio = av_inv_q(av_d2q(sar, 4096));
    } while (line[0] != '\n' && line[0]);

    hdr_get_line(&gb, line, sizeof(line));
    if (sscanf(line, "-Y %d +X %d\n", &height, &width) == 2) {
        ;
    } else if (sscanf(line, "+Y %d +X %d\n", &height, &width) == 2) {
        ;
    } else if (sscanf(line, "-Y %d -X %d\n", &height, &width) == 2) {
        ;
    } else if (sscanf(line, "+Y %d -X %d\n", &height, &width) == 2) {
        ;
    } else if (sscanf(line, "-X %d +Y %d\n", &width, &height) == 2) {
        ;
    } else if (sscanf(line, "+X %d +Y %d\n", &width, &height) == 2) {
        ;
    } else if (sscanf(line, "-X %d -Y %d\n", &width, &height) == 2) {
        ;
    } else if (sscanf(line, "+X %d -Y %d\n", &width, &height) == 2) {
        ;
    }

    if ((ret = ff_set_dimensions(avctx, width, height)) < 0)
        return ret;

    avctx->pix_fmt = AV_PIX_FMT_GBRPF32;

    if (avctx->skip_frame >= AVDISCARD_ALL)
        return avpkt->size;

    if ((ret = ff_thread_get_buffer(avctx, p, 0)) < 0)
        return ret;

    for (int y = 0; y < height; y++) {
        float *dst_r = (float *)(p->data[2] + y * p->linesize[2]);
        float *dst_g = (float *)(p->data[0] + y * p->linesize[0]);
        float *dst_b = (float *)(p->data[1] + y * p->linesize[1]);
        uint8_t *scanline = p->data[0] + y * p->linesize[0];
        int i;

        if (width < MINELEN || width > MAXELEN) {
            ret = decompress(scanline, width, &gb, scanline);
            if (ret < 0)
                return ret;
            goto convert;
        }

        i = bytestream2_peek_byte(&gb);
        if (i != 2) {
            ret = decompress(scanline, width, &gb, scanline);
            if (ret < 0)
                return ret;
            goto convert;
        }
        bytestream2_skip(&gb, 1);

        scanline[1] = bytestream2_get_byte(&gb);
        scanline[2] = bytestream2_get_byte(&gb);
        i = bytestream2_get_byte(&gb);

        if (scanline[1] != 2 || scanline[2] & 128) {
            scanline[0] = 2;
            scanline[3] = i;
            ret = decompress(scanline + 4, width - 1, &gb, scanline);
            if (ret < 0)
                return ret;
            goto convert;
        }

        for (int i = 0; i < 4; i++) {
            uint8_t *scanline = p->data[0] + y * p->linesize[0] + i;

            for (int j = 0; j < width * 4 && bytestream2_get_bytes_left(&gb) > 0;) {
                int run = bytestream2_get_byte(&gb);
                if (run > 128) {
                    uint8_t val = bytestream2_get_byte(&gb);
                    run &= 127;
                    while (run--) {
                        if (j >= width * 4)
                            break;
                        scanline[j] = val;
                        j += 4;
                    }
                } else if (run > 0) {
                    while (run--) {
                        if (j >= width * 4)
                            break;
                        scanline[j] = bytestream2_get_byte(&gb);
                        j += 4;
                    }
                }
            }
        }

convert:
        for (int x = 0; x < width; x++) {
            uint8_t rgbe[4];
            int expo;

            memcpy(rgbe, p->data[0] + y * p->linesize[0] + x * 4, 4);
            expo = rgbe[3] - 128;

            dst_r[x] = convert(expo, rgbe[0]);
            dst_b[x] = convert(expo, rgbe[2]);
            dst_g[x] = convert(expo, rgbe[1]);
        }
    }

    p->flags |= AV_FRAME_FLAG_KEY;
    p->pict_type = AV_PICTURE_TYPE_I;

    *got_frame   = 1;

    return avpkt->size;
}

const FFCodec ff_hdr_decoder = {
    .p.name         = "hdr",
    CODEC_LONG_NAME("HDR (Radiance RGBE format) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_RADIANCE_HDR,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    .caps_internal  = FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    FF_CODEC_DECODE_CB(hdr_decode_frame),
};
