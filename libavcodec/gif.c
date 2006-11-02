/*
 * GIF encoder.
 * Copyright (c) 2000 Fabrice Bellard.
 * Copyright (c) 2002 Francois Revol.
 * Copyright (c) 2006 Baptiste Coudurier.
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
#include "bitstream.h"

/* bitstream minipacket size */
#define GIF_CHUNKS 100

/* slows down the decoding (and some browsers don't like it) */
/* update on the 'some browsers don't like it issue from above: this was probably due to missing 'Data Sub-block Terminator' (byte 19) in the app_header */
#define GIF_ADD_APP_HEADER // required to enable looping of animated gif

typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
} rgb_triplet;

/* we use the standard 216 color palette */

/* this script was used to create the palette:
 * for r in 00 33 66 99 cc ff; do for g in 00 33 66 99 cc ff; do echo -n "    "; for b in 00 33 66 99 cc ff; do
 *   echo -n "{ 0x$r, 0x$g, 0x$b }, "; done; echo ""; done; done
 */

static const rgb_triplet gif_clut[216] = {
    { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x33 }, { 0x00, 0x00, 0x66 }, { 0x00, 0x00, 0x99 }, { 0x00, 0x00, 0xcc }, { 0x00, 0x00, 0xff },
    { 0x00, 0x33, 0x00 }, { 0x00, 0x33, 0x33 }, { 0x00, 0x33, 0x66 }, { 0x00, 0x33, 0x99 }, { 0x00, 0x33, 0xcc }, { 0x00, 0x33, 0xff },
    { 0x00, 0x66, 0x00 }, { 0x00, 0x66, 0x33 }, { 0x00, 0x66, 0x66 }, { 0x00, 0x66, 0x99 }, { 0x00, 0x66, 0xcc }, { 0x00, 0x66, 0xff },
    { 0x00, 0x99, 0x00 }, { 0x00, 0x99, 0x33 }, { 0x00, 0x99, 0x66 }, { 0x00, 0x99, 0x99 }, { 0x00, 0x99, 0xcc }, { 0x00, 0x99, 0xff },
    { 0x00, 0xcc, 0x00 }, { 0x00, 0xcc, 0x33 }, { 0x00, 0xcc, 0x66 }, { 0x00, 0xcc, 0x99 }, { 0x00, 0xcc, 0xcc }, { 0x00, 0xcc, 0xff },
    { 0x00, 0xff, 0x00 }, { 0x00, 0xff, 0x33 }, { 0x00, 0xff, 0x66 }, { 0x00, 0xff, 0x99 }, { 0x00, 0xff, 0xcc }, { 0x00, 0xff, 0xff },
    { 0x33, 0x00, 0x00 }, { 0x33, 0x00, 0x33 }, { 0x33, 0x00, 0x66 }, { 0x33, 0x00, 0x99 }, { 0x33, 0x00, 0xcc }, { 0x33, 0x00, 0xff },
    { 0x33, 0x33, 0x00 }, { 0x33, 0x33, 0x33 }, { 0x33, 0x33, 0x66 }, { 0x33, 0x33, 0x99 }, { 0x33, 0x33, 0xcc }, { 0x33, 0x33, 0xff },
    { 0x33, 0x66, 0x00 }, { 0x33, 0x66, 0x33 }, { 0x33, 0x66, 0x66 }, { 0x33, 0x66, 0x99 }, { 0x33, 0x66, 0xcc }, { 0x33, 0x66, 0xff },
    { 0x33, 0x99, 0x00 }, { 0x33, 0x99, 0x33 }, { 0x33, 0x99, 0x66 }, { 0x33, 0x99, 0x99 }, { 0x33, 0x99, 0xcc }, { 0x33, 0x99, 0xff },
    { 0x33, 0xcc, 0x00 }, { 0x33, 0xcc, 0x33 }, { 0x33, 0xcc, 0x66 }, { 0x33, 0xcc, 0x99 }, { 0x33, 0xcc, 0xcc }, { 0x33, 0xcc, 0xff },
    { 0x33, 0xff, 0x00 }, { 0x33, 0xff, 0x33 }, { 0x33, 0xff, 0x66 }, { 0x33, 0xff, 0x99 }, { 0x33, 0xff, 0xcc }, { 0x33, 0xff, 0xff },
    { 0x66, 0x00, 0x00 }, { 0x66, 0x00, 0x33 }, { 0x66, 0x00, 0x66 }, { 0x66, 0x00, 0x99 }, { 0x66, 0x00, 0xcc }, { 0x66, 0x00, 0xff },
    { 0x66, 0x33, 0x00 }, { 0x66, 0x33, 0x33 }, { 0x66, 0x33, 0x66 }, { 0x66, 0x33, 0x99 }, { 0x66, 0x33, 0xcc }, { 0x66, 0x33, 0xff },
    { 0x66, 0x66, 0x00 }, { 0x66, 0x66, 0x33 }, { 0x66, 0x66, 0x66 }, { 0x66, 0x66, 0x99 }, { 0x66, 0x66, 0xcc }, { 0x66, 0x66, 0xff },
    { 0x66, 0x99, 0x00 }, { 0x66, 0x99, 0x33 }, { 0x66, 0x99, 0x66 }, { 0x66, 0x99, 0x99 }, { 0x66, 0x99, 0xcc }, { 0x66, 0x99, 0xff },
    { 0x66, 0xcc, 0x00 }, { 0x66, 0xcc, 0x33 }, { 0x66, 0xcc, 0x66 }, { 0x66, 0xcc, 0x99 }, { 0x66, 0xcc, 0xcc }, { 0x66, 0xcc, 0xff },
    { 0x66, 0xff, 0x00 }, { 0x66, 0xff, 0x33 }, { 0x66, 0xff, 0x66 }, { 0x66, 0xff, 0x99 }, { 0x66, 0xff, 0xcc }, { 0x66, 0xff, 0xff },
    { 0x99, 0x00, 0x00 }, { 0x99, 0x00, 0x33 }, { 0x99, 0x00, 0x66 }, { 0x99, 0x00, 0x99 }, { 0x99, 0x00, 0xcc }, { 0x99, 0x00, 0xff },
    { 0x99, 0x33, 0x00 }, { 0x99, 0x33, 0x33 }, { 0x99, 0x33, 0x66 }, { 0x99, 0x33, 0x99 }, { 0x99, 0x33, 0xcc }, { 0x99, 0x33, 0xff },
    { 0x99, 0x66, 0x00 }, { 0x99, 0x66, 0x33 }, { 0x99, 0x66, 0x66 }, { 0x99, 0x66, 0x99 }, { 0x99, 0x66, 0xcc }, { 0x99, 0x66, 0xff },
    { 0x99, 0x99, 0x00 }, { 0x99, 0x99, 0x33 }, { 0x99, 0x99, 0x66 }, { 0x99, 0x99, 0x99 }, { 0x99, 0x99, 0xcc }, { 0x99, 0x99, 0xff },
    { 0x99, 0xcc, 0x00 }, { 0x99, 0xcc, 0x33 }, { 0x99, 0xcc, 0x66 }, { 0x99, 0xcc, 0x99 }, { 0x99, 0xcc, 0xcc }, { 0x99, 0xcc, 0xff },
    { 0x99, 0xff, 0x00 }, { 0x99, 0xff, 0x33 }, { 0x99, 0xff, 0x66 }, { 0x99, 0xff, 0x99 }, { 0x99, 0xff, 0xcc }, { 0x99, 0xff, 0xff },
    { 0xcc, 0x00, 0x00 }, { 0xcc, 0x00, 0x33 }, { 0xcc, 0x00, 0x66 }, { 0xcc, 0x00, 0x99 }, { 0xcc, 0x00, 0xcc }, { 0xcc, 0x00, 0xff },
    { 0xcc, 0x33, 0x00 }, { 0xcc, 0x33, 0x33 }, { 0xcc, 0x33, 0x66 }, { 0xcc, 0x33, 0x99 }, { 0xcc, 0x33, 0xcc }, { 0xcc, 0x33, 0xff },
    { 0xcc, 0x66, 0x00 }, { 0xcc, 0x66, 0x33 }, { 0xcc, 0x66, 0x66 }, { 0xcc, 0x66, 0x99 }, { 0xcc, 0x66, 0xcc }, { 0xcc, 0x66, 0xff },
    { 0xcc, 0x99, 0x00 }, { 0xcc, 0x99, 0x33 }, { 0xcc, 0x99, 0x66 }, { 0xcc, 0x99, 0x99 }, { 0xcc, 0x99, 0xcc }, { 0xcc, 0x99, 0xff },
    { 0xcc, 0xcc, 0x00 }, { 0xcc, 0xcc, 0x33 }, { 0xcc, 0xcc, 0x66 }, { 0xcc, 0xcc, 0x99 }, { 0xcc, 0xcc, 0xcc }, { 0xcc, 0xcc, 0xff },
    { 0xcc, 0xff, 0x00 }, { 0xcc, 0xff, 0x33 }, { 0xcc, 0xff, 0x66 }, { 0xcc, 0xff, 0x99 }, { 0xcc, 0xff, 0xcc }, { 0xcc, 0xff, 0xff },
    { 0xff, 0x00, 0x00 }, { 0xff, 0x00, 0x33 }, { 0xff, 0x00, 0x66 }, { 0xff, 0x00, 0x99 }, { 0xff, 0x00, 0xcc }, { 0xff, 0x00, 0xff },
    { 0xff, 0x33, 0x00 }, { 0xff, 0x33, 0x33 }, { 0xff, 0x33, 0x66 }, { 0xff, 0x33, 0x99 }, { 0xff, 0x33, 0xcc }, { 0xff, 0x33, 0xff },
    { 0xff, 0x66, 0x00 }, { 0xff, 0x66, 0x33 }, { 0xff, 0x66, 0x66 }, { 0xff, 0x66, 0x99 }, { 0xff, 0x66, 0xcc }, { 0xff, 0x66, 0xff },
    { 0xff, 0x99, 0x00 }, { 0xff, 0x99, 0x33 }, { 0xff, 0x99, 0x66 }, { 0xff, 0x99, 0x99 }, { 0xff, 0x99, 0xcc }, { 0xff, 0x99, 0xff },
    { 0xff, 0xcc, 0x00 }, { 0xff, 0xcc, 0x33 }, { 0xff, 0xcc, 0x66 }, { 0xff, 0xcc, 0x99 }, { 0xff, 0xcc, 0xcc }, { 0xff, 0xcc, 0xff },
    { 0xff, 0xff, 0x00 }, { 0xff, 0xff, 0x33 }, { 0xff, 0xff, 0x66 }, { 0xff, 0xff, 0x99 }, { 0xff, 0xff, 0xcc }, { 0xff, 0xff, 0xff },
};

