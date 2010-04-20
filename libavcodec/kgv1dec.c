/*
 * Kega Game Video (KGV1) decoder
 * Copyright (c) 2010 Daniel Verkamp
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

/**
 * @file
 * Kega Game Video decoder
 */

#include "libavutil/intreadwrite.h"
#include "avcodec.h"

typedef struct {
    AVCodecContext *avctx;
    AVFrame pic;
    uint16_t *prev, *cur;
} KgvContext;

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    const uint8_t *buf_end = buf + avpkt->size;
    KgvContext * const c = avctx->priv_data;
    int offsets[7];
    uint16_t *out, *prev;
    int outcnt = 0, maxcnt;
    int w, h, i;

    if (avpkt->size < 2)
        return -1;

    w = (buf[0] + 1) * 8;
    h = (buf[1] + 1) * 8;
    buf += 2;

    if (avcodec_check_dimensions(avctx, w, h))
        return -1;

    if (w != avctx->width || h != avctx->height)
        avcodec_set_dimensions(avctx, w, h);

    maxcnt = w * h;

    out = av_realloc(c->cur, w * h * 2);
    if (!out)
        return -1;
    c->cur = out;

    prev = av_realloc(c->prev, w * h * 2);
    if (!prev)
        return -1;
    c->prev = prev;

    for (i = 0; i < 7; i++)
        offsets[i] = -1;

    while (outcnt < maxcnt && buf_end - 2 > buf) {
        int code = AV_RL16(buf);
        buf += 2;

        if (!(code & 0x8000)) {
            out[outcnt++] = code; // rgb555 pixel coded directly
        } else {
            int count;
            uint16_t *inp;

            if ((code & 0x6000) == 0x6000) {
                // copy from previous frame
                int oidx = (code >> 10) & 7;
                int start;

                count = (code & 0x3FF) + 3;

                if (offsets[oidx] < 0) {
                    if (buf_end - 3 < buf)
                        break;
                    offsets[oidx] = AV_RL24(buf);
                    buf += 3;
                }

                start = (outcnt + offsets[oidx]) % maxcnt;

                if (maxcnt - start < count)
                    break;

                inp = prev + start;
            } else {
                // copy from earlier in this frame
                int offset = (code & 0x1FFF) + 1;

                if (!(code & 0x6000)) {
                    count = 2;
                } else if ((code & 0x6000) == 0x2000) {
                    count = 3;
                } else {
                    if (buf_end - 1 < buf)
                        break;
                    count = 4 + *buf++;
                }

                if (outcnt < offset)
                    break;

                inp = out + outcnt - offset;
            }

            if (maxcnt - outcnt < count)
                break;

            for (i = 0; i < count; i++)
                out[outcnt++] = inp[i];
        }
    }

    if (outcnt - maxcnt)
        av_log(avctx, AV_LOG_DEBUG, "frame finished with %d diff\n", outcnt - maxcnt);

    c->pic.data[0]     = (uint8_t *)c->cur;
    c->pic.linesize[0] = w * 2;

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = c->pic;

    FFSWAP(uint16_t *, c->cur, c->prev);

    return avpkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    KgvContext * const c = avctx->priv_data;

    c->avctx = avctx;
    avctx->pix_fmt = PIX_FMT_RGB555;

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    KgvContext * const c = avctx->priv_data;

    av_freep(&c->cur);
    av_freep(&c->prev);

    return 0;
}

AVCodec kgv1_decoder = {
    "kgv1",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_KGV1,
    sizeof(KgvContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("Kega Game Video"),
};
