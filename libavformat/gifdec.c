/*
 * GIF decoder
 * Copyright (c) 2003 Fabrice Bellard.
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

//#define DEBUG

#define MAXBITS		12
#define	SIZTABLE	(1<<MAXBITS)

#define GCE_DISPOSAL_NONE       0
#define GCE_DISPOSAL_INPLACE    1
#define GCE_DISPOSAL_BACKGROUND 2
#define GCE_DISPOSAL_RESTORE    3

typedef struct GifState {
    int screen_width;
    int screen_height;
    int bits_per_pixel;
    int background_color_index;
    int transparent_color_index;
    int color_resolution;
    int image_count;
    uint8_t *image_buf;
    /* after the frame is displayed, the disposal method is used */
    int gce_disposal;
    /* delay during which the frame is shown */
    int gce_delay;
    
    /* LZW compatible decoder */
    ByteIOContext *f;
    int eob_reached;
    uint8_t *pbuf, *ebuf;
    int bbits;
    unsigned int bbuf;

    int cursize;		/* The current code size */
    int curmask;
    int codesize;
    int clear_code;
    int end_code;
    int newcodes;		/* First available code */
    int top_slot;		/* Highest code for current size */
    int slot;			/* Last read code */
    int fc, oc;
    uint8_t *sp;
    uint8_t stack[SIZTABLE];
    uint8_t suffix[SIZTABLE];
    uint16_t prefix[SIZTABLE];

    /* aux buffers */
    uint8_t global_palette[256 * 3];
    uint8_t local_palette[256 * 3];
    uint8_t buf[256];
} GifState;


static const uint8_t gif87a_sig[6] = "GIF87a";
static const uint8_t gif89a_sig[6] = "GIF89a";

static const uint16_t mask[17] =
{
    0x0000, 0x0001, 0x0003, 0x0007,
    0x000F, 0x001F, 0x003F, 0x007F,
    0x00FF, 0x01FF, 0x03FF, 0x07FF,
    0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF
};

static int gif_probe(AVProbeData * pd)
{
    if (pd->buf_size >= 24 &&
	(memcmp(pd->buf, gif87a_sig, 6) == 0 ||
	 memcmp(pd->buf, gif89a_sig, 6) == 0))
	return AVPROBE_SCORE_MAX;
    else
	return 0;
}

static void GLZWDecodeInit(GifState * s, int csize)
{
    /* read buffer */
    s->eob_reached = 0;
    s->pbuf = s->buf;
    s->ebuf = s->buf;
    s->bbuf = 0;
    s->bbits = 0;

    /* decoder */
    s->codesize = csize;
    s->cursize = s->codesize + 1;
    s->curmask = mask[s->cursize];
    s->top_slot = 1 << s->cursize;
    s->clear_code = 1 << s->codesize;
    s->end_code = s->clear_code + 1;
    s->slot = s->newcodes = s->clear_code + 2;
    s->oc = s->fc = 0;
    s->sp = s->stack;
}

/* XXX: optimize */
static inline int GetCode(GifState * s)
{
    int c, sizbuf;
    uint8_t *ptr;

    while (s->bbits < s->cursize) {
        ptr = s->pbuf;
        if (ptr >= s->ebuf) {
            if (!s->eob_reached) {
                sizbuf = get_byte(s->f);
                s->ebuf = s->buf + sizbuf;
                s->pbuf = s->buf;
                if (sizbuf > 0) {
                    get_buffer(s->f, s->buf, sizbuf);
                } else {
                    s->eob_reached = 1;
                }
            }
            ptr = s->pbuf;
        }
        s->bbuf |= ptr[0] << s->bbits;
        ptr++;
        s->pbuf = ptr;
	s->bbits += 8;
    }
    c = s->bbuf & s->curmask;
    s->bbuf >>= s->cursize;
    s->bbits -= s->cursize;
    return c;
}

/* NOTE: the algorithm here is inspired from the LZW GIF decoder
   written by Steven A. Bennett in 1987. */
