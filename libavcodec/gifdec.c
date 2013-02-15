/*
 * GIF decoder
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Baptiste Coudurier
 * Copyright (c) 2012 Vitaliy E Sugrobov
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
#include "libavutil/opt.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "lzw.h"
#include "gif.h"

/* This value is intentionally set to "transparent white" color.
 * It is much better to have white background instead of black
 * when gif image converted to format which not support transparency.
 */
#define GIF_TRANSPARENT_COLOR    0x00ffffff

typedef struct GifState {
    const AVClass *class;
    AVFrame picture;
    int screen_width;
    int screen_height;
    int has_global_palette;
    int bits_per_pixel;
    uint32_t bg_color;
    int background_color_index;
    int transparent_color_index;
    int color_resolution;
    /* intermediate buffer for storing color indices
     * obtained from lzw-encoded data stream */
    uint8_t *idx_line;
    int idx_line_size;

    /* after the frame is displayed, the disposal method is used */
    int gce_prev_disposal;
    int gce_disposal;
    /* rectangle describing area that must be disposed */
    int gce_l, gce_t, gce_w, gce_h;
    /* depending on disposal method we store either part of the image
     * drawn on the canvas or background color that
     * should be used upon disposal */
    uint32_t * stored_img;
    int stored_img_size;
    int stored_bg_color;

    GetByteContext gb;
    /* LZW compatible decoder */
    LZWState *lzw;

    /* aux buffers */
    uint32_t global_palette[256];
    uint32_t local_palette[256];

    AVCodecContext *avctx;
    int keyframe;
    int keyframe_ok;
    int trans_color;    /**< color value that is used instead of transparent color */
} GifState;

static void gif_read_palette(GifState *s, uint32_t *pal, int nb)
{
    int i;

    for (i = 0; i < nb; i++, pal++)
        *pal = (0xffu << 24) | bytestream2_get_be24u(&s->gb);
}

static void gif_fill(AVFrame *picture, uint32_t color)
{
    uint32_t *p = (uint32_t *)picture->data[0];
    uint32_t *p_end = p + (picture->linesize[0] / sizeof(uint32_t)) * picture->height;

    for (; p < p_end; p++)
        *p = color;
}

static void gif_fill_rect(AVFrame *picture, uint32_t color, int l, int t, int w, int h)
{
    const int linesize = picture->linesize[0] / sizeof(uint32_t);
    const uint32_t *py = (uint32_t *)picture->data[0] + t * linesize;
    const uint32_t *pr, *pb = py + h * linesize;
    uint32_t *px;

    for (; py < pb; py += linesize) {
        px = (uint32_t *)py + l;
        pr = px + w;

        for (; px < pr; px++)
            *px = color;
    }
}

static void gif_copy_img_rect(const uint32_t *src, uint32_t *dst,
                              int linesize, int l, int t, int w, int h)
{
    const int y_start = t * linesize;
    const uint32_t *src_px,
                   *src_py = src + y_start,
                   *dst_py = dst + y_start;
    const uint32_t *src_pb = src_py + h * linesize;
    uint32_t *dst_px;

    for (; src_py < src_pb; src_py += linesize, dst_py += linesize) {
        src_px = src_py + l;
        dst_px = (uint32_t *)dst_py + l;

        memcpy(dst_px, src_px, w * sizeof(uint32_t));
    }
}

