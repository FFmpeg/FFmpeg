/*
 * Flash Compatible Streaming Format
 * Copyright (c) 2000 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <string.h>
#include "mpegenc.h"

#include <assert.h>

/* should have a generic way to indicate probable size */
#define DUMMY_FILE_SIZE   (100 * 1024 * 1024)
#define DUMMY_DURATION    600 /* in seconds */

#define TAG_END           0
#define TAG_SHOWFRAME     1
#define TAG_DEFINESHAPE   2
#define TAG_FREECHARACTER 3
#define TAG_PLACEOBJECT   4
#define TAG_REMOVEOBJECT  5
#define TAG_JPEG2         21

#define TAG_LONG         0x100

/* flags for shape definition */
#define FLAG_MOVETO      0x01
#define FLAG_SETFILL0    0x02
#define FLAG_SETFILL1    0x04

/* character id used */
#define BITMAP_ID 0
#define SHAPE_ID  1

typedef struct {
    long long duration_pos;
    long long tag_pos;
    int tag;
} SWFContext;

static void put_swf_tag(AVFormatContext *s, int tag)
{
    SWFContext *swf = s->priv_data;
    PutByteContext *pb = &s->pb;

    swf->tag_pos = put_pos(pb);
    swf->tag = tag;
    /* reserve some room for the tag */
    if (tag & TAG_LONG) {
        put_le16(pb, 0);
        put_le32(pb, 0);
    } else {
        put_le16(pb, 0);
    }
}

static void put_swf_end_tag(AVFormatContext *s)
{
    SWFContext *swf = s->priv_data;
    PutByteContext *pb = &s->pb;
    long long pos;
    int tag_len, tag;

    pos = put_pos(pb);
    tag_len = pos - swf->tag_pos - 2;
    tag = swf->tag;
    put_seek(pb, swf->tag_pos, SEEK_SET);
    if (tag & TAG_LONG) {
        tag &= ~TAG_LONG;
        put_le16(pb, (tag << 6) | 0x3f);
        put_le32(pb, tag_len - 4);
    } else {
        assert(tag_len < 0x3f);
        put_le16(pb, (tag << 6) | tag_len);
    }
    put_seek(pb, pos, SEEK_SET);
}

static inline void max_nbits(int *nbits_ptr, int val)
{
    int n;

    if (val == 0)
        return;
    val = abs(val);
    n = 1;
    while (val != 0) {
        n++;
        val >>= 1;
    }
    if (n > *nbits_ptr)
        *nbits_ptr = n;
}

static void put_swf_rect(PutByteContext *pb, 
                         int xmin, int xmax, int ymin, int ymax)
{
    PutBitContext p;
    UINT8 buf[256];
    int nbits, mask;

    init_put_bits(&p, buf, sizeof(buf), NULL, NULL);
    
    nbits = 0;
    max_nbits(&nbits, xmin);
    max_nbits(&nbits, xmax);
    max_nbits(&nbits, ymin);
    max_nbits(&nbits, ymax);
    mask = (1 << nbits) - 1;

    /* rectangle info */
    put_bits(&p, 5, nbits);
    put_bits(&p, nbits, xmin & mask);
    put_bits(&p, nbits, xmax & mask);
    put_bits(&p, nbits, ymin & mask);
    put_bits(&p, nbits, ymax & mask);
    
    flush_put_bits(&p);
    put_buffer(pb, buf, p.buf_ptr - p.buf);
}

static void put_swf_line_edge(PutBitContext *pb, int dx, int dy)
{
    int nbits, mask;

    put_bits(pb, 1, 1); /* edge */
    put_bits(pb, 1, 1); /* line select */
    nbits = 2;
    max_nbits(&nbits, dx);
    max_nbits(&nbits, dy);

    mask = (1 << nbits) - 1;
    put_bits(pb, 4, nbits - 2); /* 16 bits precision */
    if (dx == 0) {
      put_bits(pb, 1, 0); 
      put_bits(pb, 1, 1); 
      put_bits(pb, nbits, dy & mask);
    } else if (dy == 0) {
      put_bits(pb, 1, 0); 
      put_bits(pb, 1, 0); 
      put_bits(pb, nbits, dx & mask);
    } else {
      put_bits(pb, 1, 1); 
      put_bits(pb, nbits, dx & mask);
      put_bits(pb, nbits, dy & mask);
    }
}

#define FRAC_BITS 16

/* put matrix (not size optimized */
static void put_swf_matrix(PutByteContext *pb,
                           int a, int b, int c, int d, int tx, int ty)
{
    PutBitContext p;
    UINT8 buf[256];

    init_put_bits(&p, buf, sizeof(buf), NULL, NULL);
    
    put_bits(&p, 1, 1); /* a, d present */
    put_bits(&p, 5, 20); /* nb bits */
    put_bits(&p, 20, a);
    put_bits(&p, 20, d);
    
    put_bits(&p, 1, 1); /* b, c present */
    put_bits(&p, 5, 20); /* nb bits */
    put_bits(&p, 20, c);
    put_bits(&p, 20, b);

    put_bits(&p, 5, 20); /* nb bits */
    put_bits(&p, 20, tx);
    put_bits(&p, 20, ty);

    flush_put_bits(&p);
    put_buffer(pb, buf, p.buf_ptr - p.buf);
}

