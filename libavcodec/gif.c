/*
 * GIF encoder.
 * Copyright (c) 2000 Fabrice Bellard
 * Copyright (c) 2002 Francois Revol
 * Copyright (c) 2006 Baptiste Coudurier
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

/*
 * First version by Francois Revol revol@free.fr
 *
 * Features and limitations:
 * - currently no compression is performed,
 *   in fact the size of the data is 9/8 the size of the image in 8bpp
 * - uses only a global standard palette
 * - tested with IE 5.0, Opera for BeOS, NetPositive (BeOS), and Mozilla (BeOS).
 *
 * Reference documents:
 * http://www.goice.co.jp/member/mo/formats/gif.html
 * http://astronomy.swin.edu.au/pbourke/dataformats/gif/
 * http://www.dcs.ed.ac.uk/home/mxr/gfx/2d/GIF89a.txt
 *
 * this url claims to have an LZW algorithm not covered by Unisys patent:
 * http://www.msg.net/utility/whirlgif/gifencod.html
 * could help reduce the size of the files _a lot_...
 * some sites mentions an RLE type compression also.
 */

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "lzw.h"

/* The GIF format uses reversed order for bitstreams... */
/* at least they don't use PDP_ENDIAN :) */
#define BITSTREAM_WRITER_LE

#include "put_bits.h"

typedef struct {
    AVFrame picture;
    LZWState *lzw;
    uint8_t *buf;
} GIFContext;

/* GIF header */
static int gif_image_write_header(AVCodecContext *avctx,
                                  uint8_t **bytestream, uint32_t *palette)
{
    int i;
    unsigned int v, smallest_alpha = 0xFF, alpha_component = 0;

    bytestream_put_buffer(bytestream, "GIF", 3);
    bytestream_put_buffer(bytestream, "89a", 3);
    bytestream_put_le16(bytestream, avctx->width);
    bytestream_put_le16(bytestream, avctx->height);

    bytestream_put_byte(bytestream, 0xf7); /* flags: global clut, 256 entries */
    bytestream_put_byte(bytestream, 0x1f); /* background color index */
    bytestream_put_byte(bytestream, 0); /* aspect ratio */

    /* the global palette */
    for(i=0;i<256;i++) {
        v = palette[i];
        bytestream_put_be24(bytestream, v);
        if (v >> 24 < smallest_alpha) {
            smallest_alpha = v >> 24;
            alpha_component = i;
        }
    }

    if (smallest_alpha < 128) {
        bytestream_put_byte(bytestream, 0x21); /* Extension Introducer */
        bytestream_put_byte(bytestream, 0xf9); /* Graphic Control Label */
        bytestream_put_byte(bytestream, 0x04); /* block length */
        bytestream_put_byte(bytestream, 0x01); /* Transparent Color Flag */
        bytestream_put_le16(bytestream, 0x00); /* no delay */
        bytestream_put_byte(bytestream, alpha_component);
        bytestream_put_byte(bytestream, 0x00);
    }

    return 0;
}

static int gif_image_write_image(AVCodecContext *avctx,
                                 uint8_t **bytestream, uint8_t *end,
                                 const uint8_t *buf, int linesize)
{
    GIFContext *s = avctx->priv_data;
    int len = 0, height;
    const uint8_t *ptr;
    /* image block */

    bytestream_put_byte(bytestream, 0x2c);
    bytestream_put_le16(bytestream, 0);
    bytestream_put_le16(bytestream, 0);
    bytestream_put_le16(bytestream, avctx->width);
    bytestream_put_le16(bytestream, avctx->height);
    bytestream_put_byte(bytestream, 0x00); /* flags */
    /* no local clut */

    bytestream_put_byte(bytestream, 0x08);

    ff_lzw_encode_init(s->lzw, s->buf, avctx->width*avctx->height,
                       12, FF_LZW_GIF, put_bits);

    ptr = buf;
    for (height = avctx->height; height--;) {
        len += ff_lzw_encode(s->lzw, ptr, avctx->width);
        ptr += linesize;
    }
    len += ff_lzw_encode_flush(s->lzw, flush_put_bits);

    ptr = s->buf;
    while (len > 0) {
        int size = FFMIN(255, len);
        bytestream_put_byte(bytestream, size);
        if (end - *bytestream < size)
            return -1;
        bytestream_put_buffer(bytestream, ptr, size);
        ptr += size;
        len -= size;
    }
    bytestream_put_byte(bytestream, 0x00); /* end of image block */
    bytestream_put_byte(bytestream, 0x3b);
    return 0;
}

static av_cold int gif_encode_init(AVCodecContext *avctx)
{
    GIFContext *s = avctx->priv_data;

    avctx->coded_frame = &s->picture;
    s->lzw = av_mallocz(ff_lzw_encode_state_size);
    if (!s->lzw)
        return AVERROR(ENOMEM);
    s->buf = av_malloc(avctx->width*avctx->height*2);
    if (!s->buf)
         return AVERROR(ENOMEM);
    return 0;
}

/* better than nothing gif encoder */
static int gif_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *pict, int *got_packet)
{
    GIFContext *s = avctx->priv_data;
    AVFrame *const p = &s->picture;
    uint8_t *outbuf_ptr, *end;
    int ret;

    if ((ret = ff_alloc_packet2(avctx, pkt, avctx->width*avctx->height*7/5 + FF_MIN_BUFFER_SIZE)) < 0)
        return ret;
    outbuf_ptr = pkt->data;
    end        = pkt->data + pkt->size;

    *p = *pict;
    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;
    gif_image_write_header(avctx, &outbuf_ptr, (uint32_t *)pict->data[1]);
    gif_image_write_image(avctx, &outbuf_ptr, end, pict->data[0], pict->linesize[0]);

    pkt->size   = outbuf_ptr - pkt->data;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

static int gif_encode_close(AVCodecContext *avctx)
{
    GIFContext *s = avctx->priv_data;

    av_freep(&s->lzw);
    av_freep(&s->buf);
    return 0;
}

AVCodec ff_gif_encoder = {
    .name           = "gif",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_GIF,
    .priv_data_size = sizeof(GIFContext),
    .init           = gif_encode_init,
    .encode2        = gif_encode_frame,
    .close          = gif_encode_close,
    .pix_fmts       = (const enum PixelFormat[]){
        PIX_FMT_RGB8, PIX_FMT_BGR8, PIX_FMT_RGB4_BYTE, PIX_FMT_BGR4_BYTE,
        PIX_FMT_GRAY8, PIX_FMT_PAL8, PIX_FMT_NONE
    },
    .long_name      = NULL_IF_CONFIG_SMALL("GIF (Graphics Interchange Format)"),
};
