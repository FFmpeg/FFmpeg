/*
 * DVD subtitle encoding
 * Copyright (c) 2005 Wolfram Gloger
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
#include "avcodec.h"
#include "bytestream.h"
#include "libavutil/avassert.h"
#include "libavutil/bprint.h"
#include "libavutil/imgutils.h"

typedef struct {
    uint32_t global_palette[16];
} DVDSubtitleContext;

// ncnt is the nibble counter
#define PUTNIBBLE(val)\
do {\
    if (ncnt++ & 1)\
        *q++ = bitbuf | ((val) & 0x0f);\
    else\
        bitbuf = (val) << 4;\
} while(0)

static void dvd_encode_rle(uint8_t **pq,
                           const uint8_t *bitmap, int linesize,
                           int w, int h,
                           const int cmap[256])
{
    uint8_t *q;
    unsigned int bitbuf = 0;
    int ncnt;
    int x, y, len, color;

    q = *pq;

    for (y = 0; y < h; ++y) {
        ncnt = 0;
        for(x = 0; x < w; x += len) {
            color = bitmap[x];
            for (len=1; x+len < w; ++len)
                if (bitmap[x+len] != color)
                    break;
            color = cmap[color];
            av_assert0(color < 4);
            if (len < 0x04) {
                PUTNIBBLE((len << 2)|color);
            } else if (len < 0x10) {
                PUTNIBBLE(len >> 2);
                PUTNIBBLE((len << 2)|color);
            } else if (len < 0x40) {
                PUTNIBBLE(0);
                PUTNIBBLE(len >> 2);
                PUTNIBBLE((len << 2)|color);
            } else if (x+len == w) {
                PUTNIBBLE(0);
                PUTNIBBLE(0);
                PUTNIBBLE(0);
                PUTNIBBLE(color);
            } else {
                if (len > 0xff)
                    len = 0xff;
                PUTNIBBLE(0);
                PUTNIBBLE(len >> 6);
                PUTNIBBLE(len >> 2);
                PUTNIBBLE((len << 2)|color);
            }
        }
        /* end of line */
        if (ncnt & 1)
            PUTNIBBLE(0);
        bitmap += linesize;
    }

    *pq = q;
}

static int color_distance(uint32_t a, uint32_t b)
{
    int r = 0, d, i;

    for (i = 0; i < 32; i += 8) {
        d = ((a >> i) & 0xFF) - ((b >> i) & 0xFF);
        r += d * d;
    }
    return r;
}

/**
 * Count colors used in a rectangle, quantizing alpha and grouping by
 * nearest global palette entry.
 */
static void count_colors(AVCodecContext *avctx, unsigned hits[33],
                         const AVSubtitleRect *r)
{
    DVDSubtitleContext *dvdc = avctx->priv_data;
    unsigned count[256] = { 0 };
    uint32_t *palette = (uint32_t *)r->pict.data[1];
    uint32_t color;
    int x, y, i, j, match, d, best_d, av_uninit(best_j);
    uint8_t *p = r->pict.data[0];

    for (y = 0; y < r->h; y++) {
        for (x = 0; x < r->w; x++)
            count[*(p++)]++;
        p += r->pict.linesize[0] - r->w;
    }
    for (i = 0; i < 256; i++) {
        if (!count[i]) /* avoid useless search */
            continue;
        color = palette[i];
        /* 0: transparent, 1-16: semi-transparent, 17-33 opaque */
        match = color < 0x33000000 ? 0 : color < 0xCC000000 ? 1 : 17;
        if (match) {
            best_d = INT_MAX;
            for (j = 0; j < 16; j++) {
                d = color_distance(color & 0xFFFFFF, dvdc->global_palette[j]);
                if (d < best_d) {
                    best_d = d;
                    best_j = j;
                }
            }
            match += best_j;
        }
        hits[match] += count[i];
    }
}

