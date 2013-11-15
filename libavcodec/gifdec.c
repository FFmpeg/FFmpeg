/*
 * GIF decoder
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Baptiste Coudurier
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "lzw.h"

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
    uint32_t *image_palette;

    /* after the frame is displayed, the disposal method is used */
    int gce_disposal;
    /* delay during which the frame is shown */
    int gce_delay;

    /* LZW compatible decoder */
    GetByteContext gb;
    LZWState *lzw;

    /* aux buffers */
    uint8_t global_palette[256 * 3];
    uint8_t local_palette[256 * 3];

  AVCodecContext* avctx;
} GifState;

static const uint8_t gif87a_sig[6] = "GIF87a";
static const uint8_t gif89a_sig[6] = "GIF89a";

static int gif_read_image(GifState *s, AVFrame *frame)
{
    int left, top, width, height, bits_per_pixel, code_size, flags;
    int is_interleaved, has_local_palette, y, pass, y1, linesize, n, i;
    uint8_t *ptr, *spal, *palette, *ptr1;

    left   = bytestream2_get_le16(&s->gb);
    top    = bytestream2_get_le16(&s->gb);
    width  = bytestream2_get_le16(&s->gb);
    height = bytestream2_get_le16(&s->gb);
    flags  = bytestream2_get_byte(&s->gb);
    is_interleaved = flags & 0x40;
    has_local_palette = flags & 0x80;
    bits_per_pixel = (flags & 0x07) + 1;

    av_dlog(s->avctx, "gif: image x=%d y=%d w=%d h=%d\n", left, top, width, height);

    if (has_local_palette) {
        bytestream2_get_buffer(&s->gb, s->local_palette, 3 * (1 << bits_per_pixel));
        palette = s->local_palette;
    } else {
        palette = s->global_palette;
        bits_per_pixel = s->bits_per_pixel;
    }

    /* verify that all the image is inside the screen dimensions */
    if (left + width > s->screen_width ||
        top + height > s->screen_height ||
        !width || !height) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid image dimensions.\n");
        return AVERROR_INVALIDDATA;
    }

    /* build the palette */
    n = (1 << bits_per_pixel);
    spal = palette;
    for(i = 0; i < n; i++) {
        s->image_palette[i] = (0xffu << 24) | AV_RB24(spal);
        spal += 3;
    }
    for(; i < 256; i++)
        s->image_palette[i] = (0xffu << 24);
    /* handle transparency */
    if (s->transparent_color_index >= 0)
        s->image_palette[s->transparent_color_index] = 0;

    /* now get the image data */
    code_size = bytestream2_get_byte(&s->gb);
    ff_lzw_decode_init(s->lzw, code_size, s->gb.buffer,
                       bytestream2_get_bytes_left(&s->gb), FF_LZW_GIF);

    /* read all the image */
    linesize = frame->linesize[0];
    ptr1 = frame->data[0] + top * linesize + left;
    ptr = ptr1;
    pass = 0;
    y1 = 0;
    for (y = 0; y < height; y++) {
        ff_lzw_decode(s->lzw, ptr, width);
        if (is_interleaved) {
            switch(pass) {
            default:
            case 0:
            case 1:
                y1 += 8;
                ptr += linesize * 8;
                if (y1 >= height) {
                    y1 = pass ? 2 : 4;
                    ptr = ptr1 + linesize * y1;
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
    /* read the garbage data until end marker is found */
    ff_lzw_decode_tail(s->lzw);

    bytestream2_skip(&s->gb, ff_lzw_size_read(s->lzw));
    return 0;
}

static int gif_read_extension(GifState *s)
{
    int ext_code, ext_len, i, gce_flags, gce_transparent_index;

    /* extension */
    ext_code = bytestream2_get_byte(&s->gb);
    ext_len  = bytestream2_get_byte(&s->gb);

    av_dlog(s->avctx, "gif: ext_code=0x%x len=%d\n", ext_code, ext_len);

    switch(ext_code) {
    case 0xf9:
        if (ext_len != 4)
            goto discard_ext;
        s->transparent_color_index = -1;
        gce_flags    = bytestream2_get_byte(&s->gb);
        s->gce_delay = bytestream2_get_le16(&s->gb);
        gce_transparent_index = bytestream2_get_byte(&s->gb);
        if (gce_flags & 0x01)
            s->transparent_color_index = gce_transparent_index;
        else
            s->transparent_color_index = -1;
        s->gce_disposal = (gce_flags >> 2) & 0x7;

        av_dlog(s->avctx, "gif: gce_flags=%x delay=%d tcolor=%d disposal=%d\n",
               gce_flags, s->gce_delay,
               s->transparent_color_index, s->gce_disposal);

        ext_len = bytestream2_get_byte(&s->gb);
        break;
    }

    /* NOTE: many extension blocks can come after */
 discard_ext:
    while (ext_len != 0) {
        for (i = 0; i < ext_len; i++)
            bytestream2_get_byte(&s->gb);
        ext_len = bytestream2_get_byte(&s->gb);

        av_dlog(s->avctx, "gif: ext_len1=%d\n", ext_len);
    }
    return 0;
}

static int gif_read_header1(GifState *s)
{
    uint8_t sig[6];
    int v, n;
    int has_global_palette;

    if (bytestream2_get_bytes_left(&s->gb) < 13)
        return AVERROR_INVALIDDATA;

    /* read gif signature */
    bytestream2_get_buffer(&s->gb, sig, 6);
    if (memcmp(sig, gif87a_sig, 6) != 0 &&
        memcmp(sig, gif89a_sig, 6) != 0)
        return AVERROR_INVALIDDATA;

    /* read screen header */
    s->transparent_color_index = -1;
    s->screen_width  = bytestream2_get_le16(&s->gb);
    s->screen_height = bytestream2_get_le16(&s->gb);
    if(   (unsigned)s->screen_width  > 32767
       || (unsigned)s->screen_height > 32767){
        av_log(NULL, AV_LOG_ERROR, "picture size too large\n");
        return AVERROR_INVALIDDATA;
    }

    v = bytestream2_get_byte(&s->gb);
    s->color_resolution = ((v & 0x70) >> 4) + 1;
    has_global_palette = (v & 0x80);
    s->bits_per_pixel = (v & 0x07) + 1;
    s->background_color_index = bytestream2_get_byte(&s->gb);
    bytestream2_get_byte(&s->gb);                /* ignored */

    av_dlog(s->avctx, "gif: screen_w=%d screen_h=%d bpp=%d global_palette=%d\n",
           s->screen_width, s->screen_height, s->bits_per_pixel,
           has_global_palette);

    if (has_global_palette) {
        n = 1 << s->bits_per_pixel;
        if (bytestream2_get_bytes_left(&s->gb) < n * 3)
            return AVERROR_INVALIDDATA;
        bytestream2_get_buffer(&s->gb, s->global_palette, n * 3);
    }
    return 0;
}

static int gif_parse_next_image(GifState *s, AVFrame *frame)
{
    while (bytestream2_get_bytes_left(&s->gb) > 0) {
        int code = bytestream2_get_byte(&s->gb);
        int ret;

        av_dlog(s->avctx, "gif: code=%02x '%c'\n", code, code);

        switch (code) {
        case ',':
            return gif_read_image(s, frame);
        case '!':
            if ((ret = gif_read_extension(s)) < 0)
                return ret;
            break;
        case ';':
            /* end of image */
        default:
            /* error or erroneous EOF */
            return AVERROR_INVALIDDATA;
        }
    }
    return AVERROR_INVALIDDATA;
}

static av_cold int gif_decode_init(AVCodecContext *avctx)
{
    GifState *s = avctx->priv_data;

    s->avctx = avctx;

    ff_lzw_decode_open(&s->lzw);
    return 0;
}

static int gif_decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                            AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    GifState *s = avctx->priv_data;
    AVFrame *picture = data;
    int ret;

    bytestream2_init(&s->gb, buf, buf_size);
    if ((ret = gif_read_header1(s)) < 0)
        return ret;

    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    if ((ret = ff_set_dimensions(avctx, s->screen_width, s->screen_height)) < 0)
        return ret;

    if ((ret = ff_get_buffer(avctx, picture, 0)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    s->image_palette = (uint32_t *)picture->data[1];
    ret = gif_parse_next_image(s, picture);
    if (ret < 0)
        return ret;

    *got_frame = 1;
    return bytestream2_tell(&s->gb);
}

static av_cold int gif_decode_close(AVCodecContext *avctx)
{
    GifState *s = avctx->priv_data;

    ff_lzw_decode_close(&s->lzw);
    return 0;
}

AVCodec ff_gif_decoder = {
    .name           = "gif",
    .long_name      = NULL_IF_CONFIG_SMALL("GIF (Graphics Interchange Format)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_GIF,
    .priv_data_size = sizeof(GifState),
    .init           = gif_decode_init,
    .close          = gif_decode_close,
    .decode         = gif_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
