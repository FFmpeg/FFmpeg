/*
 * Copyright (c) 2000 Fabrice Bellard
 * Copyright (c) 2002 Francois Revol
 * Copyright (c) 2006 Baptiste Coudurier
 * Copyright (c) 2018 Bjorn Roche
 * Copyright (c) 2018 Paul B Mahol
 *
 * first version by Francois Revol <revol@free.fr>
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

/**
 * @file
 * GIF encoder
 * @see http://www.w3.org/Graphics/GIF/spec-gif89a.txt
 */

#define BITSTREAM_WRITER_LE
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "encode.h"
#include "internal.h"
#include "lzw.h"
#include "gif.h"

#include "put_bits.h"

#define DEFAULT_TRANSPARENCY_INDEX 0x1f

typedef struct GIFContext {
    const AVClass *class;
    LZWState *lzw;
    uint8_t *buf;
    uint8_t *shrunk_buf;
    int buf_size;
    AVFrame *last_frame;
    int flags;
    int image;
    int use_global_palette;
    uint32_t palette[AVPALETTE_COUNT];  ///< local reference palette for !pal8
    int palette_loaded;
    int transparent_index;
    uint8_t *tmpl;                      ///< temporary line buffer
} GIFContext;

enum {
    GF_OFFSETTING = 1<<0,
    GF_TRANSDIFF  = 1<<1,
};

static void shrink_palette(const uint32_t *src, uint8_t *map,
                           uint32_t *dst, size_t *palette_count)
{
    size_t colors_seen = 0;

    for (size_t i = 0; i < AVPALETTE_COUNT; i++) {
        int seen = 0;
        for (size_t c = 0; c < colors_seen; c++) {
            if (src[i] == dst[c]) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            dst[colors_seen] = src[i];
            map[i] = colors_seen;
            colors_seen++;
        }
    }

    *palette_count = colors_seen;
}

static void remap_frame_to_palette(const uint8_t *src, int src_linesize,
                                   uint8_t *dst, int dst_linesize,
                                   int w, int h, uint8_t *map)
{
    for (int i = 0; i < h; i++)
        for (int j = 0; j < w; j++)
            dst[i * dst_linesize + j] = map[src[i * src_linesize + j]];
}

static int is_image_translucent(AVCodecContext *avctx,
                                const uint8_t *buf, const int linesize)
{
    GIFContext *s = avctx->priv_data;
    int trans = s->transparent_index;

    if (trans < 0)
        return 0;

    for (int y = 0; y < avctx->height; y++) {
        for (int x = 0; x < avctx->width; x++) {
            if (buf[x] == trans) {
                return 1;
            }
        }
        buf += linesize;
    }

    return 0;
}

static int get_palette_transparency_index(const uint32_t *palette)
{
    int transparent_color_index = -1;
    unsigned i, smallest_alpha = 0xff;

    if (!palette)
        return -1;

    for (i = 0; i < AVPALETTE_COUNT; i++) {
        const uint32_t v = palette[i];
        if (v >> 24 < smallest_alpha) {
            smallest_alpha = v >> 24;
            transparent_color_index = i;
        }
    }
    return smallest_alpha < 128 ? transparent_color_index : -1;
}

static int pick_palette_entry(const uint8_t *buf, int linesize, int w, int h)
{
    int histogram[AVPALETTE_COUNT] = {0};
    int x, y, i;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++)
            histogram[buf[x]]++;
        buf += linesize;
    }
    for (i = 0; i < FF_ARRAY_ELEMS(histogram); i++)
        if (!histogram[i])
            return i;
    return -1;
}

static void gif_crop_translucent(AVCodecContext *avctx,
                                 const uint8_t *buf, const int linesize,
                                 int *width, int *height,
                                 int *x_start, int *y_start)
{
    GIFContext *s = avctx->priv_data;
    int trans = s->transparent_index;

    /* Crop image */
    if ((s->flags & GF_OFFSETTING) && trans >= 0) {
        const int w = avctx->width;
        const int h = avctx->height;
        int x_end = w - 1,
            y_end = h - 1;

        // crop top
        while (*y_start < y_end) {
            int is_trans = 1;
            for (int i = 0; i < w; i++) {
                if (buf[linesize * *y_start + i] != trans) {
                    is_trans = 0;
                    break;
                }
            }

            if (!is_trans)
                break;
            (*y_start)++;
        }

        // crop bottom
        while (y_end > *y_start) {
            int is_trans = 1;
            for (int i = 0; i < w; i++) {
                if (buf[linesize * y_end + i] != trans) {
                    is_trans = 0;
                    break;
                }
            }
            if (!is_trans)
                break;
            y_end--;
        }

        // crop left
        while (*x_start < x_end) {
            int is_trans = 1;
            for (int i = *y_start; i < y_end; i++) {
                if (buf[linesize * i + *x_start] != trans) {
                    is_trans = 0;
                    break;
                }
            }
            if (!is_trans)
                break;
            (*x_start)++;
        }

        // crop right
        while (x_end > *x_start) {
            int is_trans = 1;
            for (int i = *y_start; i < y_end; i++) {
                if (buf[linesize * i + x_end] != trans) {
                    is_trans = 0;
                    break;
                }
            }
            if (!is_trans)
                break;
            x_end--;
        }

        *height = y_end + 1 - *y_start;
        *width  = x_end + 1 - *x_start;
        av_log(avctx, AV_LOG_DEBUG,"%dx%d image at pos (%d;%d) [area:%dx%d]\n",
               *width, *height, *x_start, *y_start, avctx->width, avctx->height);
    }
}

