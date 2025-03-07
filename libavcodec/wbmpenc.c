/*
 * WBMP (Wireless Application Protocol Bitmap) image
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
#include "encode.h"

static void putv(uint8_t ** bufp, unsigned int v)
{
    unsigned int vv = 0;
    int n = 0;

    while (vv != v)
        vv += v & (0x7F << 7 * n++);

    while (--n > 0)
        bytestream_put_byte(bufp, 0x80 | (v & (0x7F << 7 * n)) >> 7 * n);

    bytestream_put_byte(bufp, v & 0x7F);
}

static void writebits(uint8_t ** bufp, const uint8_t * src, int width, int height, int linesize)
{
    int wpad = (width + 7) / 8;
    for (int j = 0; j < height; j++) {
        memcpy(*bufp, src, wpad);
        *bufp += wpad;
        src += linesize;
    }
}

static int wbmp_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *frame, int *got_packet)
{
    int64_t size = avctx->height * ((avctx->width + 7) / 8) + 32;
    uint8_t *buf;
    int ret;

    if ((ret = ff_get_encode_buffer(avctx, pkt, size, 0)) < 0)
        return ret;

    buf = pkt->data;

    putv(&buf, 0);
    bytestream_put_byte(&buf, 0);
    putv(&buf, avctx->width);
    putv(&buf, avctx->height);

    if (frame->linesize[0] == (avctx->width + 7) / 8)
        bytestream_put_buffer(&buf, frame->data[0], avctx->height * ((avctx->width + 7) / 8));
    else
        writebits(&buf, frame->data[0], avctx->width, avctx->height, frame->linesize[0]);

    av_shrink_packet(pkt, buf - pkt->data);

    *got_packet = 1;
    return 0;
}

const FFCodec ff_wbmp_encoder = {
    .p.name         = "wbmp",
    CODEC_LONG_NAME("WBMP (Wireless Application Protocol Bitmap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_WBMP,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    FF_CODEC_ENCODE_CB(wbmp_encode_frame),
    CODEC_PIXFMTS(AV_PIX_FMT_MONOBLACK),
};
