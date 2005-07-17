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
static int gif_image_write_header(ByteIOContext *pb, 
                                  int width, int height, int loop_count,
                                  uint32_t *palette)
{
    int i;
    unsigned int v;

    put_tag(pb, "GIF");
    put_tag(pb, "89a");
    put_le16(pb, width);
    put_le16(pb, height);

    put_byte(pb, 0xf7); /* flags: global clut, 256 entries */
    put_byte(pb, 0x1f); /* background color index */
    put_byte(pb, 0); /* aspect ratio */

    /* the global palette */
    if (!palette) {
        put_buffer(pb, (unsigned char *)gif_clut, 216*3);
        for(i=0;i<((256-216)*3);i++)
            put_byte(pb, 0);
    } else {
        for(i=0;i<256;i++) {
            v = palette[i];
            put_byte(pb, (v >> 16) & 0xff);
            put_byte(pb, (v >> 8) & 0xff);
            put_byte(pb, (v) & 0xff);
        }
    }

	/*	update: this is the 'NETSCAPE EXTENSION' that allows for looped animated gif
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
    put_byte(pb, 0x21);
    put_byte(pb, 0xff);
    put_byte(pb, 0x0b);
        put_tag(pb, "NETSCAPE2.0");  // bytes 4 to 14
        put_byte(pb, 0x03); // byte 15
        put_byte(pb, 0x01); // byte 16
        put_le16(pb, (uint16_t)loop_count);
        put_byte(pb, 0x00); // byte 19
    }
#endif
    return 0;
}

/* this is maybe slow, but allows for extensions */
static inline unsigned char gif_clut_index(uint8_t r, uint8_t g, uint8_t b)
{
    return ((((r)/47)%6)*6*6+(((g)/47)%6)*6+(((b)/47)%6));
}


static int gif_image_write_image(ByteIOContext *pb, 
                                 int x1, int y1, int width, int height,
                                 const uint8_t *buf, int linesize, int pix_fmt)
{
    PutBitContext p;
    uint8_t buffer[200]; /* 100 * 9 / 8 = 113 */
    int i, left, w, v;
    const uint8_t *ptr;
    /* image block */

    put_byte(pb, 0x2c);
    put_le16(pb, x1);
    put_le16(pb, y1);
    put_le16(pb, width);
    put_le16(pb, height);
    put_byte(pb, 0x00); /* flags */
    /* no local clut */

    put_byte(pb, 0x08);

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
            put_byte(pb, pbBufPtr(&p) - p.buf); /* byte count of the packet */
            put_buffer(pb, p.buf, pbBufPtr(&p) - p.buf); /* the actual buffer */
            p.buf_ptr = p.buf; /* dequeue the bytes off the bitstream */
        }
        left-=GIF_CHUNKS;
    }
    put_byte(pb, 0x00); /* end of image block */
    
    return 0;
}

typedef struct {
    int64_t time, file_time;
    uint8_t buffer[100]; /* data chunks */
} GIFContext;

static int gif_write_header(AVFormatContext *s)
{
    GIFContext *gif = s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVCodecContext *enc, *video_enc;
    int i, width, height, loop_count /*, rate*/;

/* XXX: do we reject audio streams or just ignore them ?
    if(s->nb_streams > 1)
        return -1;
*/
    gif->time = 0;
    gif->file_time = 0;

    video_enc = NULL;
    for(i=0;i<s->nb_streams;i++) {
        enc = s->streams[i]->codec;
        if (enc->codec_type != CODEC_TYPE_AUDIO)
            video_enc = enc;
    }

    if (!video_enc) {
        av_free(gif);
        return -1;
    } else {
        width = video_enc->width;
        height = video_enc->height;
        loop_count = s->loop_output;
//        rate = video_enc->time_base.den;
    }

    /* XXX: is it allowed ? seems to work so far... */
    video_enc->pix_fmt = PIX_FMT_RGB24;

    gif_image_write_header(pb, width, height, loop_count, NULL);

    put_flush_packet(&s->pb);
    return 0;
}

static int gif_write_video(AVFormatContext *s, 
                           AVCodecContext *enc, const uint8_t *buf, int size)
{
    ByteIOContext *pb = &s->pb;
    GIFContext *gif = s->priv_data;
    int jiffies;
    int64_t delay;

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
    jiffies = (70*enc->time_base.num/enc->time_base.den) - 1;

    put_le16(pb, jiffies);

    put_byte(pb, 0x1f); /* transparent color index */
    put_byte(pb, 0x00);

    gif_image_write_image(pb, 0, 0, enc->width, enc->height,
                          buf, enc->width * 3, PIX_FMT_RGB24);

    put_flush_packet(&s->pb);
    return 0;
}

static int gif_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecContext *codec = s->streams[pkt->stream_index]->codec;
    if (codec->codec_type == CODEC_TYPE_AUDIO)
        return 0; /* just ignore audio */
    else
        return gif_write_video(s, codec, pkt->data, pkt->size);
}

static int gif_write_trailer(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;

    put_byte(pb, 0x3b);
    put_flush_packet(&s->pb);
    return 0;
}

/* better than nothing gif image writer */
int gif_write(ByteIOContext *pb, AVImageInfo *info)
{
    gif_image_write_header(pb, info->width, info->height, AVFMT_NOOUTPUTLOOP, 
                           (uint32_t *)info->pict.data[1]);
    gif_image_write_image(pb, 0, 0, info->width, info->height, 
                          info->pict.data[0], info->pict.linesize[0], 
                          PIX_FMT_PAL8);
    put_byte(pb, 0x3b);
    put_flush_packet(pb);
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

extern AVInputFormat gif_iformat;

int gif_init(void)
{
    av_register_output_format(&gif_oformat);
    av_register_input_format(&gif_iformat);
    return 0;
}