/* return the number of byte decoded */
static int GLZWDecode(GifState * s, uint8_t * buf, int len)
{
    int l, c, code, oc, fc;
    uint8_t *sp;

    if (s->end_code < 0)
        return 0;

    l = len;
    sp = s->sp;
    oc = s->oc;
    fc = s->fc;

    while (sp > s->stack) {
	*buf++ = *(--sp);
	if ((--l) == 0)
	    goto the_end;
    }

    for (;;) {
	c = GetCode(s);
	if (c == s->end_code) {
	    s->end_code = -1;
	    break;
	} else if (c == s->clear_code) {
	    s->cursize = s->codesize + 1;
	    s->curmask = mask[s->cursize];
	    s->slot = s->newcodes;
	    s->top_slot = 1 << s->cursize;
	    while ((c = GetCode(s)) == s->clear_code);
	    if (c == s->end_code) {
		s->end_code = -1;
		break;
	    }
	    /* test error */
	    if (c >= s->slot)
		c = 0;
	    fc = oc = c;
	    *buf++ = c;
	    if ((--l) == 0)
		break;
	} else {
	    code = c;
	    if (code >= s->slot) {
		*sp++ = fc;
		code = oc;
	    }
	    while (code >= s->newcodes) {
		*sp++ = s->suffix[code];
		code = s->prefix[code];
	    }
	    *sp++ = code;
	    if (s->slot < s->top_slot) {
		s->suffix[s->slot] = fc = code;
		s->prefix[s->slot++] = oc;
		oc = c;
	    }
	    if (s->slot >= s->top_slot) {
		if (s->cursize < MAXBITS) {
		    s->top_slot <<= 1;
		    s->curmask = mask[++s->cursize];
		}
	    }
	    while (sp > s->stack) {
		*buf++ = *(--sp);
		if ((--l) == 0)
                    goto the_end;
	    }
	}
    }
  the_end:
    s->sp = sp;
    s->oc = oc;
    s->fc = fc;
    return len - l;
}

static int gif_read_image(AVFormatContext * s1, AVPacket * pkt)
{
    GifState *s = s1->priv_data;
    int left, top, width, height, bits_per_pixel, code_size, flags;
    int is_interleaved, has_local_palette, y, x, linesize;
    ByteIOContext *f = &s1->pb;
    uint8_t *ptr, *line, *d, *spal, *palette, *sptr;

    left = get_le16(f);
    top = get_le16(f);
    width = get_le16(f);
    height = get_le16(f);
    flags = get_byte(f);
    is_interleaved = flags & 0x40;
    has_local_palette = flags & 0x80;
    bits_per_pixel = (flags & 0x07) + 1;
#ifdef DEBUG
    printf("gif: image x=%d y=%d w=%d h=%d\n", left, top, width, height);
#endif

    if (has_local_palette) {
	get_buffer(f, s->local_palette, 3 * (1 << bits_per_pixel));
        palette = s->local_palette;
    } else {
        palette = s->global_palette;
    }
    
    /* allocate local image (XXX: horrible, needs API change) */
    if (s->image_count == 0) {
        /* modify screen width/height if invalid */
        if (left + width > s->screen_width)
            s->screen_width = left + width;
        if (top + height > s->screen_height)
            s->screen_width = top + height;
        s->image_buf = av_malloc(s->screen_width * s->screen_height * 3);
        if (!s->image_buf)
            return -ENOMEM;
    } else {
        /* verify that all the image is inside the screen dimensions */
        if (left + width > s->screen_width ||
            top + height > s->screen_height)
            return -EINVAL;
    }
    s->image_count++;

    line = av_malloc(width);
    if (!line)
        return -ENOMEM;

    /* now get the image data */
    s->f = f;
    code_size = get_byte(f);
    GLZWDecodeInit(s, code_size);

    /* read all the image and transcode it to RGB24 (horrible) */
    linesize = s->screen_width * 3;
    ptr = s->image_buf + top * linesize + (left * 3);
    for (y = 0; y < height; y++) {
	GLZWDecode(s, line, width);
        d = ptr;
        sptr = line;
        for(x = 0; x < width; x++) {
            spal = palette + sptr[0] * 3;
            d[0] = spal[0];
            d[1] = spal[1];
            d[2] = spal[2];
            d += 3;
            sptr++;
        }
	ptr += linesize;
    }
    av_free(line);
    
    /* read the garbage data until end marker is found */
    while (!s->eob_reached)
        GetCode(s);

    /* output image */
    /* XXX: avoid copying */
    if (av_new_packet(pkt, s->screen_width * s->screen_height * 3)) {
        av_free(line);
	return -EIO;
    }
    pkt->stream_index = 0;
    memcpy(pkt->data, s->image_buf, s->screen_width * s->screen_height * 3);
    return 0;
}