static int gif_read_image(GifState *s)
{
    int left, top, width, height, bits_per_pixel, code_size, flags;
    int is_interleaved, has_local_palette, y, pass, y1, linesize, pal_size;
    uint32_t *ptr, *pal, *px, *pr, *ptr1;
    int ret;
    uint8_t *idx;

    /* At least 9 bytes of Image Descriptor. */
    if (bytestream2_get_bytes_left(&s->gb) < 9)
        return AVERROR_INVALIDDATA;

    left = bytestream2_get_le16u(&s->gb);
    top = bytestream2_get_le16u(&s->gb);
    width = bytestream2_get_le16u(&s->gb);
    height = bytestream2_get_le16u(&s->gb);
    flags = bytestream2_get_byteu(&s->gb);
    is_interleaved = flags & 0x40;
    has_local_palette = flags & 0x80;
    bits_per_pixel = (flags & 0x07) + 1;

    av_dlog(s->avctx, "image x=%d y=%d w=%d h=%d\n", left, top, width, height);

    if (has_local_palette) {
        pal_size = 1 << bits_per_pixel;

        if (bytestream2_get_bytes_left(&s->gb) < pal_size * 3)
            return AVERROR_INVALIDDATA;

        gif_read_palette(s, s->local_palette, pal_size);
        pal = s->local_palette;
    } else {
        if (!s->has_global_palette) {
            av_log(s->avctx, AV_LOG_ERROR, "picture doesn't have either global or local palette.\n");
            return AVERROR_INVALIDDATA;
        }

        pal = s->global_palette;
    }

    if (s->keyframe) {
        if (s->transparent_color_index == -1 && s->has_global_palette) {
            /* transparency wasn't set before the first frame, fill with background color */
            gif_fill(&s->picture, s->bg_color);
        } else {
            /* otherwise fill with transparent color.
             * this is necessary since by default picture filled with 0x80808080. */
            gif_fill(&s->picture, s->trans_color);
        }
    }

    /* verify that all the image is inside the screen dimensions */
    if (left + width > s->screen_width ||
        top + height > s->screen_height)
        return AVERROR_INVALIDDATA;
    if (width <= 0 || height <= 0)
        return AVERROR_INVALIDDATA;

    /* process disposal method */
    if (s->gce_prev_disposal == GCE_DISPOSAL_BACKGROUND) {
        gif_fill_rect(&s->picture, s->stored_bg_color, s->gce_l, s->gce_t, s->gce_w, s->gce_h);
    } else if (s->gce_prev_disposal == GCE_DISPOSAL_RESTORE) {
        gif_copy_img_rect(s->stored_img, (uint32_t *)s->picture.data[0],
            s->picture.linesize[0] / sizeof(uint32_t), s->gce_l, s->gce_t, s->gce_w, s->gce_h);
    }

    s->gce_prev_disposal = s->gce_disposal;

    if (s->gce_disposal != GCE_DISPOSAL_NONE) {
        s->gce_l = left;  s->gce_t = top;
        s->gce_w = width; s->gce_h = height;

        if (s->gce_disposal == GCE_DISPOSAL_BACKGROUND) {
            if (s->transparent_color_index >= 0)
                s->stored_bg_color = s->trans_color;
            else
                s->stored_bg_color = s->bg_color;
        } else if (s->gce_disposal == GCE_DISPOSAL_RESTORE) {
            av_fast_malloc(&s->stored_img, &s->stored_img_size, s->picture.linesize[0] * s->picture.height);
            if (!s->stored_img)
                return AVERROR(ENOMEM);

            gif_copy_img_rect((uint32_t *)s->picture.data[0], s->stored_img,
                s->picture.linesize[0] / sizeof(uint32_t), left, top, width, height);
        }
    }

    /* Expect at least 2 bytes: 1 for lzw code size and 1 for block size. */
    if (bytestream2_get_bytes_left(&s->gb) < 2)
        return AVERROR_INVALIDDATA;

    /* now get the image data */
    code_size = bytestream2_get_byteu(&s->gb);
    if ((ret = ff_lzw_decode_init(s->lzw, code_size, s->gb.buffer,
                                  bytestream2_get_bytes_left(&s->gb), FF_LZW_GIF)) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "LZW init failed\n");
        return ret;
    }

    /* read all the image */
    linesize = s->picture.linesize[0] / sizeof(uint32_t);
    ptr1 = (uint32_t *)s->picture.data[0] + top * linesize + left;
    ptr = ptr1;
    pass = 0;
    y1 = 0;
    for (y = 0; y < height; y++) {
        if (ff_lzw_decode(s->lzw, s->idx_line, width) == 0)
            goto decode_tail;

        pr = ptr + width;

        for (px = ptr, idx = s->idx_line; px < pr; px++, idx++) {
            if (*idx != s->transparent_color_index)
                *px = pal[*idx];
        }

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

 decode_tail:
    /* read the garbage data until end marker is found */
    ff_lzw_decode_tail(s->lzw);

    /* Graphic Control Extension's scope is single frame.
     * Remove its influence. */
    s->transparent_color_index = -1;
    s->gce_disposal = GCE_DISPOSAL_NONE;

    return 0;
}