static void gif_crop_opaque(AVCodecContext *avctx,
                            const uint32_t *palette,
                            const uint8_t *buf, const int linesize,
                            int *width, int *height, int *x_start, int *y_start)
{
    GIFContext *s = avctx->priv_data;

    /* Crop image */
    if ((s->flags & GF_OFFSETTING) && s->last_frame && !palette) {
        const uint8_t *ref = s->last_frame->data[0];
        const int ref_linesize = s->last_frame->linesize[0];
        int x_end = avctx->width  - 1,
            y_end = avctx->height - 1;

        /* skip common lines */
        while (*y_start < y_end) {
            if (memcmp(ref + *y_start*ref_linesize, buf + *y_start*linesize, *width))
                break;
            (*y_start)++;
        }
        while (y_end > *y_start) {
            if (memcmp(ref + y_end*ref_linesize, buf + y_end*linesize, *width))
                break;
            y_end--;
        }
        *height = y_end + 1 - *y_start;

        /* skip common columns */
        while (*x_start < x_end) {
            int same_column = 1;
            for (int y = *y_start; y <= y_end; y++) {
                if (ref[y*ref_linesize + *x_start] != buf[y*linesize + *x_start]) {
                    same_column = 0;
                    break;
                }
            }
            if (!same_column)
                break;
            (*x_start)++;
        }
        while (x_end > *x_start) {
            int same_column = 1;
            for (int y = *y_start; y <= y_end; y++) {
                if (ref[y*ref_linesize + x_end] != buf[y*linesize + x_end]) {
                    same_column = 0;
                    break;
                }
            }
            if (!same_column)
                break;
            x_end--;
        }
        *width = x_end + 1 - *x_start;

        av_log(avctx, AV_LOG_DEBUG,"%dx%d image at pos (%d;%d) [area:%dx%d]\n",
               *width, *height, *x_start, *y_start, avctx->width, avctx->height);
    }
}

