/*
 * Copyright (c) 2000 Fabrice Bellard
 * Copyright (c) 2002 Francois Revol
 * Copyright (c) 2006 Baptiste Coudurier
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
#include "internal.h"
#include "lzw.h"
#include "gif.h"

#include "put_bits.h"

typedef struct GIFContext {
    const AVClass *class;
    LZWState *lzw;
    uint8_t *buf;
    int buf_size;
    AVFrame *last_frame;
    int flags;
    uint32_t palette[AVPALETTE_COUNT];  ///< local reference palette for !pal8
    int palette_loaded;
    int transparent_index;
    uint8_t *pal_exdata;
    uint8_t *tmpl;                      ///< temporary line buffer
} GIFContext;

enum {
    GF_OFFSETTING = 1<<0,
    GF_TRANSDIFF  = 1<<1,
};

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

static int gif_image_write_image(AVCodecContext *avctx,
                                 uint8_t **bytestream, uint8_t *end,
                                 const uint32_t *palette,
                                 const uint8_t *buf, const int linesize,
                                 AVPacket *pkt)
{
    GIFContext *s = avctx->priv_data;
    int len = 0, height = avctx->height, width = avctx->width, x, y;
    int x_start = 0, y_start = 0, trans = s->transparent_index;
    int honor_transparency = (s->flags & GF_TRANSDIFF) && s->last_frame;
    const uint8_t *ptr;

    /* Crop image */
    if ((s->flags & GF_OFFSETTING) && s->last_frame && !palette) {
        const uint8_t *ref = s->last_frame->data[0];
        const int ref_linesize = s->last_frame->linesize[0];
        int x_end = avctx->width  - 1,
            y_end = avctx->height - 1;

        /* skip common lines */
        while (y_start < y_end) {
            if (memcmp(ref + y_start*ref_linesize, buf + y_start*linesize, width))
                break;
            y_start++;
        }
        while (y_end > y_start) {
            if (memcmp(ref + y_end*ref_linesize, buf + y_end*linesize, width))
                break;
            y_end--;
        }
        height = y_end + 1 - y_start;

        /* skip common columns */
        while (x_start < x_end) {
            int same_column = 1;
            for (y = y_start; y <= y_end; y++) {
                if (ref[y*ref_linesize + x_start] != buf[y*linesize + x_start]) {
                    same_column = 0;
                    break;
                }
            }
            if (!same_column)
                break;
            x_start++;
        }
        while (x_end > x_start) {
            int same_column = 1;
            for (y = y_start; y <= y_end; y++) {
                if (ref[y*ref_linesize + x_end] != buf[y*linesize + x_end]) {
                    same_column = 0;
                    break;
                }
            }
            if (!same_column)
                break;
            x_end--;
        }
        width = x_end + 1 - x_start;

        av_log(avctx, AV_LOG_DEBUG,"%dx%d image at pos (%d;%d) [area:%dx%d]\n",
               width, height, x_start, y_start, avctx->width, avctx->height);
    }

    /* image block */
    bytestream_put_byte(bytestream, GIF_IMAGE_SEPARATOR);
    bytestream_put_le16(bytestream, x_start);
    bytestream_put_le16(bytestream, y_start);
    bytestream_put_le16(bytestream, width);
    bytestream_put_le16(bytestream, height);

    if (!palette) {
        bytestream_put_byte(bytestream, 0x00); /* flags */
    } else {
        unsigned i;
        bytestream_put_byte(bytestream, 1<<7 | 0x7); /* flags */
        for (i = 0; i < AVPALETTE_COUNT; i++) {
            const uint32_t v = palette[i];
            bytestream_put_be24(bytestream, v);
        }
    }

    if (honor_transparency && trans < 0) {
        trans = pick_palette_entry(buf + y_start*linesize + x_start,
                                   linesize, width, height);
        if (trans < 0) { // TODO, patch welcome
            av_log(avctx, AV_LOG_DEBUG, "No available color, can not use transparency\n");
        } else {
            uint8_t *pal_exdata = s->pal_exdata;
            if (!pal_exdata)
                pal_exdata = av_packet_new_side_data(pkt, AV_PKT_DATA_PALETTE, AVPALETTE_SIZE);
            if (!pal_exdata)
                return AVERROR(ENOMEM);
            memcpy(pal_exdata, s->palette, AVPALETTE_SIZE);
            pal_exdata[trans*4 + 3*!HAVE_BIGENDIAN] = 0x00;
        }
    }
    if (trans < 0)
        honor_transparency = 0;

    bytestream_put_byte(bytestream, 0x08);

    ff_lzw_encode_init(s->lzw, s->buf, s->buf_size,
                       12, FF_LZW_GIF, put_bits);

    ptr = buf + y_start*linesize + x_start;
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
    len += ff_lzw_encode_flush(s->lzw, flush_put_bits);

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
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
    avctx->coded_frame->key_frame = 1;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

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

/* FIXME: duplicated with lavc */
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

static int gif_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *pict, int *got_packet)
{
    GIFContext *s = avctx->priv_data;
    uint8_t *outbuf_ptr, *end;
    const uint32_t *palette = NULL;
    int ret;

    if ((ret = ff_alloc_packet2(avctx, pkt, avctx->width*avctx->height*7/5 + AV_INPUT_BUFFER_MIN_SIZE, 0)) < 0)
        return ret;
    outbuf_ptr = pkt->data;
    end        = pkt->data + pkt->size;

    if (avctx->pix_fmt == AV_PIX_FMT_PAL8) {
        uint8_t *pal_exdata = av_packet_new_side_data(pkt, AV_PKT_DATA_PALETTE, AVPALETTE_SIZE);
        if (!pal_exdata)
            return AVERROR(ENOMEM);
        memcpy(pal_exdata, pict->data[1], AVPALETTE_SIZE);
        palette = (uint32_t*)pict->data[1];

        s->pal_exdata = pal_exdata;

        /* The first palette with PAL8 will be used as generic palette by the
         * muxer so we don't need to write it locally in the packet. We store
         * it as a reference here in case it changes later. */
        if (!s->palette_loaded) {
            memcpy(s->palette, palette, AVPALETTE_SIZE);
            s->transparent_index = get_palette_transparency_index(palette);
            s->palette_loaded = 1;
            palette = NULL;
        } else if (!memcmp(s->palette, palette, AVPALETTE_SIZE)) {
            palette = NULL;
        }
    }

    gif_image_write_image(avctx, &outbuf_ptr, end, palette,
                          pict->data[0], pict->linesize[0], pkt);
    if (!s->last_frame) {
        s->last_frame = av_frame_alloc();
        if (!s->last_frame)
            return AVERROR(ENOMEM);
    }
    av_frame_unref(s->last_frame);
    ret = av_frame_ref(s->last_frame, (AVFrame*)pict);
    if (ret < 0)
        return ret;

    pkt->size   = outbuf_ptr - pkt->data;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

static int gif_encode_close(AVCodecContext *avctx)
{
    GIFContext *s = avctx->priv_data;

    av_freep(&s->lzw);
    av_freep(&s->buf);
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
    { NULL }
};

static const AVClass gif_class = {
    .class_name = "GIF encoder",
    .item_name  = av_default_item_name,
    .option     = gif_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_gif_encoder = {
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
};
