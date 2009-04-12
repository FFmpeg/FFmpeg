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

/* The GIF format uses reversed order for bitstreams... */
/* at least they don't use PDP_ENDIAN :) */
#define BITSTREAM_WRITER_LE

#include "put_bits.h"

/* bitstream minipacket size */
#define GIF_CHUNKS 100

/* GIF header */
static int gif_image_write_header(uint8_t **bytestream,
                                  int width, int height,
                                  uint32_t *palette)
{
    int i;
    unsigned int v;

    bytestream_put_buffer(bytestream, "GIF", 3);
    bytestream_put_buffer(bytestream, "89a", 3);
    bytestream_put_le16(bytestream, width);
    bytestream_put_le16(bytestream, height);

    bytestream_put_byte(bytestream, 0xf7); /* flags: global clut, 256 entries */
    bytestream_put_byte(bytestream, 0x1f); /* background color index */
    bytestream_put_byte(bytestream, 0); /* aspect ratio */

    /* the global palette */
    for(i=0;i<256;i++) {
        v = palette[i];
        bytestream_put_be24(bytestream, v);
    }

    return 0;
}

static int gif_image_write_image(uint8_t **bytestream,
                                 int x1, int y1, int width, int height,
                                 const uint8_t *buf, int linesize, int pix_fmt)
{
    PutBitContext p;
    uint8_t buffer[200]; /* 100 * 9 / 8 = 113 */
    int i, left, w;
    const uint8_t *ptr;
    /* image block */

    bytestream_put_byte(bytestream, 0x2c);
    bytestream_put_le16(bytestream, x1);
    bytestream_put_le16(bytestream, y1);
    bytestream_put_le16(bytestream, width);
    bytestream_put_le16(bytestream, height);
    bytestream_put_byte(bytestream, 0x00); /* flags */
    /* no local clut */

    bytestream_put_byte(bytestream, 0x08);

    left= width * height;

    init_put_bits(&p, buffer, 130);

/*
 * the thing here is the bitstream is written as little packets, with a size byte before
 * but it's still the same bitstream between packets (no flush !)
 */
    ptr = buf;
    w = width;
    while(left>0) {

        put_bits(&p, 9, 0x0100); /* clear code */

        for(i=(left<GIF_CHUNKS)?left:GIF_CHUNKS;i;i--) {
            put_bits(&p, 9, *ptr++);
            if (--w == 0) {
                w = width;
                buf += linesize;
                ptr = buf;
            }
        }

        if(left<=GIF_CHUNKS) {
            put_bits(&p, 9, 0x101); /* end of stream */
            flush_put_bits(&p);
        }
        if(pbBufPtr(&p) - p.buf > 0) {
            bytestream_put_byte(bytestream, pbBufPtr(&p) - p.buf); /* byte count of the packet */
            bytestream_put_buffer(bytestream, p.buf, pbBufPtr(&p) - p.buf); /* the actual buffer */
            p.buf_ptr = p.buf; /* dequeue the bytes off the bitstream */
        }
        left-=GIF_CHUNKS;
    }
    bytestream_put_byte(bytestream, 0x00); /* end of image block */
    bytestream_put_byte(bytestream, 0x3b);
    return 0;
}

typedef struct {
    AVFrame picture;
} GIFContext;

static av_cold int gif_encode_init(AVCodecContext *avctx)
{
    GIFContext *s = avctx->priv_data;

    avctx->coded_frame = &s->picture;
    return 0;
}

/* better than nothing gif encoder */
static int gif_encode_frame(AVCodecContext *avctx, unsigned char *outbuf, int buf_size, void *data)
{
    GIFContext *s = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame *const p = (AVFrame *)&s->picture;
    uint8_t *outbuf_ptr = outbuf;

    *p = *pict;
    p->pict_type = FF_I_TYPE;
    p->key_frame = 1;
    gif_image_write_header(&outbuf_ptr, avctx->width, avctx->height, (uint32_t *)pict->data[1]);
    gif_image_write_image(&outbuf_ptr, 0, 0, avctx->width, avctx->height, pict->data[0], pict->linesize[0], PIX_FMT_PAL8);
    return outbuf_ptr - outbuf;
}

AVCodec gif_encoder = {
    "gif",
    CODEC_TYPE_VIDEO,
    CODEC_ID_GIF,
    sizeof(GIFContext),
    gif_encode_init,
    gif_encode_frame,
    NULL, //encode_end,
    .pix_fmts= (enum PixelFormat[]){PIX_FMT_RGB8, PIX_FMT_BGR8, PIX_FMT_RGB4_BYTE, PIX_FMT_BGR4_BYTE, PIX_FMT_GRAY8, PIX_FMT_PAL8, PIX_FMT_NONE},
    .long_name= NULL_IF_CONFIG_SMALL("GIF (Graphics Interchange Format)"),
};