static void select_palette(AVCodecContext *avctx, int out_palette[4],
                           int out_alpha[4], unsigned hits[33])
{
    DVDSubtitleContext *dvdc = avctx->priv_data;
    int i, j, bright, mult;
    uint32_t color;
    int selected[4] = { 0 };
    uint32_t pseudopal[33] = { 0 };
    uint32_t refcolor[3] = { 0x00000000, 0xFFFFFFFF, 0xFF000000 };

    /* Bonus for transparent: if the rectangle fits tightly the text, the
       background color can be quite rare, but it would be ugly without it */
    hits[0] *= 16;
    /* Bonus for bright colors */
    for (i = 0; i < 16; i++) {
        if (!(hits[1 + i] + hits[17 + i]))
            continue; /* skip unused colors to gain time */
        color = dvdc->global_palette[i];
        bright = 0;
        for (j = 0; j < 3; j++, color >>= 8)
            bright += (color & 0xFF) < 0x40 || (color & 0xFF) >= 0xC0;
        mult = 2 + FFMIN(bright, 2);
        hits[ 1 + i] *= mult;
        hits[17 + i] *= mult;
    }

    /* Select four most frequent colors */
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 33; j++)
            if (hits[j] > hits[selected[i]])
                selected[i] = j;
        hits[selected[i]] = 0;
    }

    /* Order the colors like in most DVDs:
       0: background, 1: foreground, 2: outline */
    for (i = 0; i < 16; i++) {
        pseudopal[ 1 + i] = 0x80000000 | dvdc->global_palette[i];
        pseudopal[17 + i] = 0xFF000000 | dvdc->global_palette[i];
    }
    for (i = 0; i < 3; i++) {
        int best_d = color_distance(refcolor[i], pseudopal[selected[i]]);
        for (j = i + 1; j < 4; j++) {
            int d = color_distance(refcolor[i], pseudopal[selected[j]]);
            if (d < best_d) {
                FFSWAP(int, selected[i], selected[j]);
                best_d = d;
            }
        }
    }

    /* Output */
    for (i = 0; i < 4; i++) {
        out_palette[i] = selected[i] ? (selected[i] - 1) & 0xF : 0;
        out_alpha  [i] = !selected[i] ? 0 : selected[i] < 17 ? 0x80 : 0xFF;
    }
}

static void build_color_map(AVCodecContext *avctx, int cmap[],
                            const uint32_t palette[],
                            const int out_palette[], int const out_alpha[])
{
    DVDSubtitleContext *dvdc = avctx->priv_data;
    int i, j, d, best_d;
    uint32_t pseudopal[4];

    for (i = 0; i < 4; i++)
        pseudopal[i] = (out_alpha[i] << 24) |
                       dvdc->global_palette[out_palette[i]];
    for (i = 0; i < 256; i++) {
        best_d = INT_MAX;
        for (j = 0; j < 4; j++) {
            d = color_distance(pseudopal[j], palette[i]);
            if (d < best_d) {
                cmap[i] = j;
                best_d = d;
            }
        }
    }
}

static void copy_rectangle(AVSubtitleRect *dst, AVSubtitleRect *src, int cmap[])
{
    int x, y;
    uint8_t *p, *q;

    p = src->pict.data[0];
    q = dst->pict.data[0] + (src->x - dst->x) +
                            (src->y - dst->y) * dst->pict.linesize[0];
    for (y = 0; y < src->h; y++) {
        for (x = 0; x < src->w; x++)
            *(q++) = cmap[*(p++)];
        p += src->pict.linesize[0] - src->w;
        q += dst->pict.linesize[0] - src->w;
    }
}

