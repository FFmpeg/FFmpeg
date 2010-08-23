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

#include "a64enc.h"
#include "a64tables.h"
#include "elbg.h"
#include "libavutil/intreadwrite.h"

#define DITHERSTEPS 8

static void to_meta_with_crop(AVCodecContext *avctx, AVFrame *p, int *dest)
{
    int blockx, blocky, x, y;
    int luma = 0;
    int height = FFMIN(avctx->height,C64YRES);
    int width  = FFMIN(avctx->width ,C64XRES);
    uint8_t *src = p->data[0];

    for (blocky = 0; blocky < height; blocky += 8) {
        for (blockx = 0; blockx < C64XRES; blockx += 8) {
            for (y = blocky; y < blocky+8 && y < height; y++) {
                for (x = blockx; x < blockx+8 && x < C64XRES; x += 2) {
                    if(x < width) {
                        /* build average over 2 pixels */
                        luma = (src[(x + 0 + y * p->linesize[0])] +
                                src[(x + 1 + y * p->linesize[0])]) / 2;
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
    uint8_t row1;
    int charpos, x, y;
    int pix;
    int dither;
    int index1, index2;
    int lowdiff, highdiff;
    int maxindex = c->mc_use_5col + 3;
    int maxsteps = DITHERSTEPS * maxindex + 1;
    int *best_cb = c->mc_best_cb;

    /* now reduce colors first */
    for (x = 0; x < 256 * 32; x++) best_cb[x] = best_cb[x] * maxsteps / 255;

    /* and render charset */
    for (charpos = 0; charpos < 256; charpos++) {
        lowdiff  = 0;
        highdiff = 0;
        for (y = 0; y < 8; y++) {
            row1 = 0;
            for (x = 0; x < 4; x++) {
                pix = best_cb[y * 4 + x];
                dither = pix % DITHERSTEPS;
                index1 = pix / DITHERSTEPS;
                index2 = FFMIN(index1 + 1, maxindex);

                if (pix > 3 * DITHERSTEPS)
                    highdiff += pix - 3 * DITHERSTEPS;
                if (pix < DITHERSTEPS)
                    lowdiff += DITHERSTEPS - pix;

                row1 <<= 2;
                if (prep_dither_patterns[dither][y & 3][x & 3]) {
                    row1 |= 3-(index2 & 3);
                } else {
                    row1 |= 3-(index1 & 3);
                }
            }
            charset[y] = row1;
        }

        /* are we in 5col mode and need to adjust pixels? */
        if (c->mc_use_5col && highdiff > 0 && lowdiff > 0) {
            if (lowdiff > highdiff) {
                for (x = 0; x < 32; x++)
                    best_cb[x] = FFMIN(3 * DITHERSTEPS, best_cb[x]);
            } else {
                for (x = 0; x < 32; x++)
                    best_cb[x] = FFMAX(DITHERSTEPS, best_cb[x]);
            }
            charpos--;          /* redo char */
        } else {
            /* advance pointers */
            best_cb += 32;
            charset += 8;

            if (highdiff > 0) {
                colrammap[charpos] = 0x9;
            } else {
                colrammap[charpos] = 0x8;
            }
        }
    }
}

static av_cold int a64multi_close_encoder(AVCodecContext *avctx)
{
    A64Context *c = avctx->priv_data;
    av_free(c->mc_meta_charset);
    av_free(c->mc_best_cb);
    av_free(c->mc_charmap);
    return 0;
}

static av_cold int a64multi_init_encoder(AVCodecContext *avctx)
{
    A64Context *c = avctx->priv_data;
    av_lfg_init(&c->randctx, 1);

    if (avctx->global_quality < 1) {
        c->mc_lifetime = 4;
    } else {
        c->mc_lifetime = avctx->global_quality /= FF_QP2LAMBDA;
    }

    av_log(avctx, AV_LOG_INFO, "charset lifetime set to %d frame(s)\n", c->mc_lifetime);

    c->mc_frame_counter = 0;
    c->mc_use_5col      = avctx->codec->id == CODEC_ID_A64_MULTI5;
    c->mc_meta_charset  = av_malloc(32000 * c->mc_lifetime * sizeof(int));
    c->mc_best_cb       = av_malloc(256 * 32 * sizeof(int));
    c->mc_charmap       = av_malloc(1000 * c->mc_lifetime * sizeof(int));

    avcodec_get_frame_defaults(&c->picture);
    avctx->coded_frame            = &c->picture;
    avctx->coded_frame->pict_type = FF_I_TYPE;
    avctx->coded_frame->key_frame = 1;
    if (!avctx->codec_tag)
         avctx->codec_tag = AV_RL32("a64m");

    return 0;
}

static int a64multi_encode_frame(AVCodecContext *avctx, unsigned char *buf,
                                 int buf_size, void *data)
{
    A64Context *c = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame *const p = (AVFrame *) & c->picture;

    int frame;
    int a;

    uint8_t colrammap[256];
    int *charmap = c->mc_charmap;
    int *meta    = c->mc_meta_charset;
    int *best_cb = c->mc_best_cb;
    int frm_size = 0x400 + 0x400 * c->mc_use_5col;
    int req_size;

    /* it is the last frame so prepare to flush */
    if (!data)
        c->mc_lifetime = c->mc_frame_counter;

    req_size = 0x800 + frm_size * c->mc_lifetime;

    if (req_size > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "buf size too small (need %d, got %d)\n", req_size, buf_size);
        return -1;
    }
    /* fill up mc_meta_charset with framedata until lifetime exceeds */
    if (c->mc_frame_counter < c->mc_lifetime) {
        *p = *pict;
        p->pict_type = FF_I_TYPE;
        p->key_frame = 1;
        to_meta_with_crop(avctx, p, meta + 32000 * c->mc_frame_counter);
        c->mc_frame_counter++;
        /* lifetime is not reached */
        return 0;
    }
    /* lifetime exceeded so now convert X frames at once */
    if (c->mc_frame_counter == c->mc_lifetime && c->mc_lifetime > 0) {
        c->mc_frame_counter = 0;
        ff_init_elbg(meta, 32, 1000 * c-> mc_lifetime, best_cb, 256, 5, charmap, &c->randctx);
        ff_do_elbg  (meta, 32, 1000 * c-> mc_lifetime, best_cb, 256, 5, charmap, &c->randctx);

        render_charset(avctx, buf, colrammap);

        for (frame = 0; frame < c->mc_lifetime; frame++) {
            for (a = 0; a < 1000; a++) {
                buf[0x800 + a] = charmap[a];
                if (c->mc_use_5col) buf[0xc00 + a] = colrammap[charmap[a]];
            }
            buf += frm_size;
            charmap += 1000;
        }
        return req_size;
    }
    return 0;
}

AVCodec a64multi_encoder = {
    .name           = "a64multi",
    .type           = CODEC_TYPE_VIDEO,
    .id             = CODEC_ID_A64_MULTI,
    .priv_data_size = sizeof(A64Context),
    .init           = a64multi_init_encoder,
    .encode         = a64multi_encode_frame,
    .close          = a64multi_close_encoder,
    .pix_fmts       = (enum PixelFormat[]) {PIX_FMT_GRAY8, PIX_FMT_NONE},
    .long_name      = NULL_IF_CONFIG_SMALL("Multicolor charset for Commodore 64"),
    .capabilities   = CODEC_CAP_DELAY,
};

AVCodec a64multi5_encoder = {
    .name           = "a64multi5",
    .type           = CODEC_TYPE_VIDEO,
    .id             = CODEC_ID_A64_MULTI5,
    .priv_data_size = sizeof(A64Context),
    .init           = a64multi_init_encoder,
    .encode         = a64multi_encode_frame,
    .close          = a64multi_close_encoder,
    .pix_fmts       = (enum PixelFormat[]) {PIX_FMT_GRAY8, PIX_FMT_NONE},
    .long_name      = NULL_IF_CONFIG_SMALL("Multicolor charset for Commodore 64, extended with 5th color (colram)"),
    .capabilities   = CODEC_CAP_DELAY,
};