static int gif_read_extension(AVFormatContext * s1)
{
    GifState *s = s1->priv_data;
    ByteIOContext *f = &s1->pb;
    int ext_code, ext_len, i, gce_flags, gce_transparent_index;

    /* extension */
    ext_code = get_byte(f);
    ext_len = get_byte(f);
#ifdef DEBUG
    printf("gif: ext_code=0x%x len=%d\n", ext_code, ext_len);
#endif
    switch(ext_code) {
    case 0xf9:
        if (ext_len != 4)
            goto discard_ext;
        s->transparent_color_index = -1;
        gce_flags = get_byte(f);
        s->gce_delay = get_le16(f);
        gce_transparent_index = get_byte(f);
        if (gce_flags & 0x01)
            s->transparent_color_index = gce_transparent_index;
        else
            s->transparent_color_index = -1;
        s->gce_disposal = (gce_flags >> 2) & 0x7;
#ifdef DEBUG
        printf("gif: gce_flags=%x delay=%d tcolor=%d disposal=%d\n", 
               gce_flags, s->gce_delay, 
               s->transparent_color_index, s->gce_disposal);
#endif
        ext_len = get_byte(f);
        break;
    }
        
    /* NOTE: many extension blocks can come after */
 discard_ext:
    while (ext_len != 0) {
        for (i = 0; i < ext_len; i++)
            get_byte(f);
        ext_len = get_byte(f);
#ifdef DEBUG
        printf("gif: ext_len1=%d\n", ext_len);
#endif
    }
    return 0;
}

static int gif_read_header(AVFormatContext * s1,
			   AVFormatParameters * ap)
{
    GifState *s = s1->priv_data;
    ByteIOContext *f = &s1->pb;
    AVStream *st;
    uint8_t sig[6];
    int ret, v, n;
    int has_global_palette;

    /* read gif signature */
    ret = get_buffer(f, sig, 6);
    if (ret != 6)
	return -1;
    if (memcmp(sig, gif87a_sig, 6) != 0 &&
	memcmp(sig, gif89a_sig, 6) != 0)
	return -1;

    /* read screen header */
    s->transparent_color_index = -1;
    s->screen_width = get_le16(f);
    s->screen_height = get_le16(f);
    v = get_byte(f);
    s->color_resolution = ((v & 0x70) >> 4) + 1;
    has_global_palette = (v & 0x80);
    s->bits_per_pixel = (v & 0x07) + 1;
    s->background_color_index = get_byte(f);
    get_byte(f);		/* ignored */
#ifdef DEBUG
    printf("gif: screen_w=%d screen_h=%d bpp=%d global_palette=%d\n",
	   s->screen_width, s->screen_height, s->bits_per_pixel,
	   has_global_palette);
#endif
    if (has_global_palette) {
	n = 1 << s->bits_per_pixel;
	get_buffer(f, s->global_palette, n * 3);
    }
    /* now we are ready: build format streams */
    st = av_new_stream(s1, 0);
    if (!st)
	return -1;

    st->codec.codec_type = CODEC_TYPE_VIDEO;
    st->codec.codec_id = CODEC_ID_RAWVIDEO;
    st->codec.frame_rate = 5 * FRAME_RATE_BASE;
    /* XXX: check if screen size is always valid */
    st->codec.width = s->screen_width;
    st->codec.height = s->screen_height;
    st->codec.pix_fmt = PIX_FMT_RGB24;
    return 0;
}

static int gif_read_packet(AVFormatContext * s1,
			   AVPacket * pkt)
{
    ByteIOContext *f = &s1->pb;
    int ret, code;

    for (;;) {
	code = url_fgetc(f);
#ifdef DEBUG
	printf("gif: code=%02x '%c'\n", code, code);
#endif
	switch (code) {
	case ',':
	    if (gif_read_image(s1, pkt) < 0)
		return -EIO;
	    ret = 0;
	    goto the_end;
	case ';':
	    /* end of image */
	    ret = -EIO;
	    goto the_end;
	case '!':
            if (gif_read_extension(s1) < 0)
                return -EIO;
	    break;
	case EOF:
	default:
	    /* error or errneous EOF */
	    ret = -EIO;
	    goto the_end;
	}
    }
  the_end:
    return ret;
}

static int gif_read_close(AVFormatContext *s1)
{
    GifState *s = s1->priv_data;
    av_free(s->image_buf);
    return 0;
}

AVInputFormat gif_iformat =
{
    "gif",
    "gif format",
    sizeof(GifState),
    gif_probe,
    gif_read_header,
    gif_read_packet,
    gif_read_close,
};