static int encode_dvd_subtitles(AVCodecContext *avctx,
                                uint8_t *outbuf, int outbuf_size,
                                const AVSubtitle *h)
{
    DVDSubtitleContext *dvdc = avctx->priv_data;
    uint8_t *q, *qq;
    int offset1, offset2;
    int i, rects = h->num_rects, ret;
    unsigned global_palette_hits[33] = { 0 };
    int cmap[256];
    int out_palette[4];
    int out_alpha[4];
    AVSubtitleRect vrect;
    uint8_t *vrect_data = NULL;
    int x2, y2;

    if (rects == 0 || h->rects == NULL)
        return AVERROR(EINVAL);
    for (i = 0; i < rects; i++)
        if (h->rects[i]->type != SUBTITLE_BITMAP) {
            av_log(avctx, AV_LOG_ERROR, "Bitmap subtitle required\n");
            return AVERROR(EINVAL);
        }
    vrect = *h->rects[0];

    if (rects > 1) {
        /* DVD subtitles can have only one rectangle: build a virtual
           rectangle containing all actual rectangles.
           The data of the rectangles will be copied later, when the palette
           is decided, because the rectangles may have different palettes. */
        int xmin = h->rects[0]->x, xmax = xmin + h->rects[0]->w;
        int ymin = h->rects[0]->y, ymax = ymin + h->rects[0]->h;
        for (i = 1; i < rects; i++) {
            xmin = FFMIN(xmin, h->rects[i]->x);
            ymin = FFMIN(ymin, h->rects[i]->y);
            xmax = FFMAX(xmax, h->rects[i]->x + h->rects[i]->w);
            ymax = FFMAX(ymax, h->rects[i]->y + h->rects[i]->h);
        }
        vrect.x = xmin;
        vrect.y = ymin;
        vrect.w = xmax - xmin;
        vrect.h = ymax - ymin;
        if ((ret = av_image_check_size(vrect.w, vrect.h, 0, avctx)) < 0)
            return ret;

        /* Count pixels outside the virtual rectangle as transparent */
        global_palette_hits[0] = vrect.w * vrect.h;
        for (i = 0; i < rects; i++)
            global_palette_hits[0] -= h->rects[i]->w * h->rects[i]->h;
    }

    for (i = 0; i < rects; i++)
        count_colors(avctx, global_palette_hits, h->rects[i]);
    select_palette(avctx, out_palette, out_alpha, global_palette_hits);

    if (rects > 1) {
        if (!(vrect_data = av_calloc(vrect.w, vrect.h)))
            return AVERROR(ENOMEM);
        vrect.pict.data    [0] = vrect_data;
        vrect.pict.linesize[0] = vrect.w;
        for (i = 0; i < rects; i++) {
            build_color_map(avctx, cmap, (uint32_t *)h->rects[i]->pict.data[1],
                            out_palette, out_alpha);
            copy_rectangle(&vrect, h->rects[i], cmap);
        }
        for (i = 0; i < 4; i++)
            cmap[i] = i;
    } else {
        build_color_map(avctx, cmap, (uint32_t *)h->rects[0]->pict.data[1],
                        out_palette, out_alpha);
    }

    av_log(avctx, AV_LOG_DEBUG, "Selected palette:");
    for (i = 0; i < 4; i++)
        av_log(avctx, AV_LOG_DEBUG, " 0x%06x@@%02x (0x%x,0x%x)",
               dvdc->global_palette[out_palette[i]], out_alpha[i],
               out_palette[i], out_alpha[i] >> 4);
    av_log(avctx, AV_LOG_DEBUG, "\n");

    // encode data block
    q = outbuf + 4;
    offset1 = q - outbuf;
    // worst case memory requirement: 1 nibble per pixel..
    if ((q - outbuf) + vrect.w * vrect.h / 2 + 17 + 21 > outbuf_size) {
        av_log(NULL, AV_LOG_ERROR, "dvd_subtitle too big\n");
        ret = AVERROR_BUFFER_TOO_SMALL;
        goto fail;
    }
    dvd_encode_rle(&q, vrect.pict.data[0], vrect.w * 2,
                   vrect.w, (vrect.h + 1) >> 1, cmap);
    offset2 = q - outbuf;
    dvd_encode_rle(&q, vrect.pict.data[0] + vrect.w, vrect.w * 2,
                   vrect.w, vrect.h >> 1, cmap);

    // set data packet size
    qq = outbuf + 2;
    bytestream_put_be16(&qq, q - outbuf);

    // send start display command
    bytestream_put_be16(&q, (h->start_display_time*90) >> 10);
    bytestream_put_be16(&q, (q - outbuf) /*- 2 */ + 8 + 12 + 2);
    *q++ = 0x03; // palette - 4 nibbles
    *q++ = (out_palette[3] << 4) | out_palette[2];
    *q++ = (out_palette[1] << 4) | out_palette[0];
    *q++ = 0x04; // alpha - 4 nibbles
    *q++ = (out_alpha[3] & 0xF0) | (out_alpha[2] >> 4);
    *q++ = (out_alpha[1] & 0xF0) | (out_alpha[0] >> 4);

    // 12 bytes per rect
    x2 = vrect.x + vrect.w - 1;
    y2 = vrect.y + vrect.h - 1;

    *q++ = 0x05;
    // x1 x2 -> 6 nibbles
    *q++ = vrect.x >> 4;
    *q++ = (vrect.x << 4) | ((x2 >> 8) & 0xf);
    *q++ = x2;
    // y1 y2 -> 6 nibbles
    *q++ = vrect.y >> 4;
    *q++ = (vrect.y << 4) | ((y2 >> 8) & 0xf);
    *q++ = y2;

    *q++ = 0x06;
    // offset1, offset2
    bytestream_put_be16(&q, offset1);
    bytestream_put_be16(&q, offset2);

    *q++ = 0x01; // start command
    *q++ = 0xff; // terminating command

    // send stop display command last
    bytestream_put_be16(&q, (h->end_display_time*90) >> 10);
    bytestream_put_be16(&q, (q - outbuf) - 2 /*+ 4*/);
    *q++ = 0x02; // set end
    *q++ = 0xff; // terminating command

    qq = outbuf;
    bytestream_put_be16(&qq, q - outbuf);

    av_log(NULL, AV_LOG_DEBUG, "subtitle_packet size=%td\n", q - outbuf);
    ret = q - outbuf;

fail:
    av_free(vrect_data);
    return ret;
}

