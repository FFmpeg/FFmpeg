/*
 * Zip Motion Blocks Video (ZMBV) encoder
 * Copyright (c) 2006 Konstantin Shishkov
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
 * Zip Motion Blocks Video encoder
 */

#include <stdio.h>
#include <stdlib.h>

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "internal.h"

#include <zlib.h>

/* Frame header flags */
#define ZMBV_KEYFRAME 1
#define ZMBV_DELTAPAL 2

/* Motion block width/height (maximum allowed value is 255)
 * Note: histogram datatype in block_cmp() must be big enough to hold values
 * up to (4 * ZMBV_BLOCK * ZMBV_BLOCK)
 */
#define ZMBV_BLOCK 16

/* Keyframe header format values */
enum ZmbvFormat {
    ZMBV_FMT_NONE  = 0,
    ZMBV_FMT_1BPP  = 1,
    ZMBV_FMT_2BPP  = 2,
    ZMBV_FMT_4BPP  = 3,
    ZMBV_FMT_8BPP  = 4,
    ZMBV_FMT_15BPP = 5,
    ZMBV_FMT_16BPP = 6,
    ZMBV_FMT_24BPP = 7,
    ZMBV_FMT_32BPP = 8
};

/**
 * Encoder context
 */
typedef struct ZmbvEncContext {
    AVCodecContext *avctx;

    int lrange, urange;
    uint8_t *comp_buf, *work_buf;
    uint8_t pal[768];
    uint32_t pal2[256]; //for quick comparisons
    uint8_t *prev, *prev_buf;
    int pstride;
    int comp_size;
    int keyint, curfrm;
    int bypp;
    enum ZmbvFormat fmt;
    z_stream zstream;

    int score_tab[ZMBV_BLOCK * ZMBV_BLOCK * 4 + 1];
} ZmbvEncContext;


/** Block comparing function
 * XXX should be optimized and moved to DSPContext
 */
static inline int block_cmp(ZmbvEncContext *c, uint8_t *src, int stride,
                            uint8_t *src2, int stride2, int bw, int bh,
                            int *xored)
{
    int sum = 0;
    int i, j;
    uint16_t histogram[256] = {0};
    int bw_bytes = bw * c->bypp;

    /* Build frequency histogram of byte values for src[] ^ src2[] */
    for(j = 0; j < bh; j++){
        for(i = 0; i < bw_bytes; i++){
            int t = src[i] ^ src2[i];
            histogram[t]++;
        }
        src += stride;
        src2 += stride2;
    }

    /* If not all the xored values were 0, then the blocks are different */
    *xored = (histogram[0] < bw_bytes * bh);

    /* Exit early if blocks are equal */
    if (!*xored) return 0;

    /* Sum the entropy of all values */
    for(i = 0; i < 256; i++)
        sum += c->score_tab[histogram[i]];

    return sum;
}

/** Motion estimation function
 * TODO make better ME decisions
 */