static int gif_read_extension(GifState *s)
{
    int ext_code, ext_len, gce_flags, gce_transparent_index;

    /* There must be at least 2 bytes:
     * 1 for extension label and 1 for extension length. */
    if (bytestream2_get_bytes_left(&s->gb) < 2)
        return AVERROR_INVALIDDATA;

    ext_code = bytestream2_get_byteu(&s->gb);
    ext_len = bytestream2_get_byteu(&s->gb);

    av_dlog(s->avctx, "ext_code=0x%x len=%d\n", ext_code, ext_len);

    switch(ext_code) {
    case GIF_GCE_EXT_LABEL:
        if (ext_len != 4)
            goto discard_ext;

        /* We need at least 5 bytes more: 4 is for extension body
         * and 1 for next block size. */
        if (bytestream2_get_bytes_left(&s->gb) < 5)
            return AVERROR_INVALIDDATA;

        gce_flags = bytestream2_get_byteu(&s->gb);
        bytestream2_skipu(&s->gb, 2);    // delay during which the frame is shown
        gce_transparent_index = bytestream2_get_byteu(&s->gb);
        if (gce_flags & 0x01)
            s->transparent_color_index = gce_transparent_index;
        else
            s->transparent_color_index = -1;
        s->gce_disposal = (gce_flags >> 2) & 0x7;

        av_dlog(s->avctx, "gce_flags=%x tcolor=%d disposal=%d\n",
               gce_flags,
               s->transparent_color_index, s->gce_disposal);

        if (s->gce_disposal > 3) {
            s->gce_disposal = GCE_DISPOSAL_NONE;
            av_dlog(s->avctx, "invalid value in gce_disposal (%d). Using default value of 0.\n", ext_len);
        }

        ext_len = bytestream2_get_byteu(&s->gb);
        break;
    }

    /* NOTE: many extension blocks can come after */
 discard_ext:
    while (ext_len) {
        /* There must be at least ext_len bytes and 1 for next block size byte. */
        if (bytestream2_get_bytes_left(&s->gb) < ext_len + 1)
            return AVERROR_INVALIDDATA;

        bytestream2_skipu(&s->gb, ext_len);
        ext_len = bytestream2_get_byteu(&s->gb);

        av_dlog(s->avctx, "ext_len1=%d\n", ext_len);
    }
    return 0;
}

static int gif_read_header1(GifState *s)
{
    uint8_t sig[6];
    int v, n;
    int background_color_index;

    if (bytestream2_get_bytes_left(&s->gb) < 13)
        return AVERROR_INVALIDDATA;

    /* read gif signature */
    bytestream2_get_bufferu(&s->gb, sig, 6);
    if (memcmp(sig, gif87a_sig, 6) &&
        memcmp(sig, gif89a_sig, 6))
        return AVERROR_INVALIDDATA;

    /* read screen header */
    s->transparent_color_index = -1;
    s->screen_width = bytestream2_get_le16u(&s->gb);
    s->screen_height = bytestream2_get_le16u(&s->gb);

    v = bytestream2_get_byteu(&s->gb);
    s->color_resolution = ((v & 0x70) >> 4) + 1;
    s->has_global_palette = (v & 0x80);
    s->bits_per_pixel = (v & 0x07) + 1;
    background_color_index = bytestream2_get_byteu(&s->gb);
    n = bytestream2_get_byteu(&s->gb);
    if (n) {
        s->avctx->sample_aspect_ratio.num = n + 15;
        s->avctx->sample_aspect_ratio.den = 64;
    }

    av_dlog(s->avctx, "screen_w=%d screen_h=%d bpp=%d global_palette=%d\n",
           s->screen_width, s->screen_height, s->bits_per_pixel,
           s->has_global_palette);

    if (s->has_global_palette) {
        s->background_color_index = background_color_index;
        n = 1 << s->bits_per_pixel;
        if (bytestream2_get_bytes_left(&s->gb) < n * 3)
            return AVERROR_INVALIDDATA;

        gif_read_palette(s, s->global_palette, n);
        s->bg_color = s->global_palette[s->background_color_index];
    } else
        s->background_color_index = -1;

    return 0;
}

