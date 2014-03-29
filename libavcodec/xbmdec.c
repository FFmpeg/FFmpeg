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

#include "libavutil/avstring.h"

#include "avcodec.h"
#include "internal.h"
#include "mathops.h"

static int convert(uint8_t x)
{
    if (x >= 'a')
        x -= 87;
    else if (x >= 'A')
        x -= 55;
    else
        x -= '0';
    return x;
}

static int parse_str_int(const uint8_t *p, int len, const uint8_t *key)
{
    const uint8_t *end = p + len;

    for(; p<end - strlen(key); p++) {
        if (!memcmp(p, key, strlen(key)))
            break;
    }
    p += strlen(key);
    if (p >= end)
        return INT_MIN;

    for(; p<end; p++) {
        char **eptr;
        int64_t ret = strtol(p, &eptr, 10);
        if (eptr != p)
            return ret;
    }
    return INT_MIN;
}

static int xbm_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *avpkt)
{
    AVFrame *p = data;
    int ret, linesize, i, j;
    int width  = 0;
    int height = 0;
    const uint8_t *end, *ptr = avpkt->data;
    const uint8_t *next;
    uint8_t *dst;

    avctx->pix_fmt = AV_PIX_FMT_MONOWHITE;
    end = avpkt->data + avpkt->size;

    width  = parse_str_int(avpkt->data, avpkt->size, "_width");
    height = parse_str_int(avpkt->data, avpkt->size, "_height");

    if ((ret = ff_set_dimensions(avctx, width, height)) < 0)
        return ret;

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;

    // goto start of image data
    next = ptr + strcspn(ptr, "{");
    if (!*next)
        next = ptr + strcspn(ptr, "(");
    if (!*next)
        return AVERROR_INVALIDDATA;
    ptr = next + 1;

    linesize = (avctx->width + 7) / 8;
    for (i = 0; i < avctx->height; i++) {
        dst = p->data[0] + i * p->linesize[0];
        for (j = 0; j < linesize; j++) {
            uint8_t val;

            ptr += strcspn(ptr, "x$") + 1;
            if (ptr < end && av_isxdigit(*ptr)) {
                val = convert(*ptr++);
                if (av_isxdigit(*ptr))
                    val = (val << 4) + convert(*ptr++);
                *dst++ = ff_reverse[val];
                if (av_isxdigit(*ptr) && j+1 < linesize) {
                    j++;
                    val = convert(*ptr++);
                    if (av_isxdigit(*ptr))
                        val = (val << 4) + convert(*ptr++);
                    *dst++ = ff_reverse[val];
                }
            } else {
                av_log(avctx, AV_LOG_ERROR,
                       "Unexpected data at %.8s.\n", ptr);
                return AVERROR_INVALIDDATA;
            }
        }
    }

    p->key_frame = 1;
    p->pict_type = AV_PICTURE_TYPE_I;

    *got_frame       = 1;

    return avpkt->size;
}

AVCodec ff_xbm_decoder = {
    .name         = "xbm",
    .long_name    = NULL_IF_CONFIG_SMALL("XBM (X BitMap) image"),
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = AV_CODEC_ID_XBM,
    .decode       = xbm_decode_frame,
    .capabilities = CODEC_CAP_DR1,
};
