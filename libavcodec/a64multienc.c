/*
 * a64 video encoder - multicolor modes
 * Copyright (c) 2009 Tobias Bindhammer
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
 * a64 video encoder - multicolor modes
 */

#include "a64colors.h"
#include "a64tables.h"
#include "elbg.h"
#include "internal.h"
#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"

#define DITHERSTEPS   8
#define CHARSET_CHARS 256
#define INTERLACED    1
#define CROP_SCREENS  1

#define C64XRES 320
#define C64YRES 200

typedef struct A64Context {
    /* variables for multicolor modes */
    AVLFG randctx;
    int mc_lifetime;
    int mc_use_5col;
    unsigned mc_frame_counter;
    int *mc_meta_charset;
    int *mc_charmap;
    int *mc_best_cb;
    int mc_luma_vals[5];
    uint8_t *mc_charset;
    uint8_t *mc_colram;
    uint8_t *mc_palette;
    int mc_pal_size;

    /* pts of the next packet that will be output */
    int64_t next_pts;
} A64Context;

/* gray gradient */
static const int mc_colors[5]={0x0,0xb,0xc,0xf,0x1};

/* other possible gradients - to be tested */
//static const int mc_colors[5]={0x0,0x8,0xa,0xf,0x7};
//static const int mc_colors[5]={0x0,0x9,0x8,0xa,0x3};

static void to_meta_with_crop(AVCodecContext *avctx, const AVFrame *p, int *dest)
{
    int blockx, blocky, x, y;
    int luma = 0;
    int height = FFMIN(avctx->height, C64YRES);
    int width  = FFMIN(avctx->width , C64XRES);
    uint8_t *src = p->data[0];

    for (blocky = 0; blocky < C64YRES; blocky += 8) {
        for (blockx = 0; blockx < C64XRES; blockx += 8) {
            for (y = blocky; y < blocky + 8 && y < C64YRES; y++) {
                for (x = blockx; x < blockx + 8 && x < C64XRES; x += 2) {
                    if(x < width && y < height) {
                        if (x + 1 < width) {
                            /* build average over 2 pixels */
                            luma = (src[(x + 0 + y * p->linesize[0])] +
                                    src[(x + 1 + y * p->linesize[0])]) / 2;
                        } else {
                            luma = src[(x + y * p->linesize[0])];
                        }
                        /* write blocks as linear data now so they are suitable for elbg */
                        dest[0] = luma;
                    }
                    dest++;
                }
            }
        }
    }
}

static void render_charset(AVCodecContext *avctx, uint8_t *charset,
                           uint8_t *colrammap)
{
    A64Context *c = avctx->priv_data;
    uint8_t row1, row2;
    int charpos, x, y;
    int a, b;
    uint8_t pix;
    int lowdiff, highdiff;
    int *best_cb = c->mc_best_cb;
    static uint8_t index1[256];
    static uint8_t index2[256];
    static uint8_t dither[256];
    int i;
    int distance;

    /* generate lookup-tables for dither and index before looping */
    i = 0;
    for (a=0; a < 256; a++) {
        if(i < c->mc_pal_size -1 && a == c->mc_luma_vals[i + 1]) {
            distance = c->mc_luma_vals[i + 1] - c->mc_luma_vals[i];
            for(b = 0; b <= distance; b++) {
                  dither[c->mc_luma_vals[i] + b] = b * (DITHERSTEPS - 1) / distance;
            }
            i++;
        }
        if(i >= c->mc_pal_size - 1) dither[a] = 0;
        index1[a] = i;
        index2[a] = FFMIN(i + 1, c->mc_pal_size - 1);
    }

    /* and render charset */
    for (charpos = 0; charpos < CHARSET_CHARS; charpos++) {
        lowdiff  = 0;
        highdiff = 0;
        for (y = 0; y < 8; y++) {
            row1 = 0; row2 = 0;
            for (x = 0; x < 4; x++) {
                pix = best_cb[y * 4 + x];

                /* accumulate error for brightest/darkest color */
                if (index1[pix] >= 3)
                    highdiff += pix - c->mc_luma_vals[3];
                if (index1[pix] < 1)
                    lowdiff += c->mc_luma_vals[1] - pix;

                row1 <<= 2;

                if (INTERLACED) {
                    row2 <<= 2;
                    if (interlaced_dither_patterns[dither[pix]][(y & 3) * 2 + 0][x & 3])
                        row1 |= 3-(index2[pix] & 3);
                    else
                        row1 |= 3-(index1[pix] & 3);

                    if (interlaced_dither_patterns[dither[pix]][(y & 3) * 2 + 1][x & 3])
                        row2 |= 3-(index2[pix] & 3);
                    else
                        row2 |= 3-(index1[pix] & 3);
                }
                else {
                    if (multi_dither_patterns[dither[pix]][(y & 3)][x & 3])
                        row1 |= 3-(index2[pix] & 3);
                    else
                        row1 |= 3-(index1[pix] & 3);
                }
            }
            charset[y+0x000] = row1;
            if (INTERLACED) charset[y+0x800] = row2;
        }
        /* do we need to adjust pixels? */
        if (highdiff > 0 && lowdiff > 0 && c->mc_use_5col) {
            if (lowdiff > highdiff) {
                for (x = 0; x < 32; x++)
                    best_cb[x] = FFMIN(c->mc_luma_vals[3], best_cb[x]);
            } else {
                for (x = 0; x < 32; x++)
                    best_cb[x] = FFMAX(c->mc_luma_vals[1], best_cb[x]);
            }
            charpos--;          /* redo now adjusted char */
        /* no adjustment needed, all fine */
        } else {
            /* advance pointers */
            best_cb += 32;
            charset += 8;

            /* remember colorram value */
            colrammap[charpos] = (highdiff > 0);
        }
    }
}