static int gif_image_write_image(AVCodecContext *avctx,
                                 uint8_t **bytestream, uint8_t *end,
                                 const uint32_t *palette,
                                 const uint8_t *buf, const int linesize,
                                 AVPacket *pkt)
{
    GIFContext *s = avctx->priv_data;
    int disposal, len = 0, height = avctx->height, width = avctx->width, x, y;
    int x_start = 0, y_start = 0, trans = s->transparent_index;
    int bcid = -1, honor_transparency = (s->flags & GF_TRANSDIFF) && s->last_frame && !palette;
    const uint8_t *ptr;
    uint32_t shrunk_palette[AVPALETTE_COUNT];
    uint8_t map[AVPALETTE_COUNT] = { 0 };
    size_t shrunk_palette_count = 0;

    /*
     * We memset to 0xff instead of 0x00 so that the transparency detection
     * doesn't pick anything after the palette entries as the transparency
     * index, and because GIF89a requires us to always write a power-of-2
     * number of palette entries.
     */
    memset(shrunk_palette, 0xff, AVPALETTE_SIZE);

    if (!s->image && is_image_translucent(avctx, buf, linesize)) {
        gif_crop_translucent(avctx, buf, linesize, &width, &height, &x_start, &y_start);
        honor_transparency = 0;
        disposal = GCE_DISPOSAL_BACKGROUND;
    } else {
        gif_crop_opaque(avctx, palette, buf, linesize, &width, &height, &x_start, &y_start);
        disposal = GCE_DISPOSAL_INPLACE;
    }

    if (s->image || !avctx->frame_number) { /* GIF header */
        const uint32_t *global_palette = palette ? palette : s->palette;
        const AVRational sar = avctx->sample_aspect_ratio;
        int64_t aspect = 0;

        if (sar.num > 0 && sar.den > 0) {
            aspect = sar.num * 64LL / sar.den - 15;
            if (aspect < 0 || aspect > 255)
                aspect = 0;
        }

        bytestream_put_buffer(bytestream, gif89a_sig, sizeof(gif89a_sig));
        bytestream_put_le16(bytestream, avctx->width);
        bytestream_put_le16(bytestream, avctx->height);

        bcid = get_palette_transparency_index(global_palette);

        bytestream_put_byte(bytestream, ((uint8_t) s->use_global_palette << 7) | 0x70 | (s->use_global_palette ? 7 : 0)); /* flags: global clut, 256 entries */
        bytestream_put_byte(bytestream, bcid < 0 ? DEFAULT_TRANSPARENCY_INDEX : bcid); /* background color index */
        bytestream_put_byte(bytestream, aspect);
        if (s->use_global_palette) {
            for (int i = 0; i < 256; i++) {
                const uint32_t v = global_palette[i] & 0xffffff;
                bytestream_put_be24(bytestream, v);
            }
        }
    }

    if (honor_transparency && trans < 0) {
        trans = pick_palette_entry(buf + y_start*linesize + x_start,
                                   linesize, width, height);
        if (trans < 0) // TODO, patch welcome
            av_log(avctx, AV_LOG_DEBUG, "No available color, can not use transparency\n");
    }

    if (trans < 0)
        honor_transparency = 0;

    if (palette || !s->use_global_palette) {
        const uint32_t *pal = palette ? palette : s->palette;
        shrink_palette(pal, map, shrunk_palette, &shrunk_palette_count);
    }

    bcid = honor_transparency || disposal == GCE_DISPOSAL_BACKGROUND ? trans : get_palette_transparency_index(palette);

    /* graphic control extension */
    bytestream_put_byte(bytestream, GIF_EXTENSION_INTRODUCER);
    bytestream_put_byte(bytestream, GIF_GCE_EXT_LABEL);
    bytestream_put_byte(bytestream, 0x04); /* block size */
    bytestream_put_byte(bytestream, disposal<<2 | (bcid >= 0));
    bytestream_put_le16(bytestream, 5); // default delay
    bytestream_put_byte(bytestream, bcid < 0 ? DEFAULT_TRANSPARENCY_INDEX : (shrunk_palette_count ? map[bcid] : bcid));
    bytestream_put_byte(bytestream, 0x00);

    /* image block */
    bytestream_put_byte(bytestream, GIF_IMAGE_SEPARATOR);
    bytestream_put_le16(bytestream, x_start);
    bytestream_put_le16(bytestream, y_start);
    bytestream_put_le16(bytestream, width);
    bytestream_put_le16(bytestream, height);

    if (palette || !s->use_global_palette) {
        unsigned pow2_count = av_log2(shrunk_palette_count - 1);
        unsigned i;

        bytestream_put_byte(bytestream, 1<<7 | pow2_count); /* flags */
        for (i = 0; i < 1 << (pow2_count + 1); i++) {
            const uint32_t v = shrunk_palette[i];
            bytestream_put_be24(bytestream, v);
        }
    } else {
        bytestream_put_byte(bytestream, 0x00); /* flags */
    }

    bytestream_put_byte(bytestream, 0x08);

    ff_lzw_encode_init(s->lzw, s->buf, s->buf_size,
                       12, FF_LZW_GIF, 1);

    if (shrunk_palette_count) {
        if (!s->shrunk_buf) {
            s->shrunk_buf = av_malloc(avctx->height * linesize);
            if (!s->shrunk_buf) {
                av_log(avctx, AV_LOG_ERROR, "Could not allocated remapped frame buffer.\n");
                return AVERROR(ENOMEM);
            }
        }
        remap_frame_to_palette(buf, linesize, s->shrunk_buf, linesize, avctx->width, avctx->height, map);
        ptr = s->shrunk_buf + y_start*linesize + x_start;
    } else {
        ptr = buf + y_start*linesize + x_start;
    }
    if (honor_transparency) {
        const int ref_linesize = s->last_frame->linesize[0];
        const uint8_t *ref = s->last_frame->data[0] + y_start*ref_linesize + x_start;

        for (y = 0; y < height; y++) {
            memcpy(s->tmpl, ptr, width);
            for (x = 0; x < width; x++)
                if (ref[x] == ptr[x])
                    s->tmpl[x] = trans;
            len += ff_lzw_encode(s->lzw, s->tmpl, width);
            ptr += linesize;
            ref += ref_linesize;
        }
    } else {
        for (y = 0; y < height; y++) {
            len += ff_lzw_encode(s->lzw, ptr, width);
            ptr += linesize;
        }
    }
    len += ff_lzw_encode_flush(s->lzw);

    ptr = s->buf;
    while (len > 0) {
        int size = FFMIN(255, len);
        bytestream_put_byte(bytestream, size);
        if (end - *bytestream < size)
            return -1;
        bytestream_put_buffer(bytestream, ptr, size);
        ptr += size;
        len -= size;
    }
    bytestream_put_byte(bytestream, 0x00); /* end of image block */
    return 0;
}

