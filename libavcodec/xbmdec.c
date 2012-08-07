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

#include "avcodec.h"
#include "internal.h"

static av_cold int xbm_decode_init(AVCodecContext *avctx)
{
    avctx->coded_frame = avcodec_alloc_frame();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);

    return 0;
}

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
                            int *data_size, AVPacket *avpkt)
{
    AVFrame *p = avctx->coded_frame;
    const uint8_t *end, *ptr = avpkt->data;
    uint8_t *dst;
    int ret, linesize, i, j;

    end = avpkt->data + avpkt->size;
    while (!avctx->width || !avctx->height) {
        char name[256];
        int number, len;

        ptr += strcspn(ptr, "#");
        if (sscanf(ptr, "#define %256s %u", name, &number) != 2) {
            av_log(avctx, AV_LOG_ERROR, "Unexpected preprocessor directive\n");
            return AVERROR_INVALIDDATA;
        }

        len = strlen(name);
        if ((len > 6) && !avctx->height && !memcmp(name + len - 7, "_height", 7)) {
                avctx->height = number;
        } else if ((len > 5) && !avctx->width && !memcmp(name + len - 6, "_width", 6)) {
                avctx->width = number;
        } else {
            av_log(avctx, AV_LOG_ERROR, "Unknown define '%s'\n", name);
            return AVERROR_INVALIDDATA;
        }
        ptr += strcspn(ptr, "\n\r") + 1;
    }

    avctx->pix_fmt = PIX_FMT_MONOWHITE;

    if (p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference = 0;
    if ((ret = avctx->get_buffer(avctx, p)) < 0)
        return ret;

    // goto start of image data
    ptr += strcspn(ptr, "{") + 1;

    linesize = (avctx->width + 7) / 8;
    for (i = 0; i < avctx->height; i++) {
        dst = p->data[0] + i * p->linesize[0];
        for (j = 0; j < linesize; j++) {
            uint8_t val;

            ptr += strcspn(ptr, "x") + 1;
            if (ptr < end && isxdigit(*ptr)) {
                val = convert(*ptr);
                ptr++;
                if (isxdigit(*ptr))
                    val = (val << 4) + convert(*ptr);
                *dst++ = av_reverse[val];
            } else {
                av_log(avctx, AV_LOG_ERROR, "Unexpected data at '%.8s'\n", ptr);
                return AVERROR_INVALIDDATA;
            }
        }
    }

    p->key_frame = 1;
    p->pict_type = AV_PICTURE_TYPE_I;

    *data_size       = sizeof(AVFrame);
    *(AVFrame *)data = *p;

    return avpkt->size;
}

static av_cold int xbm_decode_close(AVCodecContext *avctx)
{
    if (avctx->coded_frame->data[0])
        avctx->release_buffer(avctx, avctx->coded_frame);

    av_freep(&avctx->coded_frame);

    return 0;
}

AVCodec ff_xbm_decoder = {
    .name         = "xbm",
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = AV_CODEC_ID_XBM,
    .init         = xbm_decode_init,
    .close        = xbm_decode_close,
    .decode       = xbm_decode_frame,
    .capabilities = CODEC_CAP_DR1,
    .long_name    = NULL_IF_CONFIG_SMALL("XBM (X BitMap) image"),
};