/* The GIF format uses reversed order for bitstreams... */
/* at least they don't use PDP_ENDIAN :) */
/* so we 'extend' PutBitContext. hmmm, OOP :) */
/* seems this thing changed slightly since I wrote it... */

#ifdef ALT_BITSTREAM_WRITER
# error no ALT_BITSTREAM_WRITER support for now
#endif

static void gif_put_bits_rev(PutBitContext *s, int n, unsigned int value)
{
    unsigned int bit_buf;
    int bit_cnt;

    //    printf("put_bits=%d %x\n", n, value);
    assert(n == 32 || value < (1U << n));

    bit_buf = s->bit_buf;
    bit_cnt = 32 - s->bit_left; /* XXX:lazyness... was = s->bit_cnt; */

    //    printf("n=%d value=%x cnt=%d buf=%x\n", n, value, bit_cnt, bit_buf);
    /* XXX: optimize */
    if (n < (32-bit_cnt)) {
        bit_buf |= value << (bit_cnt);
        bit_cnt+=n;
    } else {
        bit_buf |= value << (bit_cnt);

        *s->buf_ptr = bit_buf & 0xff;
        s->buf_ptr[1] = (bit_buf >> 8) & 0xff;
        s->buf_ptr[2] = (bit_buf >> 16) & 0xff;
        s->buf_ptr[3] = (bit_buf >> 24) & 0xff;

        //printf("bitbuf = %08x\n", bit_buf);
        s->buf_ptr+=4;
        if (s->buf_ptr >= s->buf_end)
            puts("bit buffer overflow !!"); // should never happen ! who got rid of the callback ???
//            flush_buffer_rev(s);
        bit_cnt=bit_cnt + n - 32;
        if (bit_cnt == 0) {
            bit_buf = 0;
        } else {
            bit_buf = value >> (n - bit_cnt);
        }
    }

    s->bit_buf = bit_buf;
    s->bit_left = 32 - bit_cnt;
}

