/*
 * Animated GIF encoder
 * Copyright (c) 2000 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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

#include "avformat.h"

/* bitstream minipacket size */
#define GIF_CHUNKS 100

/* slows down the decoding (and some browsers doesn't like it) */
/* #define GIF_ADD_APP_HEADER */

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

const rgb_triplet gif_clut[216] = {
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

void init_put_bits_rev(PutBitContext *s,
                   UINT8 *buffer, int buffer_size,
                   void *opaque,
                   void (*write_data)(void *, UINT8 *, int))
{
    init_put_bits(s, buffer, buffer_size, opaque, write_data);
}

void put_bits_rev(PutBitContext *s, int n, unsigned int value)
{
    unsigned int bit_buf;
    int bit_cnt;

#ifdef STATS
    st_out_bit_counts[st_current_index] += n;
#endif
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

/* return the number of bits output */
INT64 get_bit_count_rev(PutBitContext *s)
{
    return get_bit_count(s);
}

void align_put_bits_rev(PutBitContext *s)
{
    align_put_bits(s);
}

/* pad the end of the output stream with zeros */
void flush_put_bits_rev(PutBitContext *s)
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

typedef struct {
    UINT8 buffer[100]; /* data chunks */
    INT64 time, file_time;
} GIFContext;

static int gif_write_header(AVFormatContext *s)
{
    GIFContext *gif = s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVCodecContext *enc, *video_enc;
    int i, width, height, rate;

/* XXX: do we reject audio streams or just ignore them ?
    if(s->nb_streams > 1)
        return -1;
*/
    gif->time = 0;
    gif->file_time = 0;

    video_enc = NULL;
    for(i=0;i<s->nb_streams;i++) {
        enc = &s->streams[i]->codec;
        if (enc->codec_type != CODEC_TYPE_AUDIO)
            video_enc = enc;
    }

    if (!video_enc) {
        av_free(gif);
        return -1;
    } else {
        width = video_enc->width;
        height = video_enc->height;
        rate = video_enc->frame_rate;
    }

    /* XXX: is it allowed ? seems to work so far... */
    video_enc->pix_fmt = PIX_FMT_RGB24;

    /* GIF header */

    put_tag(pb, "GIF");
    put_tag(pb, "89a");
    put_le16(pb, width);
    put_le16(pb, height);

    put_byte(pb, 0xf7); /* flags: global clut, 256 entries */
    put_byte(pb, 0x1f); /* background color index */
    put_byte(pb, 0); /* aspect ratio */

    /* the global palette */

    put_buffer(pb, (unsigned char *)gif_clut, 216*3);
    for(i=0;i<((256-216)*3);i++)
       put_byte(pb, 0);

    /* application extension header */
    /* XXX: not really sure what to put in here... */
#ifdef GIF_ADD_APP_HEADER
    put_byte(pb, 0x21);
    put_byte(pb, 0xff);
    put_byte(pb, 0x0b);
    put_tag(pb, "NETSCAPE2.0");
    put_byte(pb, 0x03);
    put_byte(pb, 0x01);
    put_byte(pb, 0x00);
    put_byte(pb, 0x00);
#endif

    put_flush_packet(&s->pb);
    return 0;
}

/* this is maybe slow, but allows for extensions */
static inline unsigned char gif_clut_index(rgb_triplet *clut, UINT8 r, UINT8 g, UINT8 b)
{
    return ((((r)/47)%6)*6*6+(((g)/47)%6)*6+(((b)/47)%6));
}

/* chunk writer callback */
/* !!! XXX:deprecated
static void gif_put_chunk(void *pbctx, UINT8 *buffer, int count)
{
    ByteIOContext *pb = (ByteIOContext *)pbctx;
    put_byte(pb, (UINT8)count);
    put_buffer(pb, buffer, count);
}
*/

static int gif_write_video(AVFormatContext *s, 
                           AVCodecContext *enc, UINT8 *buf, int size)
{
    ByteIOContext *pb = &s->pb;
    GIFContext *gif = s->priv_data;
    int i, left, jiffies;
    INT64 delay;
    PutBitContext p;
    UINT8 buffer[200]; /* 100 * 9 / 8 = 113 */


    /* graphic control extension block */
    put_byte(pb, 0x21);
    put_byte(pb, 0xf9);
    put_byte(pb, 0x04); /* block size */
    put_byte(pb, 0x04); /* flags */
    
    /* 1 jiffy is 1/70 s */
    /* the delay_time field indicates the number of jiffies - 1 */
    delay = gif->file_time - gif->time;

    /* XXX: should use delay, in order to be more accurate */
    /* instead of using the same rounded value each time */
    /* XXX: don't even remember if I really use it for now */
    jiffies = (70*FRAME_RATE_BASE/enc->frame_rate) - 1;

    put_le16(pb, jiffies);

    put_byte(pb, 0x1f); /* transparent color index */
    put_byte(pb, 0x00);

    /* image block */

    put_byte(pb, 0x2c);
    put_le16(pb, 0);
    put_le16(pb, 0);
    put_le16(pb, enc->width);
    put_le16(pb, enc->height);
    put_byte(pb, 0x00); /* flags */
    /* no local clut */

    put_byte(pb, 0x08);

    left=size/3;

    /* XXX:deprecated */
    /*init_put_bits_rev(&p, buffer, sizeof(buf), (void *)pb, gif_put_chunk); *//* mmm found a but in my code: s/sizeof(buf)/150/ */

    init_put_bits_rev(&p, buffer, 130, NULL, NULL);

/*
 * the thing here is the bitstream is written as little packets, with a size byte before
 * but it's still the same bitstream between packets (no flush !)
 */

    while(left>0) {

        put_bits_rev(&p, 9, 0x0100); /* clear code */

        for(i=0;i<GIF_CHUNKS;i++) {
            put_bits_rev(&p, 9, gif_clut_index(NULL, *buf, buf[1], buf[2]));
            buf+=3;
        }

        if(left<=GIF_CHUNKS) {
            put_bits_rev(&p, 9, 0x101); /* end of stream */
            flush_put_bits_rev(&p);
        }
        if(pbBufPtr(&p) - p.buf > 0) {
            put_byte(pb, pbBufPtr(&p) - p.buf); /* byte count of the packet */
            put_buffer(pb, p.buf, pbBufPtr(&p) - p.buf); /* the actual buffer */
            p.data_out_size += pbBufPtr(&p) - p.buf;
            p.buf_ptr = p.buf; /* dequeue the bytes off the bitstream */
        }
        if(left<=GIF_CHUNKS) {
            put_byte(pb, 0x00); /* end of image block */
        }

        left-=GIF_CHUNKS;
    }

    put_flush_packet(&s->pb);
    return 0;
}

static int gif_write_packet(AVFormatContext *s, int stream_index, 
                           UINT8 *buf, int size, int force_pts)
{
    AVCodecContext *codec = &s->streams[stream_index]->codec;
    if (codec->codec_type == CODEC_TYPE_AUDIO)
        return 0; /* just ignore audio */
    else
        return gif_write_video(s, codec, buf, size);
}

static int gif_write_trailer(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;

    put_byte(pb, 0x3b);
    put_flush_packet(&s->pb);
    return 0;
}

static AVOutputFormat gif_oformat = {
    "gif",
    "GIF Animation",
    "image/gif",
    "gif",
    sizeof(GIFContext),
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    gif_write_header,
    gif_write_packet,
    gif_write_trailer,
};

int gif_init(void)
{
    av_register_output_format(&gif_oformat);
    return 0;
}