static int zmbv_me(ZmbvEncContext *c, uint8_t *src, int sstride, uint8_t *prev,
                   int pstride, int x, int y, int *mx, int *my, int *xored)
{
    int dx, dy, txored, tv, bv, bw, bh;
    int mx0, my0;

    mx0 = *mx;
    my0 = *my;
    bw = FFMIN(ZMBV_BLOCK, c->avctx->width - x);
    bh = FFMIN(ZMBV_BLOCK, c->avctx->height - y);

    /* Try (0,0) */
    bv = block_cmp(c, src, sstride, prev, pstride, bw, bh, xored);
    *mx = *my = 0;
    if(!bv) return 0;

    /* Try previous block's MV (if not 0,0) */
    if (mx0 || my0){
        tv = block_cmp(c, src, sstride, prev + mx0 * c->bypp + my0 * pstride, pstride, bw, bh, &txored);
        if(tv < bv){
            bv = tv;
            *mx = mx0;
            *my = my0;
            *xored = txored;
            if(!bv) return 0;
        }
    }

    /* Try other MVs from top-to-bottom, left-to-right */
    for(dy = -c->lrange; dy <= c->urange; dy++){
        for(dx = -c->lrange; dx <= c->urange; dx++){
            if(!dx && !dy) continue; // we already tested this block
            if(dx == mx0 && dy == my0) continue; // this one too
            tv = block_cmp(c, src, sstride, prev + dx * c->bypp + dy * pstride, pstride, bw, bh, &txored);
            if(tv < bv){
                 bv = tv;
                 *mx = dx;
                 *my = dy;
                 *xored = txored;
                 if(!bv) return 0;
             }
         }
    }
    return bv;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *pict, int *got_packet)
{
    ZmbvEncContext * const c = avctx->priv_data;
    const AVFrame * const p = pict;
    uint8_t *src, *prev, *buf;
    uint32_t *palptr;
    int keyframe, chpal;
    int fl;
    int work_size = 0, pkt_size;
    int bw, bh;
    int i, j, ret;

    keyframe = !c->curfrm;
    c->curfrm++;
    if(c->curfrm == c->keyint)
        c->curfrm = 0;
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    avctx->coded_frame->pict_type = keyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;
    avctx->coded_frame->key_frame = keyframe;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    palptr = (avctx->pix_fmt == AV_PIX_FMT_PAL8) ? (uint32_t *)p->data[1] : NULL;
    chpal = !keyframe && palptr && memcmp(palptr, c->pal2, 1024);

    src = p->data[0];
    prev = c->prev;
    if(chpal){
        uint8_t tpal[3];
        for(i = 0; i < 256; i++){
            AV_WB24(tpal, palptr[i]);
            c->work_buf[work_size++] = tpal[0] ^ c->pal[i * 3 + 0];
            c->work_buf[work_size++] = tpal[1] ^ c->pal[i * 3 + 1];
            c->work_buf[work_size++] = tpal[2] ^ c->pal[i * 3 + 2];
            c->pal[i * 3 + 0] = tpal[0];
            c->pal[i * 3 + 1] = tpal[1];
            c->pal[i * 3 + 2] = tpal[2];
        }
        memcpy(c->pal2, palptr, 1024);
    }
    if(keyframe){
        if (palptr){
            for(i = 0; i < 256; i++){
                AV_WB24(c->pal+(i*3), palptr[i]);
            }
            memcpy(c->work_buf, c->pal, 768);
            memcpy(c->pal2, palptr, 1024);
            work_size = 768;
        }
        for(i = 0; i < avctx->height; i++){
            memcpy(c->work_buf + work_size, src, avctx->width * c->bypp);
            src += p->linesize[0];
            work_size += avctx->width * c->bypp;
        }
    }else{
        int x, y, bh2, bw2, xored;
        uint8_t *tsrc, *tprev;
        uint8_t *mv;
        int mx = 0, my = 0;

        bw = (avctx->width + ZMBV_BLOCK - 1) / ZMBV_BLOCK;
        bh = (avctx->height + ZMBV_BLOCK - 1) / ZMBV_BLOCK;
        mv = c->work_buf + work_size;
        memset(c->work_buf + work_size, 0, (bw * bh * 2 + 3) & ~3);
        work_size += (bw * bh * 2 + 3) & ~3;
        /* for now just XOR'ing */
        for(y = 0; y < avctx->height; y += ZMBV_BLOCK) {
            bh2 = FFMIN(avctx->height - y, ZMBV_BLOCK);
            for(x = 0; x < avctx->width; x += ZMBV_BLOCK, mv += 2) {
                bw2 = FFMIN(avctx->width - x, ZMBV_BLOCK);

                tsrc = src + x * c->bypp;
                tprev = prev + x * c->bypp;

                zmbv_me(c, tsrc, p->linesize[0], tprev, c->pstride, x, y, &mx, &my, &xored);
                mv[0] = (mx << 1) | !!xored;
                mv[1] = my << 1;
                tprev += mx * c->bypp + my * c->pstride;
                if(xored){
                    for(j = 0; j < bh2; j++){
                        for(i = 0; i < bw2 * c->bypp; i++)
                            c->work_buf[work_size++] = tsrc[i] ^ tprev[i];
                        tsrc += p->linesize[0];
                        tprev += c->pstride;
                    }
                }
            }
            src += p->linesize[0] * ZMBV_BLOCK;
            prev += c->pstride * ZMBV_BLOCK;
        }
    }
    /* save the previous frame */
    src = p->data[0];
    prev = c->prev;
    for(i = 0; i < avctx->height; i++){
        memcpy(prev, src, avctx->width * c->bypp);
        prev += c->pstride;
        src += p->linesize[0];
    }

    if (keyframe)
        deflateReset(&c->zstream);

    c->zstream.next_in = c->work_buf;
    c->zstream.avail_in = work_size;
    c->zstream.total_in = 0;

    c->zstream.next_out = c->comp_buf;
    c->zstream.avail_out = c->comp_size;
    c->zstream.total_out = 0;
    if(deflate(&c->zstream, Z_SYNC_FLUSH) != Z_OK){
        av_log(avctx, AV_LOG_ERROR, "Error compressing data\n");
        return -1;
    }

    pkt_size = c->zstream.total_out + 1 + 6*keyframe;
    if ((ret = ff_alloc_packet2(avctx, pkt, pkt_size, 0)) < 0)
        return ret;
    buf = pkt->data;

    fl = (keyframe ? ZMBV_KEYFRAME : 0) | (chpal ? ZMBV_DELTAPAL : 0);
    *buf++ = fl;
    if (keyframe) {
        *buf++ = 0; // hi ver
        *buf++ = 1; // lo ver
        *buf++ = 1; // comp
        *buf++ = c->fmt; // format
        *buf++ = ZMBV_BLOCK; // block width
        *buf++ = ZMBV_BLOCK; // block height
    }
    memcpy(buf, c->comp_buf, c->zstream.total_out);

    pkt->flags |= AV_PKT_FLAG_KEY*keyframe;
    *got_packet = 1;

    return 0;
}

static av_cold int encode_end(AVCodecContext *avctx)
{
    ZmbvEncContext * const c = avctx->priv_data;

    av_freep(&c->comp_buf);
    av_freep(&c->work_buf);

    deflateEnd(&c->zstream);
    av_freep(&c->prev_buf);

    return 0;
}