/* pad the end of the output stream with zeros */
static void gif_flush_put_bits_rev(PutBitContext *s)
{
    while (s->bit_left < 32) {
        /* XXX: should test end of buffer */
        *s->buf_ptr++=s->bit_buf & 0xff;
        s->bit_buf>>=8;
        s->bit_left+=8;
    }
//    flush_buffer_rev(s);
    s->bit_left=32;
    s->bit_buf=0;
}

/* !RevPutBitContext */

/* GIF header */
static int gif_image_write_header(uint8_t **bytestream,
                                  int width, int height, int loop_count,
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
    if (!palette) {
        bytestream_put_buffer(bytestream, (const unsigned char *)gif_clut, 216*3);
        for(i=0;i<((256-216)*3);i++)
            bytestream_put_byte(bytestream, 0);
    } else {
        for(i=0;i<256;i++) {
            v = palette[i];
            bytestream_put_byte(bytestream, (v >> 16) & 0xff);
            bytestream_put_byte(bytestream, (v >> 8) & 0xff);
            bytestream_put_byte(bytestream, (v) & 0xff);
        }
    }

        /*        update: this is the 'NETSCAPE EXTENSION' that allows for looped animated gif
                see http://members.aol.com/royalef/gifabout.htm#net-extension

                byte   1       : 33 (hex 0x21) GIF Extension code
                byte   2       : 255 (hex 0xFF) Application Extension Label
                byte   3       : 11 (hex (0x0B) Length of Application Block
                                         (eleven bytes of data to follow)
                bytes  4 to 11 : "NETSCAPE"
                bytes 12 to 14 : "2.0"
                byte  15       : 3 (hex 0x03) Length of Data Sub-Block
                                         (three bytes of data to follow)
                byte  16       : 1 (hex 0x01)
                bytes 17 to 18 : 0 to 65535, an unsigned integer in
                                         lo-hi byte format. This indicate the
                                         number of iterations the loop should
                                         be executed.
                bytes 19       : 0 (hex 0x00) a Data Sub-block Terminator
        */

    /* application extension header */
#ifdef GIF_ADD_APP_HEADER
    if (loop_count >= 0 && loop_count <= 65535) {
        bytestream_put_byte(bytestream, 0x21);
        bytestream_put_byte(bytestream, 0xff);
        bytestream_put_byte(bytestream, 0x0b);
        bytestream_put_buffer(bytestream, "NETSCAPE2.0", 11);  // bytes 4 to 14
        bytestream_put_byte(bytestream, 0x03); // byte 15
        bytestream_put_byte(bytestream, 0x01); // byte 16
        bytestream_put_le16(bytestream, (uint16_t)loop_count);
        bytestream_put_byte(bytestream, 0x00); // byte 19
    }
#endif
    return 0;
}