static int dvdsub_init(AVCodecContext *avctx)
{
    DVDSubtitleContext *dvdc = avctx->priv_data;
    static const uint32_t default_palette[16] = {
        0x000000, 0x0000FF, 0x00FF00, 0xFF0000,
        0xFFFF00, 0xFF00FF, 0x00FFFF, 0xFFFFFF,
        0x808000, 0x8080FF, 0x800080, 0x80FF80,
        0x008080, 0xFF8080, 0x555555, 0xAAAAAA,
    };
    AVBPrint extradata;
    int i, ret;

    av_assert0(sizeof(dvdc->global_palette) == sizeof(default_palette));
    memcpy(dvdc->global_palette, default_palette, sizeof(dvdc->global_palette));

    av_bprint_init(&extradata, 0, 1);
    if (avctx->width && avctx->height)
        av_bprintf(&extradata, "size: %dx%d\n", avctx->width, avctx->height);
    av_bprintf(&extradata, "palette:");
    for (i = 0; i < 16; i++)
        av_bprintf(&extradata, " %06"PRIx32"%c",
                   dvdc->global_palette[i] & 0xFFFFFF, i < 15 ? ',' : '\n');

    if ((ret = av_bprint_finalize(&extradata, (char **)&avctx->extradata)) < 0)
        return ret;
    avctx->extradata_size = extradata.len;

    return 0;
}

static int dvdsub_encode(AVCodecContext *avctx,
                         unsigned char *buf, int buf_size,
                         const AVSubtitle *sub)
{
    //DVDSubtitleContext *s = avctx->priv_data;
    int ret;

    ret = encode_dvd_subtitles(avctx, buf, buf_size, sub);
    return ret;
}

AVCodec ff_dvdsub_encoder = {
    .name           = "dvdsub",
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_DVD_SUBTITLE,
    .init           = dvdsub_init,
    .encode_sub     = dvdsub_encode,
    .long_name      = NULL_IF_CONFIG_SMALL("DVD subtitles"),
    .priv_data_size = sizeof(DVDSubtitleContext),
};