/**
 * Init zmbv encoder
 */
static av_cold int encode_init(AVCodecContext *avctx)
{
    ZmbvEncContext * const c = avctx->priv_data;
    int zret; // Zlib return code
    int i;
    int lvl = 9;
    int prev_size, prev_offset;

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_PAL8:
        c->fmt = ZMBV_FMT_8BPP;
        c->bypp = 1;
        break;
    case AV_PIX_FMT_RGB555LE:
        c->fmt = ZMBV_FMT_15BPP;
        c->bypp = 2;
        break;
    case AV_PIX_FMT_RGB565LE:
        c->fmt = ZMBV_FMT_16BPP;
        c->bypp = 2;
        break;
    case AV_PIX_FMT_BGR0:
        c->fmt = ZMBV_FMT_32BPP;
        c->bypp = 4;
        break;
    default:
        av_log(avctx, AV_LOG_INFO, "unsupported pixel format\n");
        return AVERROR(EINVAL);
    }

    /* Entropy-based score tables for comparing blocks.
     * Suitable for blocks up to (ZMBV_BLOCK * ZMBV_BLOCK) bytes.
     * Scores are nonnegative, lower is better.
     */
    for(i = 1; i <= ZMBV_BLOCK * ZMBV_BLOCK * c->bypp; i++)
        c->score_tab[i] = -i * log2(i / (double)(ZMBV_BLOCK * ZMBV_BLOCK * c->bypp)) * 256;

    c->avctx = avctx;

    c->curfrm = 0;
    c->keyint = avctx->keyint_min;

    /* Motion estimation range: maximum distance is -64..63 */
    c->lrange = c->urange = 8;
    if(avctx->me_range > 0){
        c->lrange = FFMIN(avctx->me_range, 64);
        c->urange = FFMIN(avctx->me_range, 63);
    }

    if(avctx->compression_level >= 0)
        lvl = avctx->compression_level;
    if(lvl < 0 || lvl > 9){
        av_log(avctx, AV_LOG_ERROR, "Compression level should be 0-9, not %i\n", lvl);
        return AVERROR(EINVAL);
    }

    // Needed if zlib unused or init aborted before deflateInit
    memset(&c->zstream, 0, sizeof(z_stream));
    c->comp_size = avctx->width * c->bypp * avctx->height + 1024 +
        ((avctx->width + ZMBV_BLOCK - 1) / ZMBV_BLOCK) * ((avctx->height + ZMBV_BLOCK - 1) / ZMBV_BLOCK) * 2 + 4;
    if (!(c->work_buf = av_malloc(c->comp_size))) {
        av_log(avctx, AV_LOG_ERROR, "Can't allocate work buffer.\n");
        return AVERROR(ENOMEM);
    }
    /* Conservative upper bound taken from zlib v1.2.1 source via lcl.c */
    c->comp_size = c->comp_size + ((c->comp_size + 7) >> 3) +
                           ((c->comp_size + 63) >> 6) + 11;

    /* Allocate compression buffer */
    if (!(c->comp_buf = av_malloc(c->comp_size))) {
        av_log(avctx, AV_LOG_ERROR, "Can't allocate compression buffer.\n");
        return AVERROR(ENOMEM);
    }

    /* Allocate prev buffer - pad around the image to allow out-of-edge ME:
     * - The image should be padded with `lrange` rows before and `urange` rows
     *   after.
     * - The stride should be padded with `lrange` pixels, then rounded up to a
     *   multiple of 16 bytes.
     * - The first row should also be padded with `lrange` pixels before, then
     *   aligned up to a multiple of 16 bytes.
     */
    c->pstride = FFALIGN((avctx->width + c->lrange) * c->bypp, 16);
    prev_size = FFALIGN(c->lrange * c->bypp, 16) + c->pstride * (c->lrange + avctx->height + c->urange);
    prev_offset = FFALIGN(c->lrange, 16) + c->pstride * c->lrange;
    if (!(c->prev_buf = av_mallocz(prev_size))) {
        av_log(avctx, AV_LOG_ERROR, "Can't allocate picture.\n");
        return AVERROR(ENOMEM);
    }
    c->prev = c->prev_buf + prev_offset;

    c->zstream.zalloc = Z_NULL;
    c->zstream.zfree = Z_NULL;
    c->zstream.opaque = Z_NULL;
    zret = deflateInit(&c->zstream, lvl);
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate init error: %d\n", zret);
        return -1;
    }

    return 0;
}

AVCodec ff_zmbv_encoder = {
    .name           = "zmbv",
    .long_name      = NULL_IF_CONFIG_SMALL("Zip Motion Blocks Video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_ZMBV,
    .priv_data_size = sizeof(ZmbvEncContext),
    .init           = encode_init,
    .encode2        = encode_frame,
    .close          = encode_end,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_PAL8,
                                                     AV_PIX_FMT_RGB555LE,
                                                     AV_PIX_FMT_RGB565LE,
                                                     AV_PIX_FMT_BGR0,
                                                     AV_PIX_FMT_NONE },
};
