/*
 * XBM image format
 *
 * Copyright (c) 2012 Paul B Mahol
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

#include "libavutil/reverse.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"

static int get_nibble(uint8_t x)
{
#define TIMES256(idx) \
TIMES64(4 * (idx)) TIMES64(4 * (idx) + 1) TIMES64(4 * (idx) + 2) TIMES64(4 * (idx) + 3)
#define TIMES64(idx) \
TIMES16(4 * (idx)) TIMES16(4 * (idx) + 1) TIMES16(4 * (idx) + 2) TIMES16(4 * (idx) + 3)
#define TIMES16(idx) \
TIMES4(4 * (idx)) TIMES4(4 * (idx) + 1) TIMES4(4 * (idx) + 2) TIMES4(4 * (idx) + 3)
#define TIMES4(idx) \
ENTRY(4 * (idx)) ENTRY(4 * (idx) + 1) ENTRY(4 * (idx) + 2) ENTRY(4 * (idx) + 3)
#define ENTRY(x) [x] = ((x) >= 'a' && (x) <= 'f') ? (x) - ('a' - 10) : \
                       ((x) >= 'A' && (x) <= 'F') ? (x) - ('A' - 10) : \
                       ((x) >= '0' && (x) <= '9') ? (x) - '0' : 255,

    static const uint8_t lut[] = {
        TIMES256(0)
    };
    return lut[x];
}

static int parse_str_int(const uint8_t *p, const uint8_t *end, const uint8_t *key)
{
    int keylen = strlen(key);
    const uint8_t *e = end - keylen;

    for(; p < e; p++) {
        if (!memcmp(p, key, keylen))
            break;
    }
    p += keylen;
    if (p >= end)
        return INT_MIN;

    for(; p<end; p++) {
        char *eptr;
        int64_t ret = strtol(p, &eptr, 10);
        if ((const uint8_t *)eptr != p)
            return ret;
    }
    return INT_MIN;
}

static int xbm_decode_frame(AVCodecContext *avctx, AVFrame *p,
                            int *got_frame, AVPacket *avpkt)
{
    int ret, linesize, i, j;
    int width  = 0;
    int height = 0;
    const uint8_t *end, *ptr = avpkt->data;
    const uint8_t *next;
    uint8_t *dst;

    avctx->pix_fmt = AV_PIX_FMT_MONOWHITE;
    end = avpkt->data + avpkt->size;

    width  = parse_str_int(avpkt->data, end, "_width");
    height = parse_str_int(avpkt->data, end, "_height");

    if ((ret = ff_set_dimensions(avctx, width, height)) < 0)
        return ret;

    if (avctx->skip_frame >= AVDISCARD_ALL)
        return avpkt->size;

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;

    // goto start of image data
    next = memchr(ptr, '{', avpkt->size);
    if (!next)
        next = memchr(ptr, '(', avpkt->size);
    if (!next)
        return AVERROR_INVALIDDATA;
    ptr = next + 1;

    linesize = (avctx->width + 7) / 8;
    for (i = 0; i < avctx->height; i++) {
        dst = p->data[0] + i * p->linesize[0];
        for (j = 0; j < linesize; j++) {
            uint8_t nib, val;

            while (ptr < end && *ptr != 'x' && *ptr != '$')
                ptr++;

            ptr ++;
            if (ptr < end && (val = get_nibble(*ptr)) <= 15) {
                ptr++;
                if ((nib = get_nibble(*ptr)) <= 15) {
                    val = (val << 4) + nib;
                    ptr++;
                }
                *dst++ = ff_reverse[val];
                if ((val = get_nibble(*ptr)) <= 15 && j+1 < linesize) {
                    j++;
                    ptr++;
                    if ((nib = get_nibble(*ptr)) <= 15) {
                        val = (val << 4) + nib;
                        ptr++;
                    }
                    *dst++ = ff_reverse[val];
                }
            } else {
                av_log(avctx, AV_LOG_ERROR,
                       "Unexpected data at %.8s.\n", ptr);
                return AVERROR_INVALIDDATA;
            }
        }
    }

    *got_frame       = 1;

    return avpkt->size;
}

const FFCodec ff_xbm_decoder = {
    .p.name       = "xbm",
    CODEC_LONG_NAME("XBM (X BitMap) image"),
    .p.type       = AVMEDIA_TYPE_VIDEO,
    .p.id         = AV_CODEC_ID_XBM,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    FF_CODEC_DECODE_CB(xbm_decode_frame),
};