static int swf_write_header(AVFormatContext *s)
{
    SWFContext *swf;
    PutByteContext *pb = &s->pb;
    AVEncodeContext *enc = s->video_enc;
    PutBitContext p;
    UINT8 buf1[256];

    swf = malloc(sizeof(SWFContext));
    if (!swf)
        return -1;
    s->priv_data = swf;

    put_tag(pb, "FWS");
    put_byte(pb, 3); /* version (should use 4 for mpeg audio support) */
    put_le32(pb, DUMMY_FILE_SIZE); /* dummy size 
                                      (will be patched if not streamed) */ 

    put_swf_rect(pb, 0, enc->width, 0, enc->height);
    put_le16(pb, enc->rate << 8); /* frame rate */
    swf->duration_pos = put_pos(pb);
    put_le16(pb, DUMMY_DURATION * enc->rate); /* frame count */
    
    /* define a shape with the jpeg inside */

    put_swf_tag(s, TAG_DEFINESHAPE);

    put_le16(pb, SHAPE_ID); /* ID of shape */
    /* bounding rectangle */
    put_swf_rect(pb, 0, enc->width, 0, enc->height);
    /* style info */
    put_byte(pb, 1); /* one fill style */
    put_byte(pb, 0x41); /* clipped bitmap fill */
    put_le16(pb, BITMAP_ID); /* bitmap ID */
    /* position of the bitmap */
    put_swf_matrix(pb, (int)(1.0 * (1 << FRAC_BITS)), 0, 
                   0, (int)(1.0 * (1 << FRAC_BITS)), 0, 0);
    put_byte(pb, 0); /* no line style */
    
    /* shape drawing */
    init_put_bits(&p, buf1, sizeof(buf1), NULL, NULL);
    put_bits(&p, 4, 1); /* one fill bit */
    put_bits(&p, 4, 0); /* zero line bit */
    
    put_bits(&p, 1, 0); /* not an edge */
    put_bits(&p, 5, FLAG_MOVETO | FLAG_SETFILL0);
    put_bits(&p, 5, 1); /* nbits */
    put_bits(&p, 1, 0); /* X */
    put_bits(&p, 1, 0); /* Y */
    put_bits(&p, 1, 1); /* set fill style 1 */
    
    /* draw the rectangle ! */
    put_swf_line_edge(&p, enc->width, 0);
    put_swf_line_edge(&p, 0, enc->height);
    put_swf_line_edge(&p, -enc->width, 0);
    put_swf_line_edge(&p, 0, -enc->height);
    
    /* end of shape */
    put_bits(&p, 1, 0); /* not an edge */
    put_bits(&p, 5, 0);

    flush_put_bits(&p);
    put_buffer(pb, buf1, p.buf_ptr - p.buf);

    put_swf_end_tag(s);

    put_flush_packet(&s->pb);
    return 0;
}

static int swf_write_video(AVFormatContext *s, UINT8 *buf, int size)
{
    PutByteContext *pb = &s->pb;
    AVEncodeContext *enc = s->video_enc;
    static int tag_id = 0;

    if (enc->frame_number > 1) {
        /* remove the shape */
        put_swf_tag(s, TAG_REMOVEOBJECT);
        put_le16(pb, SHAPE_ID); /* shape ID */
        put_le16(pb, 1); /* depth */
        put_swf_end_tag(s);
        
        /* free the bitmap */
        put_swf_tag(s, TAG_FREECHARACTER);
        put_le16(pb, BITMAP_ID);
        put_swf_end_tag(s);
    }

    put_swf_tag(s, TAG_JPEG2 | TAG_LONG);

    put_le16(pb, tag_id); /* ID of the image */

    /* a dummy jpeg header seems to be required */
    put_byte(pb, 0xff); 
    put_byte(pb, 0xd8);
    put_byte(pb, 0xff);
    put_byte(pb, 0xd9);
    /* write the jpeg image */
    put_buffer(pb, buf, size);

    put_swf_end_tag(s);

    /* draw the shape */

    put_swf_tag(s, TAG_PLACEOBJECT);
    put_le16(pb, SHAPE_ID); /* shape ID */
    put_le16(pb, 1); /* depth */
    put_swf_matrix(pb, 1 << FRAC_BITS, 0, 0, 1 << FRAC_BITS, 0, 0);
    put_swf_end_tag(s);
    
    /* output the frame */
    put_swf_tag(s, TAG_SHOWFRAME);
    put_swf_end_tag(s);
    
    put_flush_packet(&s->pb);
    return 0;
}

static int swf_write_trailer(AVFormatContext *s)
{
    SWFContext *swf = s->priv_data;
    PutByteContext *pb = &s->pb;
    int file_size;
    AVEncodeContext *enc = s->video_enc;

    put_swf_tag(s, TAG_END);
    put_swf_end_tag(s);
    
    put_flush_packet(&s->pb);

    /* patch file size and number of frames if not streamed */
    if (!s->is_streamed) {
        file_size = put_pos(pb);
        put_seek(pb, 4, SEEK_SET);
        put_le32(pb, file_size);
        put_seek(pb, swf->duration_pos, SEEK_SET);
        put_le16(pb, enc->frame_number);
    }
    free(swf);
    return 0;
}

AVFormat swf_format = {
    "swf",
    "Flash format",
    "application/x-shockwave-flash",
    "swf",
    CODEC_ID_NONE,
    CODEC_ID_MJPEG,
    swf_write_header,
    NULL,
    swf_write_video,
    swf_write_trailer,
};
