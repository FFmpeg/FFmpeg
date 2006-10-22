/*
 * GIF decoder
 * Copyright (c) 2003 Fabrice Bellard.
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

//#define DEBUG

#include "avcodec.h"
#include "bytestream.h"

#define MAXBITS                 12
#define SIZTABLE                (1<<MAXBITS)

#define GCE_DISPOSAL_NONE       0
#define GCE_DISPOSAL_INPLACE    1
#define GCE_DISPOSAL_BACKGROUND 2
#define GCE_DISPOSAL_RESTORE    3

typedef struct GifState {
    AVFrame picture;
    int screen_width;
    int screen_height;
    int bits_per_pixel;
    int background_color_index;
    int transparent_color_index;
    int color_resolution;
    uint32_t *image_palette;

    /* after the frame is displayed, the disposal method is used */
    int gce_disposal;
    /* delay during which the frame is shown */
    int gce_delay;

    /* LZW compatible decoder */
    uint8_t *bytestream;
    int eob_reached;
    uint8_t *pbuf, *ebuf;
    int bbits;
    unsigned int bbuf;

    int cursize;                /* The current code size */
    int curmask;
    int codesize;
    int clear_code;
    int end_code;
    int newcodes;               /* First available code */
    int top_slot;               /* Highest code for current size */
    int slot;                   /* Last read code */
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
                sizbuf = bytestream_get_byte(&s->bytestream);
                s->ebuf = s->buf + sizbuf;
                s->pbuf = s->buf;
                if (sizbuf > 0) {
                    bytestream_get_buffer(&s->bytestream, s->buf, sizbuf);
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

static int gif_read_image(GifState *s)
{
    int left, top, width, height, bits_per_pixel, code_size, flags;
    int is_interleaved, has_local_palette, y, pass, y1, linesize, n, i;
    uint8_t *ptr, *line, *spal, *palette, *ptr1;

    left = bytestream_get_le16(&s->bytestream);
    top = bytestream_get_le16(&s->bytestream);
    width = bytestream_get_le16(&s->bytestream);
    height = bytestream_get_le16(&s->bytestream);
    flags = bytestream_get_byte(&s->bytestream);
    is_interleaved = flags & 0x40;
    has_local_palette = flags & 0x80;
    bits_per_pixel = (flags & 0x07) + 1;
#ifdef DEBUG
    dprintf("gif: image x=%d y=%d w=%d h=%d\n", left, top, width, height);
#endif

    if (has_local_palette) {
        bytestream_get_buffer(&s->bytestream, s->local_palette, 3 * (1 << bits_per_pixel));
        palette = s->local_palette;
    } else {
        palette = s->global_palette;
        bits_per_pixel = s->bits_per_pixel;
    }

    /* verify that all the image is inside the screen dimensions */
    if (left + width > s->screen_width ||
        top + height > s->screen_height)
        return -EINVAL;

    /* build the palette */
    n = (1 << bits_per_pixel);
    spal = palette;
    for(i = 0; i < n; i++) {
        s->image_palette[i] = (0xff << 24) |
            (spal[0] << 16) | (spal[1] << 8) | (spal[2]);
        spal += 3;
    }
    for(; i < 256; i++)
        s->image_palette[i] = (0xff << 24);
    /* handle transparency */
    if (s->transparent_color_index >= 0)
        s->image_palette[s->transparent_color_index] = 0;
    line = NULL;

    /* now get the image data */
    code_size = bytestream_get_byte(&s->bytestream);
    GLZWDecodeInit(s, code_size);

    /* read all the image */
    linesize = s->picture.linesize[0];
    ptr1 = s->picture.data[0] + top * linesize + (left * 3);
    ptr = ptr1;
    pass = 0;
    y1 = 0;
    for (y = 0; y < height; y++) {
        GLZWDecode(s, ptr, width);
        if (is_interleaved) {
            switch(pass) {
            default:
            case 0:
            case 1:
                y1 += 8;
                ptr += linesize * 8;
                if (y1 >= height) {
                    y1 = 4;
                    if (pass == 0)
                        ptr = ptr1 + linesize * 4;
                    else
                        ptr = ptr1 + linesize * 2;
                    pass++;
                }
                break;
            case 2:
                y1 += 4;
                ptr += linesize * 4;
                if (y1 >= height) {
                    y1 = 1;
                    ptr = ptr1 + linesize;
                    pass++;
                }
                break;
            case 3:
                y1 += 2;
                ptr += linesize * 2;
                break;
            }
        } else {
            ptr += linesize;
        }
    }
    av_free(line);

    /* read the garbage data until end marker is found */
    while (!s->eob_reached)
        GetCode(s);
    return 0;
}

static int gif_read_extension(GifState *s)
{
    int ext_code, ext_len, i, gce_flags, gce_transparent_index;

    /* extension */
    ext_code = bytestream_get_byte(&s->bytestream);
    ext_len = bytestream_get_byte(&s->bytestream);
#ifdef DEBUG
    dprintf("gif: ext_code=0x%x len=%d\n", ext_code, ext_len);
#endif
    switch(ext_code) {
    case 0xf9:
        if (ext_len != 4)
            goto discard_ext;
        s->transparent_color_index = -1;
        gce_flags = bytestream_get_byte(&s->bytestream);
        s->gce_delay = bytestream_get_le16(&s->bytestream);
        gce_transparent_index = bytestream_get_byte(&s->bytestream);
        if (gce_flags & 0x01)
            s->transparent_color_index = gce_transparent_index;
        else
            s->transparent_color_index = -1;
        s->gce_disposal = (gce_flags >> 2) & 0x7;
#ifdef DEBUG
        dprintf("gif: gce_flags=%x delay=%d tcolor=%d disposal=%d\n",
               gce_flags, s->gce_delay,
               s->transparent_color_index, s->gce_disposal);
#endif
        ext_len = bytestream_get_byte(&s->bytestream);
        break;
    }

    /* NOTE: many extension blocks can come after */
 discard_ext:
    while (ext_len != 0) {
        for (i = 0; i < ext_len; i++)
            bytestream_get_byte(&s->bytestream);
        ext_len = bytestream_get_byte(&s->bytestream);
#ifdef DEBUG
        dprintf("gif: ext_len1=%d\n", ext_len);
#endif
    }
    return 0;
}

static int gif_read_header1(GifState *s)
{
    uint8_t sig[6];
    int v, n;
    int has_global_palette;

    /* read gif signature */
    bytestream_get_buffer(&s->bytestream, sig, 6);
    if (memcmp(sig, gif87a_sig, 6) != 0 &&
        memcmp(sig, gif89a_sig, 6) != 0)
        return -1;

    /* read screen header */
    s->transparent_color_index = -1;
    s->screen_width = bytestream_get_le16(&s->bytestream);
    s->screen_height = bytestream_get_le16(&s->bytestream);
    if(   (unsigned)s->screen_width  > 32767
       || (unsigned)s->screen_height > 32767){
        av_log(NULL, AV_LOG_ERROR, "picture size too large\n");
        return -1;
    }

    v = bytestream_get_byte(&s->bytestream);
    s->color_resolution = ((v & 0x70) >> 4) + 1;
    has_global_palette = (v & 0x80);
    s->bits_per_pixel = (v & 0x07) + 1;
    s->background_color_index = bytestream_get_byte(&s->bytestream);
    bytestream_get_byte(&s->bytestream);                /* ignored */
#ifdef DEBUG
    dprintf("gif: screen_w=%d screen_h=%d bpp=%d global_palette=%d\n",
           s->screen_width, s->screen_height, s->bits_per_pixel,
           has_global_palette);
#endif
    if (has_global_palette) {
        n = 1 << s->bits_per_pixel;
        bytestream_get_buffer(&s->bytestream, s->global_palette, n * 3);
    }
    return 0;
}

static int gif_parse_next_image(GifState *s)
{
    int ret, code;

    for (;;) {
        code = bytestream_get_byte(&s->bytestream);
#ifdef DEBUG
        dprintf("gif: code=%02x '%c'\n", code, code);
#endif
        switch (code) {
        case ',':
            if (gif_read_image(s) < 0)
                return -1;
            ret = 0;
            goto the_end;
        case ';':
            /* end of image */
            ret = -1;
            goto the_end;
        case '!':
            if (gif_read_extension(s) < 0)
                return -1;
            break;
        case EOF:
        default:
            /* error or errneous EOF */
            ret = -1;
            goto the_end;
        }
    }
  the_end:
    return ret;
}

static int gif_decode_init(AVCodecContext *avctx)
{
    GifState *s = avctx->priv_data;

    avcodec_get_frame_defaults(&s->picture);
    avctx->coded_frame= &s->picture;
    s->picture.data[0] = NULL;
    return 0;
}

static int gif_decode_frame(AVCodecContext *avctx, void *data, int *data_size, uint8_t *buf, int buf_size)
{
    GifState *s = avctx->priv_data;
    AVFrame *picture = data;
    int ret;

    s->bytestream = buf;
    if (gif_read_header1(s) < 0)
        return -1;

    avctx->pix_fmt = PIX_FMT_PAL8;
    if (avcodec_check_dimensions(avctx, s->screen_width, s->screen_height))
        return -1;
    avcodec_set_dimensions(avctx, s->screen_width, s->screen_height);

    if (s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);
    if (avctx->get_buffer(avctx, &s->picture) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    s->image_palette = (uint32_t *)s->picture.data[1];
    ret = gif_parse_next_image(s);
    if (ret < 0)
        return ret;

    *picture = s->picture;
    *data_size = sizeof(AVPicture);
    return 0;
}

static int gif_decode_close(AVCodecContext *avctx)
{
    GifState *s = avctx->priv_data;

    if(s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);
    return 0;
}

AVCodec gif_decoder = {
    "gif",
    CODEC_TYPE_VIDEO,
    CODEC_ID_GIF,
    sizeof(GifState),
    gif_decode_init,
    NULL,
    gif_decode_close,
    gif_decode_frame,
};
