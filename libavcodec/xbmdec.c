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

static int xbm_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *avpkt)
{
    AVFrame *p = data;
    int ret, linesize, i, j;
    int width  = 0;
    int height = 0;
    const uint8_t *end, *ptr = avpkt->data;
    uint8_t *dst;

    avctx->pix_fmt = AV_PIX_FMT_MONOWHITE;
    end = avpkt->data + avpkt->size;
    while (!width || !height) {
        char name[256];
        int number, len;

        ptr += strcspn(ptr, "#");
        if (ptr >= avpkt->data + avpkt->size) {
            av_log(avctx, AV_LOG_ERROR, "End of file reached.\n");
            return AVERROR_INVALIDDATA;
        }
        if (sscanf(ptr, "#define %255s %u", name, &number) != 2) {
            av_log(avctx, AV_LOG_ERROR, "Unexpected preprocessor directive\n");
            return AVERROR_INVALIDDATA;
        }

        len = strlen(name);
        if ((len > 6) && !height && !memcmp(name + len - 7, "_height", 7)) {
            height = number;
        } else if ((len > 5) && !width && !memcmp(name + len - 6, "_width", 6)) {
            width = number;
        } else {
            av_log(avctx, AV_LOG_WARNING, "Unknown define '%s'\n", name);
        }
        ptr += strcspn(ptr, "\n\r") + 1;
    }

    if ((ret = ff_set_dimensions(avctx, width, height)) < 0)
        return ret;

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;

    // goto start of image data
    ptr += strcspn(ptr, "{") + 1;

    linesize = (avctx->width + 7) / 8;
    for (i = 0; i < avctx->height; i++) {
        dst = p->data[0] + i * p->linesize[0];
        for (j = 0; j < linesize; j++) {
            uint8_t val;

            ptr += strcspn(ptr, "x") + 1;
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
