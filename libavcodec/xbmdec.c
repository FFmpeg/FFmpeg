/*
 * XBM image format
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avstring.h"

#include "avcodec.h"
#include "internal.h"
#include "mathops.h"

static int xbm_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *avpkt)
{
    AVFrame *p = data;
    int ret, linesize, i;
    int width  = 0;
    int height = 0;
    const uint8_t *ptr = avpkt->data;
    uint8_t *dst;

    avctx->pix_fmt = AV_PIX_FMT_MONOWHITE;
    while (!width || !height) {
        ptr += strcspn(ptr, "#");
        if (ptr >= avpkt->data + avpkt->size) {
            av_log(avctx, AV_LOG_ERROR, "End of file reached.\n");
            return AVERROR_INVALIDDATA;
        }
        if (strncmp(ptr, "#define", 7) != 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "Unexpected preprocessor directive.\n");
            return AVERROR_INVALIDDATA;
        }
        // skip the name
        ptr += strcspn(ptr, "_") + 1;
        // get width or height
        if (strncmp(ptr, "width", 5) == 0) {
            ptr += strcspn(ptr, " ");
            width = strtol(ptr, NULL, 10);
        } else if (strncmp(ptr, "height", 6) == 0) {
            ptr += strcspn(ptr, " ");
            height = strtol(ptr, NULL, 10);
        } else {
            // skip offset and unknown variables
            av_log(avctx, AV_LOG_VERBOSE,
                   "Ignoring preprocessor directive.\n");
        }
    }

    if ((ret = ff_set_dimensions(avctx, width, height)) < 0)
        return ret;

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;

    // go to start of image data
    ptr += strcspn(ptr, "{");

    linesize = (avctx->width + 7) / 8;
    for (i = 0; i < avctx->height; i++) {
        int eol = 0, e = 0;
        dst = p->data[0] + i * p->linesize[0];
        if (ptr >= avpkt->data + avpkt->size) {
            av_log(avctx, AV_LOG_ERROR, "End of file reached.\n");
            return AVERROR_INVALIDDATA;
        }
        do {
            int val;
            uint8_t *endptr;

            ptr += strcspn(ptr, "x") - 1; // -1 to get 0x
            val = strtol(ptr, (char **)&endptr, 16);

            if (endptr - ptr == 4) {
                // XBM X11 format
                *dst++ = ff_reverse[val];
                eol = linesize;
            } else if (endptr - ptr == 6) {
                // XBM X10 format
                *dst++ = ff_reverse[val >> 8];
                *dst++ = ff_reverse[val & 0xFF];
                eol = linesize / 2; // 2 bytes read
            } else {
                av_log(avctx, AV_LOG_ERROR,
                       "Unexpected data at %.8s.\n", ptr);
                return AVERROR_INVALIDDATA;
            }
            ptr = endptr;
        } while (++e < eol);
    }

    p->key_frame = 1;
    p->pict_type = AV_PICTURE_TYPE_I;

    *got_frame = 1;

    return avpkt->size;
}

AVCodec ff_xbm_decoder = {
    .name         = "xbm",
    .long_name    = NULL_IF_CONFIG_SMALL("XBM (X BitMap) image"),
    .type         = AVMEDIA_TYPE_VIDEO,
    .id           = AV_CODEC_ID_XBM,
    .decode       = xbm_decode_frame,
    .capabilities = AV_CODEC_CAP_DR1,
};