static av_cold int a64multi_close_encoder(AVCodecContext *avctx)
{
    A64Context *c = avctx->priv_data;
    av_freep(&c->mc_meta_charset);
    av_freep(&c->mc_best_cb);
    av_freep(&c->mc_charset);
    av_freep(&c->mc_charmap);
    av_freep(&c->mc_colram);
    return 0;
}

static av_cold int a64multi_encode_init(AVCodecContext *avctx)
{
    A64Context *c = avctx->priv_data;
    int a;
    av_lfg_init(&c->randctx, 1);

    if (avctx->global_quality < 1) {
        c->mc_lifetime = 4;
    } else {
        c->mc_lifetime = avctx->global_quality /= FF_QP2LAMBDA;
    }

    av_log(avctx, AV_LOG_INFO, "charset lifetime set to %d frame(s)\n", c->mc_lifetime);

    c->mc_frame_counter = 0;
    c->mc_use_5col      = avctx->codec->id == AV_CODEC_ID_A64_MULTI5;
    c->mc_pal_size      = 4 + c->mc_use_5col;

    /* precalc luma values for later use */
    for (a = 0; a < c->mc_pal_size; a++) {
        c->mc_luma_vals[a]=a64_palette[mc_colors[a]][0] * 0.30 +
                           a64_palette[mc_colors[a]][1] * 0.59 +
                           a64_palette[mc_colors[a]][2] * 0.11;
    }

    if (!(c->mc_meta_charset = av_mallocz_array(c->mc_lifetime, 32000 * sizeof(int))) ||
       !(c->mc_best_cb       = av_malloc(CHARSET_CHARS * 32 * sizeof(int)))     ||
       !(c->mc_charmap       = av_mallocz_array(c->mc_lifetime, 1000 * sizeof(int))) ||
       !(c->mc_colram        = av_mallocz(CHARSET_CHARS * sizeof(uint8_t)))     ||
       !(c->mc_charset       = av_malloc(0x800 * (INTERLACED+1) * sizeof(uint8_t)))) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate buffer memory.\n");
        return AVERROR(ENOMEM);
    }

    /* set up extradata */
    if (!(avctx->extradata = av_mallocz(8 * 4 + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate memory for extradata.\n");
        return AVERROR(ENOMEM);
    }
    avctx->extradata_size = 8 * 4;
    AV_WB32(avctx->extradata, c->mc_lifetime);
    AV_WB32(avctx->extradata + 16, INTERLACED);

    if (!avctx->codec_tag)
         avctx->codec_tag = AV_RL32("a64m");

    c->next_pts = AV_NOPTS_VALUE;

    return 0;
}

static void a64_compress_colram(unsigned char *buf, int *charmap, uint8_t *colram)
{
    int a;
    uint8_t temp;
    /* only needs to be done in 5col mode */
    /* XXX could be squeezed to 0x80 bytes */
    for (a = 0; a < 256; a++) {
        temp  = colram[charmap[a + 0x000]] << 0;
        temp |= colram[charmap[a + 0x100]] << 1;
        temp |= colram[charmap[a + 0x200]] << 2;
        if (a < 0xe8) temp |= colram[charmap[a + 0x300]] << 3;
        buf[a] = temp << 2;
    }
}

static int a64multi_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                 const AVFrame *p, int *got_packet)
{
    A64Context *c = avctx->priv_data;

    int frame;
    int x, y;
    int b_height;
    int b_width;

    int req_size, ret;
    uint8_t *buf = NULL;

    int *charmap     = c->mc_charmap;
    uint8_t *colram  = c->mc_colram;
    uint8_t *charset = c->mc_charset;
    int *meta        = c->mc_meta_charset;
    int *best_cb     = c->mc_best_cb;

    int charset_size = 0x800 * (INTERLACED + 1);
    int colram_size  = 0x100 * c->mc_use_5col;
    int screen_size;

    if(CROP_SCREENS) {
        b_height = FFMIN(avctx->height,C64YRES) >> 3;
        b_width  = FFMIN(avctx->width ,C64XRES) >> 3;
        screen_size = b_width * b_height;
    } else {
        b_height = C64YRES >> 3;
        b_width  = C64XRES >> 3;
        screen_size = 0x400;
    }

    /* no data, means end encoding asap */
    if (!p) {
        /* all done, end encoding */
        if (!c->mc_lifetime) return 0;
        /* no more frames in queue, prepare to flush remaining frames */
        if (!c->mc_frame_counter) {
            c->mc_lifetime = 0;
        }
        /* still frames in queue so limit lifetime to remaining frames */
        else c->mc_lifetime = c->mc_frame_counter;
    /* still new data available */
    } else {
        /* fill up mc_meta_charset with data until lifetime exceeds */
        if (c->mc_frame_counter < c->mc_lifetime) {
            to_meta_with_crop(avctx, p, meta + 32000 * c->mc_frame_counter);
            c->mc_frame_counter++;
            if (c->next_pts == AV_NOPTS_VALUE)
                c->next_pts = p->pts;
            /* lifetime is not reached so wait for next frame first */
            return 0;
        }
    }

    /* lifetime reached so now convert X frames at once */
    if (c->mc_frame_counter == c->mc_lifetime) {
        req_size = 0;
        /* any frames to encode? */
        if (c->mc_lifetime) {
            int alloc_size = charset_size + c->mc_lifetime*(screen_size + colram_size);
            if ((ret = ff_alloc_packet2(avctx, pkt, alloc_size)) < 0)
                return ret;
            buf = pkt->data;

            /* calc optimal new charset + charmaps */
            ret = avpriv_init_elbg(meta, 32, 1000 * c->mc_lifetime, best_cb,
                               CHARSET_CHARS, 50, charmap, &c->randctx);
            if (ret < 0)
                return ret;
            ret = avpriv_do_elbg(meta, 32, 1000 * c->mc_lifetime, best_cb,
                             CHARSET_CHARS, 50, charmap, &c->randctx);
            if (ret < 0)
                return ret;

            /* create colorram map and a c64 readable charset */
            render_charset(avctx, charset, colram);

            /* copy charset to buf */
            memcpy(buf, charset, charset_size);

            /* advance pointers */
            buf      += charset_size;
            req_size += charset_size;
        }

        /* write x frames to buf */
        for (frame = 0; frame < c->mc_lifetime; frame++) {
            /* copy charmap to buf. buf is uchar*, charmap is int*, so no memcpy here, sorry */
            for (y = 0; y < b_height; y++) {
                for (x = 0; x < b_width; x++) {
                    buf[y * b_width + x] = charmap[y * b_width + x];
                }
            }
            /* advance pointers */
            buf += screen_size;
            req_size += screen_size;

            /* compress and copy colram to buf */
            if (c->mc_use_5col) {
                a64_compress_colram(buf, charmap, colram);
                /* advance pointers */
                buf += colram_size;
                req_size += colram_size;
            }

            /* advance to next charmap */
            charmap += 1000;
        }

        AV_WB32(avctx->extradata + 4,  c->mc_frame_counter);
        AV_WB32(avctx->extradata + 8,  charset_size);
        AV_WB32(avctx->extradata + 12, screen_size + colram_size);

        /* reset counter */
        c->mc_frame_counter = 0;

        pkt->pts = pkt->dts = c->next_pts;
        c->next_pts         = AV_NOPTS_VALUE;

        av_assert0(pkt->size >= req_size);
        pkt->size   = req_size;
        pkt->flags |= AV_PKT_FLAG_KEY;
        *got_packet = !!req_size;
    }
    return 0;
}

#if CONFIG_A64MULTI_ENCODER
AVCodec ff_a64multi_encoder = {
    .name           = "a64multi",
    .long_name      = NULL_IF_CONFIG_SMALL("Multicolor charset for Commodore 64"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_A64_MULTI,
    .priv_data_size = sizeof(A64Context),
    .init           = a64multi_encode_init,
    .encode2        = a64multi_encode_frame,
    .close          = a64multi_close_encoder,
    .pix_fmts       = (const enum AVPixelFormat[]) {AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE},
    .capabilities   = CODEC_CAP_DELAY,
};
#endif
#if CONFIG_A64MULTI5_ENCODER
AVCodec ff_a64multi5_encoder = {
    .name           = "a64multi5",
    .long_name      = NULL_IF_CONFIG_SMALL("Multicolor charset for Commodore 64, extended with 5th color (colram)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_A64_MULTI5,
    .priv_data_size = sizeof(A64Context),
    .init           = a64multi_encode_init,
    .encode2        = a64multi_encode_frame,
    .close          = a64multi_close_encoder,
    .pix_fmts       = (const enum AVPixelFormat[]) {AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE},
    .capabilities   = CODEC_CAP_DELAY,
};
#endif