/* this is maybe slow, but allows for extensions */
static inline unsigned char gif_clut_index(uint8_t r, uint8_t g, uint8_t b)
{
    return ((((r)/47)%6)*6*6+(((g)/47)%6)*6+(((b)/47)%6));
}


static int gif_image_write_image(uint8_t **bytestream,
                                 int x1, int y1, int width, int height,
                                 const uint8_t *buf, int linesize, int pix_fmt)
{
    PutBitContext p;
    uint8_t buffer[200]; /* 100 * 9 / 8 = 113 */
    int i, left, w, v;
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

        gif_put_bits_rev(&p, 9, 0x0100); /* clear code */

        for(i=(left<GIF_CHUNKS)?left:GIF_CHUNKS;i;i--) {
            if (pix_fmt == PIX_FMT_RGB24) {
                v = gif_clut_index(ptr[0], ptr[1], ptr[2]);
                ptr+=3;
            } else {
                v = *ptr++;
            }
            gif_put_bits_rev(&p, 9, v);
            if (--w == 0) {
                w = width;
                buf += linesize;
                ptr = buf;
            }
        }

        if(left<=GIF_CHUNKS) {
            gif_put_bits_rev(&p, 9, 0x101); /* end of stream */
            gif_flush_put_bits_rev(&p);
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
    int64_t time, file_time;
    uint8_t buffer[100]; /* data chunks */
    AVFrame picture;
} GIFContext;

static int gif_encode_init(AVCodecContext *avctx)
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
    gif_image_write_header(&outbuf_ptr, avctx->width, avctx->height, -1, (uint32_t *)pict->data[1]);
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
    .pix_fmts= (enum PixelFormat[]){PIX_FMT_PAL8, -1},
};