static av_cold int gif_encode_init(AVCodecContext *avctx)
{
    GIFContext *s = avctx->priv_data;

    if (avctx->width > 65535 || avctx->height > 65535) {
        av_log(avctx, AV_LOG_ERROR, "GIF does not support resolutions above 65535x65535\n");
        return AVERROR(EINVAL);
    }

    s->transparent_index = -1;

    s->lzw = av_mallocz(ff_lzw_encode_state_size);
    s->buf_size = avctx->width*avctx->height*2 + 1000;
    s->buf = av_malloc(s->buf_size);
    s->tmpl = av_malloc(avctx->width);
    if (!s->tmpl || !s->buf || !s->lzw)
        return AVERROR(ENOMEM);

    if (avpriv_set_systematic_pal2(s->palette, avctx->pix_fmt) < 0)
        av_assert0(avctx->pix_fmt == AV_PIX_FMT_PAL8);

    return 0;
}

static int gif_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *pict, int *got_packet)
{
    GIFContext *s = avctx->priv_data;
    uint8_t *outbuf_ptr, *end;
    const uint32_t *palette = NULL;
    int ret;

    if ((ret = ff_alloc_packet(avctx, pkt, avctx->width*avctx->height*7/5 + AV_INPUT_BUFFER_MIN_SIZE)) < 0)
        return ret;
    outbuf_ptr = pkt->data;
    end        = pkt->data + pkt->size;

    if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        palette = (uint32_t*)pict->data[1];

        if (!s->palette_loaded) {
            memcpy(s->palette, palette, AVPALETTE_SIZE);
            s->transparent_index = get_palette_transparency_index(palette);
            s->palette_loaded = 1;
        } else if (!memcmp(s->palette, palette, AVPALETTE_SIZE)) {
            palette = NULL;
        }
    }

    gif_image_write_image(avctx, &outbuf_ptr, end, palette,
                          pict->data[0], pict->linesize[0], pkt);
    if (!s->last_frame && !s->image) {
        s->last_frame = av_frame_alloc();
        if (!s->last_frame)
            return AVERROR(ENOMEM);
    }

    if (!s->image) {
        av_frame_unref(s->last_frame);
        ret = av_frame_ref(s->last_frame, (AVFrame*)pict);
        if (ret < 0)
            return ret;
    }

    pkt->size   = outbuf_ptr - pkt->data;
    if (s->image || !avctx->frame_number)
        pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

static int gif_encode_close(AVCodecContext *avctx)
{
    GIFContext *s = avctx->priv_data;

    av_freep(&s->lzw);
    av_freep(&s->buf);
    av_freep(&s->shrunk_buf);
    s->buf_size = 0;
    av_frame_free(&s->last_frame);
    av_freep(&s->tmpl);
    return 0;
}

#define OFFSET(x) offsetof(GIFContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption gif_options[] = {
    { "gifflags", "set GIF flags", OFFSET(flags), AV_OPT_TYPE_FLAGS, {.i64 = GF_OFFSETTING|GF_TRANSDIFF}, 0, INT_MAX, FLAGS, "flags" },
        { "offsetting", "enable picture offsetting", 0, AV_OPT_TYPE_CONST, {.i64=GF_OFFSETTING}, INT_MIN, INT_MAX, FLAGS, "flags" },
        { "transdiff", "enable transparency detection between frames", 0, AV_OPT_TYPE_CONST, {.i64=GF_TRANSDIFF}, INT_MIN, INT_MAX, FLAGS, "flags" },
    { "gifimage", "enable encoding only images per frame", OFFSET(image), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "global_palette", "write a palette to the global gif header where feasible", OFFSET(use_global_palette), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, FLAGS },
    { NULL }
};

static const AVClass gif_class = {
    .class_name = "GIF encoder",
    .item_name  = av_default_item_name,
    .option     = gif_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVCodec ff_gif_encoder = {
    .name           = "gif",
    .long_name      = NULL_IF_CONFIG_SMALL("GIF (Graphics Interchange Format)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_GIF,
    .priv_data_size = sizeof(GIFContext),
    .init           = gif_encode_init,
    .encode2        = gif_encode_frame,
    .close          = gif_encode_close,
    .pix_fmts       = (const enum AVPixelFormat[]){
        AV_PIX_FMT_RGB8, AV_PIX_FMT_BGR8, AV_PIX_FMT_RGB4_BYTE, AV_PIX_FMT_BGR4_BYTE,
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_PAL8, AV_PIX_FMT_NONE
    },
    .priv_class     = &gif_class,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
