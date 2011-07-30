/*
 * GIF decoder
 * Copyright (c) 2003 Fabrice Bellard
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

//#define DEBUG

#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "lzw.h"

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
    const uint8_t *bytestream;
    const uint8_t *bytestream_end;
    LZWState *lzw;

    /* aux buffers */
    uint8_t global_palette[256 * 3];
    uint8_t local_palette[256 * 3];

  AVCodecContext* avctx;
} GifState;

static const uint8_t gif87a_sig[6] = "GIF87a";
static const uint8_t gif89a_sig[6] = "GIF89a";

static int gif_read_image(GifState *s)
{
    int left, top, width, height, bits_per_pixel, code_size, flags;
    int is_interleaved, has_local_palette, y, pass, y1, linesize, n, i;
    uint8_t *ptr, *spal, *palette, *ptr1;

    left = bytestream_get_le16(&s->bytestream);
    top = bytestream_get_le16(&s->bytestream);
    width = bytestream_get_le16(&s->bytestream);
    height = bytestream_get_le16(&s->bytestream);
    flags = bytestream_get_byte(&s->bytestream);
    is_interleaved = flags & 0x40;
    has_local_palette = flags & 0x80;
    bits_per_pixel = (flags & 0x07) + 1;

    av_dlog(s->avctx, "gif: image x=%d y=%d w=%d h=%d\n", left, top, width, height);

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
        return AVERROR(EINVAL);

    /* build the palette */
    n = (1 << bits_per_pixel);
    spal = palette;
    for(i = 0; i < n; i++) {
        s->image_palette[i] = (0xff << 24) | AV_RB24(spal);
        spal += 3;
    }
    for(; i < 256; i++)
        s->image_palette[i] = (0xff << 24);
    /* handle transparency */
    if (s->transparent_color_index >= 0)
        s->image_palette[s->transparent_color_index] = 0;

    /* now get the image data */
    code_size = bytestream_get_byte(&s->bytestream);
    ff_lzw_decode_init(s->lzw, code_size, s->bytestream,
                       s->bytestream_end - s->bytestream, FF_LZW_GIF);

    /* read all the image */
    linesize = s->picture.linesize[0];
    ptr1 = s->picture.data[0] + top * linesize + left;
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
    s->bytestream = ff_lzw_cur_ptr(s->lzw);
    return 0;
}

static int gif_read_extension(GifState *s)
{
    int ext_code, ext_len, i, gce_flags, gce_transparent_index;

    /* extension */
    ext_code = bytestream_get_byte(&s->bytestream);
    ext_len = bytestream_get_byte(&s->bytestream);

    av_dlog(s->avctx, "gif: ext_code=0x%x len=%d\n", ext_code, ext_len);

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

        av_dlog(s->avctx, "gif: gce_flags=%x delay=%d tcolor=%d disposal=%d\n",
               gce_flags, s->gce_delay,
               s->transparent_color_index, s->gce_disposal);

        ext_len = bytestream_get_byte(&s->bytestream);
        break;
    }

    /* NOTE: many extension blocks can come after */
 discard_ext:
    while (ext_len != 0) {
        for (i = 0; i < ext_len; i++)
            bytestream_get_byte(&s->bytestream);
        ext_len = bytestream_get_byte(&s->bytestream);

        av_dlog(s->avctx, "gif: ext_len1=%d\n", ext_len);
    }
    return 0;
}

static int gif_read_header1(GifState *s)
{
    uint8_t sig[6];
    int v, n;
    int has_global_palette;

    if (s->bytestream_end < s->bytestream + 13)
        return -1;

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

    av_dlog(s->avctx, "gif: screen_w=%d screen_h=%d bpp=%d global_palette=%d\n",
           s->screen_width, s->screen_height, s->bits_per_pixel,
           has_global_palette);

    if (has_global_palette) {
        n = 1 << s->bits_per_pixel;
        if (s->bytestream_end < s->bytestream + n * 3)
            return -1;
        bytestream_get_buffer(&s->bytestream, s->global_palette, n * 3);
    }
    return 0;
}

static int gif_parse_next_image(GifState *s)
{
    while (s->bytestream < s->bytestream_end) {
        int code = bytestream_get_byte(&s->bytestream);

        av_dlog(s->avctx, "gif: code=%02x '%c'\n", code, code);

        switch (code) {
        case ',':
            return gif_read_image(s);
        case '!':
            if (gif_read_extension(s) < 0)
                return -1;
            break;
        case ';':
            /* end of image */
        default:
            /* error or erroneous EOF */
            return -1;
        }
    }
    return -1;
}

static av_cold int gif_decode_init(AVCodecContext *avctx)
{
    GifState *s = avctx->priv_data;

    s->avctx = avctx;

    avcodec_get_frame_defaults(&s->picture);
    avctx->coded_frame= &s->picture;
    s->picture.data[0] = NULL;
    ff_lzw_decode_open(&s->lzw);
    return 0;
}

static int gif_decode_frame(AVCodecContext *avctx, void *data, int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    GifState *s = avctx->priv_data;
    AVFrame *picture = data;
    int ret;

    s->bytestream = buf;
    s->bytestream_end = buf + buf_size;
    if (gif_read_header1(s) < 0)
        return -1;

    avctx->pix_fmt = PIX_FMT_PAL8;
    if (av_image_check_size(s->screen_width, s->screen_height, 0, avctx))
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
    return s->bytestream - buf;
}

static av_cold int gif_decode_close(AVCodecContext *avctx)
{
    GifState *s = avctx->priv_data;

    ff_lzw_decode_close(&s->lzw);
    if(s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);
    return 0;
}

AVCodec ff_gif_decoder = {
    .name           = "gif",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_GIF,
    .priv_data_size = sizeof(GifState),
    .init           = gif_decode_init,
    .close          = gif_decode_close,
    .decode         = gif_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("GIF (Graphics Interchange Format)"),
};
