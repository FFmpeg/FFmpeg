/*
 * Flash Compatible Streaming Format
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
#include "avformat.h"

/* should have a generic way to indicate probable size */
#define DUMMY_FILE_SIZE   (100 * 1024 * 1024)
#define DUMMY_DURATION    600 /* in seconds */

#define TAG_END           0
#define TAG_SHOWFRAME     1
#define TAG_DEFINESHAPE   2
#define TAG_FREECHARACTER 3
#define TAG_PLACEOBJECT   4
#define TAG_REMOVEOBJECT  5
#define TAG_STREAMHEAD    18
#define TAG_STREAMBLOCK   19
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
    offset_t duration_pos;
    offset_t tag_pos;
    int tag;
} SWFContext;

static void put_swf_tag(AVFormatContext *s, int tag)
{
    SWFContext *swf = s->priv_data;
    ByteIOContext *pb = &s->pb;

    swf->tag_pos = url_ftell(pb);
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
    ByteIOContext *pb = &s->pb;
    offset_t pos;
    int tag_len, tag;

    pos = url_ftell(pb);
    tag_len = pos - swf->tag_pos - 2;
    tag = swf->tag;
    url_fseek(pb, swf->tag_pos, SEEK_SET);
    if (tag & TAG_LONG) {
        tag &= ~TAG_LONG;
        put_le16(pb, (tag << 6) | 0x3f);
        put_le32(pb, tag_len - 4);
    } else {
        assert(tag_len < 0x3f);
        put_le16(pb, (tag << 6) | tag_len);
    }
    url_fseek(pb, pos, SEEK_SET);
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

static void put_swf_rect(ByteIOContext *pb, 
                         int xmin, int xmax, int ymin, int ymax)
{
    PutBitContext p;
    uint8_t buf[256];
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
    put_buffer(pb, buf, pbBufPtr(&p) - p.buf);
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
static void put_swf_matrix(ByteIOContext *pb,
                           int a, int b, int c, int d, int tx, int ty)
{
    PutBitContext p;
    uint8_t buf[256];

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
    put_buffer(pb, buf, pbBufPtr(&p) - p.buf);
}

/* XXX: handle audio only */
static int swf_write_header(AVFormatContext *s)
{
    SWFContext *swf;
    ByteIOContext *pb = &s->pb;
    AVCodecContext *enc, *audio_enc, *video_enc;
    PutBitContext p;
    uint8_t buf1[256];
    int i, width, height, rate, rate_base;

    swf = av_malloc(sizeof(SWFContext));
    if (!swf)
        return -1;
    s->priv_data = swf;

    video_enc = NULL;
    audio_enc = NULL;
    for(i=0;i<s->nb_streams;i++) {
        enc = &s->streams[i]->codec;
        if (enc->codec_type == CODEC_TYPE_AUDIO)
            audio_enc = enc;
        else
            video_enc = enc;
    }

    if (!video_enc) {
        /* currenty, cannot work correctly if audio only */
        width = 320;
        height = 200;
        rate = 10;
        rate_base= 1;
    } else {
        width = video_enc->width;
        height = video_enc->height;
        rate = video_enc->frame_rate;
        rate_base = video_enc->frame_rate_base;
    }

    put_tag(pb, "FWS");
    put_byte(pb, 4); /* version (should use 4 for mpeg audio support) */
    put_le32(pb, DUMMY_FILE_SIZE); /* dummy size 
                                      (will be patched if not streamed) */ 

    put_swf_rect(pb, 0, width, 0, height);
    put_le16(pb, (rate * 256) / rate_base); /* frame rate */
    swf->duration_pos = url_ftell(pb);
    put_le16(pb, (uint16_t)(DUMMY_DURATION * (int64_t)rate / rate_base)); /* frame count */
    
    /* define a shape with the jpeg inside */

    put_swf_tag(s, TAG_DEFINESHAPE);

    put_le16(pb, SHAPE_ID); /* ID of shape */
    /* bounding rectangle */
    put_swf_rect(pb, 0, width, 0, height);
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
    put_swf_line_edge(&p, width, 0);
    put_swf_line_edge(&p, 0, height);
    put_swf_line_edge(&p, -width, 0);
    put_swf_line_edge(&p, 0, -height);
    
    /* end of shape */
    put_bits(&p, 1, 0); /* not an edge */
    put_bits(&p, 5, 0);

    flush_put_bits(&p);
    put_buffer(pb, buf1, pbBufPtr(&p) - p.buf);

    put_swf_end_tag(s);

    
    if (audio_enc) {
        int v;

        /* start sound */

        v = 0;
        switch(audio_enc->sample_rate) {
        case 11025:
            v |= 1 << 2;
            break;
        case 22050:
            v |= 2 << 2;
            break;
        case 44100:
            v |= 3 << 2;
            break;
        default:
            /* not supported */
            av_free(swf);
            return -1;
        }
        if (audio_enc->channels == 2)
            v |= 1;
        v |= 0x20; /* mp3 compressed */
        v |= 0x02; /* 16 bits */
        
        put_swf_tag(s, TAG_STREAMHEAD);
        put_byte(&s->pb, 0);
        put_byte(&s->pb, v);
        put_le16(&s->pb, (audio_enc->sample_rate * rate_base) / rate);  /* avg samples per frame */
        
        
        put_swf_end_tag(s);
    }

    put_flush_packet(&s->pb);
    return 0;
}

static int swf_write_video(AVFormatContext *s, 
                           AVCodecContext *enc, const uint8_t *buf, int size)
{
    ByteIOContext *pb = &s->pb;
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

static int swf_write_audio(AVFormatContext *s, const uint8_t *buf, int size)
{
    ByteIOContext *pb = &s->pb;

    put_swf_tag(s, TAG_STREAMBLOCK | TAG_LONG);

    put_buffer(pb, buf, size);
    
    put_swf_end_tag(s);
    put_flush_packet(&s->pb);
    return 0;
}

static int swf_write_packet(AVFormatContext *s, int stream_index, 
                           const uint8_t *buf, int size, int64_t pts)
{
    AVCodecContext *codec = &s->streams[stream_index]->codec;
    if (codec->codec_type == CODEC_TYPE_AUDIO)
        return swf_write_audio(s, buf, size);
    else
        return swf_write_video(s, codec, buf, size);
}

static int swf_write_trailer(AVFormatContext *s)
{
    SWFContext *swf = s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVCodecContext *enc, *video_enc;
    int file_size, i;

    video_enc = NULL;
    for(i=0;i<s->nb_streams;i++) {
        enc = &s->streams[i]->codec;
        if (enc->codec_type == CODEC_TYPE_VIDEO)
            video_enc = enc;
    }

    put_swf_tag(s, TAG_END);
    put_swf_end_tag(s);
    
    put_flush_packet(&s->pb);

    /* patch file size and number of frames if not streamed */
    if (!url_is_streamed(&s->pb) && video_enc) {
        file_size = url_ftell(pb);
        url_fseek(pb, 4, SEEK_SET);
        put_le32(pb, file_size);
        url_fseek(pb, swf->duration_pos, SEEK_SET);
        put_le16(pb, video_enc->frame_number);
    }
    return 0;
}

/***********************************/
/* just to extract MP3 from swf */

static int get_swf_tag(ByteIOContext *pb, int *len_ptr)
{
    int tag, len;
    
    if (url_feof(pb))
        return -1;

    tag = get_le16(pb);
    len = tag & 0x3f;
    tag = tag >> 6;
    if (len == 0x3f) {
        len = get_le32(pb);
    }
    *len_ptr = len;
    return tag;
}


static int swf_probe(AVProbeData *p)
{
    /* check file header */
    if (p->buf_size <= 16)
        return 0;
    if (p->buf[0] == 'F' && p->buf[1] == 'W' &&
        p->buf[2] == 'S')
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int swf_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    ByteIOContext *pb = &s->pb;
    int nbits, len, frame_rate, tag, v;
    AVStream *st;

    if ((get_be32(pb) & 0xffffff00) != MKBETAG('F', 'W', 'S', 0))
        return -EIO;
    get_le32(pb);
    /* skip rectangle size */
    nbits = get_byte(pb) >> 3;
    len = (4 * nbits - 3 + 7) / 8;
    url_fskip(pb, len);
    frame_rate = get_le16(pb);
    get_le16(pb); /* frame count */

    for(;;) {
        tag = get_swf_tag(pb, &len);
        if (tag < 0) {
            fprintf(stderr, "No streaming found in SWF\n");
            return -EIO;
        }
        if (tag == TAG_STREAMHEAD) {
            /* streaming found */
            get_byte(pb);
            v = get_byte(pb);
            get_le16(pb);
            /* if mp3 streaming found, OK */
            if ((v & 0x20) != 0) {
                st = av_new_stream(s, 0);
                if (!st)
                    return -ENOMEM;

                if (v & 0x01)
                    st->codec.channels = 2;
                else
                    st->codec.channels = 1;

                switch((v>> 2) & 0x03) {
                case 1:
                    st->codec.sample_rate = 11025;
                    break;
                case 2:
                    st->codec.sample_rate = 22050;
                    break;
                case 3:
                    st->codec.sample_rate = 44100;
                    break;
                default:
                    av_free(st);
                    return -EIO;
                }
                st->codec.codec_type = CODEC_TYPE_AUDIO;
                st->codec.codec_id = CODEC_ID_MP2;
                break;
            }
        } else {
            url_fskip(pb, len);
        }
    }

    return 0;
}

static int swf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = &s->pb;
    int tag, len;
    
    for(;;) {
        tag = get_swf_tag(pb, &len);
        if (tag < 0) 
            return -EIO;
        if (tag == TAG_STREAMBLOCK) {
            av_new_packet(pkt, len);
            get_buffer(pb, pkt->data, pkt->size);
            break;
        } else {
            url_fskip(pb, len);
        }
    }
    return 0;
}

static int swf_read_close(AVFormatContext *s)
{
     return 0;
}

static AVInputFormat swf_iformat = {
    "swf",
    "Flash format",
    0,
    swf_probe,
    swf_read_header,
    swf_read_packet,
    swf_read_close,
};

static AVOutputFormat swf_oformat = {
    "swf",
    "Flash format",
    "application/x-shockwave-flash",
    "swf",
    sizeof(SWFContext),
    CODEC_ID_MP2,
    CODEC_ID_MJPEG,
    swf_write_header,
    swf_write_packet,
    swf_write_trailer,
};

int swf_init(void)
{
    av_register_input_format(&swf_iformat);
    av_register_output_format(&swf_oformat);
    return 0;
}