static int gif_parse_next_image(GifState *s)
{
    while (bytestream2_get_bytes_left(&s->gb)) {
        int code = bytestream2_get_byte(&s->gb);
        int ret;

        av_dlog(s->avctx, "code=%02x '%c'\n", code, code);

        switch (code) {
        case GIF_IMAGE_SEPARATOR:
            return gif_read_image(s);
        case GIF_EXTENSION_INTRODUCER:
            if ((ret = gif_read_extension(s)) < 0)
                return ret;
            break;
        case GIF_TRAILER:
            /* end of image */
            return AVERROR_EOF;
        default:
            /* erroneous block label */
            return AVERROR_INVALIDDATA;
        }
    }
    return AVERROR_EOF;
}

static av_cold int gif_decode_init(AVCodecContext *avctx)
{
    GifState *s = avctx->priv_data;

    s->avctx = avctx;

    avctx->pix_fmt = AV_PIX_FMT_RGB32;
    avcodec_get_frame_defaults(&s->picture);
    avctx->coded_frame= &s->picture;
    s->picture.data[0] = NULL;
    ff_lzw_decode_open(&s->lzw);
    return 0;
}

static int gif_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
    GifState *s = avctx->priv_data;
    AVFrame *picture = data;
    int ret;

    bytestream2_init(&s->gb, avpkt->data, avpkt->size);

    s->picture.pts          = avpkt->pts;
    s->picture.pkt_pts      = avpkt->pts;
    s->picture.pkt_dts      = avpkt->dts;
    s->picture.pkt_duration = avpkt->duration;

    if (avpkt->size >= 6) {
        s->keyframe = memcmp(avpkt->data, gif87a_sig, 6) == 0 ||
                      memcmp(avpkt->data, gif89a_sig, 6) == 0;
    } else {
        s->keyframe = 0;
    }

    if (s->keyframe) {
        s->keyframe_ok = 0;
        if ((ret = gif_read_header1(s)) < 0)
            return ret;

        if ((ret = av_image_check_size(s->screen_width, s->screen_height, 0, avctx)) < 0)
            return ret;
        avcodec_set_dimensions(avctx, s->screen_width, s->screen_height);

        if (s->picture.data[0])
            avctx->release_buffer(avctx, &s->picture);

        if ((ret = ff_get_buffer(avctx, &s->picture)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
            return ret;
        }

        av_fast_malloc(&s->idx_line, &s->idx_line_size, s->screen_width);
        if (!s->idx_line)
            return AVERROR(ENOMEM);

        s->picture.pict_type = AV_PICTURE_TYPE_I;
        s->picture.key_frame = 1;
        s->keyframe_ok = 1;
    } else {
        if (!s->keyframe_ok) {
            av_log(avctx, AV_LOG_ERROR, "cannot decode frame without keyframe\n");
            return AVERROR_INVALIDDATA;
        }

        if ((ret = avctx->reget_buffer(avctx, &s->picture)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
            return ret;
        }

        s->picture.pict_type = AV_PICTURE_TYPE_P;
        s->picture.key_frame = 0;
    }

    ret = gif_parse_next_image(s);
    if (ret < 0)
        return ret;

    *picture = s->picture;
    *got_frame = 1;

    return avpkt->size;
}

static av_cold int gif_decode_close(AVCodecContext *avctx)
{
    GifState *s = avctx->priv_data;

    ff_lzw_decode_close(&s->lzw);
    if(s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    av_freep(&s->idx_line);
    av_freep(&s->stored_img);

    return 0;
}

static const AVOption options[] = {
    { "trans_color", "color value (ARGB) that is used instead of transparent color",
      offsetof(GifState, trans_color), AV_OPT_TYPE_INT,
      {.i64 = GIF_TRANSPARENT_COLOR}, 0, 0xffffffff,
      AV_OPT_FLAG_DECODING_PARAM|AV_OPT_FLAG_VIDEO_PARAM },
    { NULL },
};

static const AVClass decoder_class = {
    .class_name = "gif decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DECODER,
};

AVCodec ff_gif_decoder = {
    .name           = "gif",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_GIF,
    .priv_data_size = sizeof(GifState),
    .init           = gif_decode_init,
    .close          = gif_decode_close,
    .decode         = gif_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("GIF (Graphics Interchange Format)"),
    .priv_class     = &decoder_class,
};
