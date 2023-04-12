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
#include "decode.h"
#include "thread.h"

static unsigned int getv(GetByteContext * gb)
{
    int i;
    unsigned int v = 0;

    do {
        i = bytestream2_get_byte(gb);
        v = (v << 7) | (i & 0x7F);
    } while (i & 0x80);
    return v;
}

static void readbits(uint8_t * dst, int width, int height, int linesize, const uint8_t * src, int size)
{
    int wpad = (width + 7) / 8;
    for (int j = 0; j < height && size > 0; j++) {
        memcpy(dst, src, FFMIN(wpad, size));
        src += wpad;
        size -= wpad;
        dst += linesize;
    }
}

static int wbmp_decode_frame(AVCodecContext *avctx, AVFrame *p,
                            int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size, width, height, ret;
    GetByteContext gb;

    bytestream2_init(&gb, buf, buf_size);

    if (getv(&gb))
        return AVERROR_INVALIDDATA;
    bytestream2_skip(&gb, 1);
    width = getv(&gb);
    height = getv(&gb);

    if ((ret = ff_set_dimensions(avctx, width, height)) < 0)
        return ret;

    avctx->pix_fmt = AV_PIX_FMT_MONOBLACK;
    if ((ret = ff_thread_get_buffer(avctx, p, 0)) < 0)
        return ret;

    if (p->linesize[0] == (width + 7) / 8)
        bytestream2_get_buffer(&gb, p->data[0], height * ((width + 7) / 8));
    else
        readbits(p->data[0], width, height, p->linesize[0], gb.buffer, gb.buffer_end - gb.buffer);

    p->flags |= AV_FRAME_FLAG_KEY;
    p->pict_type = AV_PICTURE_TYPE_I;

    *got_frame   = 1;

    return buf_size;
}

const FFCodec ff_wbmp_decoder = {
    .p.name         = "wbmp",
    CODEC_LONG_NAME("WBMP (Wireless Application Protocol Bitmap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_WBMP,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    FF_CODEC_DECODE_CB(wbmp_decode_frame),
};
