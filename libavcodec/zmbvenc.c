/*
 * Zip Motion Blocks Video (ZMBV) encoder
 * Copyright (c) 2006 Konstantin Shishkov
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

#define ZMBV_KEYFRAME 1
#define ZMBV_DELTAPAL 2

#define ZMBV_BLOCK 16

/**
 * Encoder context
 */
typedef struct ZmbvEncContext {
    AVCodecContext *avctx;

    int range;
    uint8_t *comp_buf, *work_buf;
    uint8_t pal[768];
    uint32_t pal2[256]; //for quick comparisons
    uint8_t *prev;
    int pstride;
    int comp_size;
    int keyint, curfrm;
    z_stream zstream;
} ZmbvEncContext;

static int score_tab[256];

/** Block comparing function
 * XXX should be optimized and moved to DSPContext
 * TODO handle out of edge ME
 */
static inline int block_cmp(uint8_t *src, int stride, uint8_t *src2, int stride2,
                            int bw, int bh, int *xored)
{
    int sum = 0;
    int i, j;
    uint8_t histogram[256] = {0};

    *xored = 0;
    for(j = 0; j < bh; j++){
        for(i = 0; i < bw; i++){
            int t = src[i] ^ src2[i];
            histogram[t]++;
            *xored |= t;
        }
        src += stride;
        src2 += stride2;
    }

    for(i = 1; i < 256; i++)
        sum += score_tab[histogram[i]];

    return sum;
}

/** Motion estimation function
 * TODO make better ME decisions
 */
static int zmbv_me(ZmbvEncContext *c, uint8_t *src, int sstride, uint8_t *prev,
                   int pstride, int x, int y, int *mx, int *my, int *xored)
{
    int dx, dy, tx, ty, tv, bv, bw, bh;

    *mx = *my = 0;
    bw = FFMIN(ZMBV_BLOCK, c->avctx->width - x);
    bh = FFMIN(ZMBV_BLOCK, c->avctx->height - y);
    bv = block_cmp(src, sstride, prev, pstride, bw, bh, xored);
    if(!bv) return 0;
    for(ty = FFMAX(y - c->range, 0); ty < FFMIN(y + c->range, c->avctx->height - bh); ty++){
        for(tx = FFMAX(x - c->range, 0); tx < FFMIN(x + c->range, c->avctx->width - bw); tx++){
            if(tx == x && ty == y) continue; // we already tested this block
            dx = tx - x;
            dy = ty - y;
            tv = block_cmp(src, sstride, prev + dx + dy*pstride, pstride, bw, bh, xored);
            if(tv < bv){
                 bv = tv;
                 *mx = dx;
                 *my = dy;
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
    chpal = !keyframe && memcmp(p->data[1], c->pal2, 1024);

    palptr = (uint32_t*)p->data[1];
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
        memcpy(c->pal2, p->data[1], 1024);
    }
    if(keyframe){
        for(i = 0; i < 256; i++){
            AV_WB24(c->pal+(i*3), palptr[i]);
        }
        memcpy(c->work_buf, c->pal, 768);
        memcpy(c->pal2, p->data[1], 1024);
        work_size = 768;
        for(i = 0; i < avctx->height; i++){
            memcpy(c->work_buf + work_size, src, avctx->width);
            src += p->linesize[0];
            work_size += avctx->width;
        }
    }else{
        int x, y, bh2, bw2, xored;
        uint8_t *tsrc, *tprev;
        uint8_t *mv;
        int mx, my;

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

                tsrc = src + x;
                tprev = prev + x;

                zmbv_me(c, tsrc, p->linesize[0], tprev, c->pstride, x, y, &mx, &my, &xored);
                mv[0] = (mx << 1) | !!xored;
                mv[1] = my << 1;
                tprev += mx + my * c->pstride;
                if(xored){
                    for(j = 0; j < bh2; j++){
                        for(i = 0; i < bw2; i++)
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
        memcpy(prev, src, avctx->width);
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
    if ((ret = ff_alloc_packet(pkt, pkt_size)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error getting packet of size %d.\n", pkt_size);
        return ret;
    }
    buf = pkt->data;

    fl = (keyframe ? ZMBV_KEYFRAME : 0) | (chpal ? ZMBV_DELTAPAL : 0);
    *buf++ = fl;
    if (keyframe) {
        *buf++ = 0; // hi ver
        *buf++ = 1; // lo ver
        *buf++ = 1; // comp
        *buf++ = 4; // format - 8bpp
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
    av_freep(&c->prev);

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

    for(i=1; i<256; i++)
        score_tab[i]= -i * log(i/(double)(ZMBV_BLOCK*ZMBV_BLOCK)) * (256/M_LN2);

    c->avctx = avctx;

    c->curfrm = 0;
    c->keyint = avctx->keyint_min;
    c->range = 8;
    if(avctx->me_range > 0)
        c->range = FFMIN(avctx->me_range, 127);

    if(avctx->compression_level >= 0)
        lvl = avctx->compression_level;
    if(lvl < 0 || lvl > 9){
        av_log(avctx, AV_LOG_ERROR, "Compression level should be 0-9, not %i\n", lvl);
        return AVERROR(EINVAL);
    }

    // Needed if zlib unused or init aborted before deflateInit
    memset(&c->zstream, 0, sizeof(z_stream));
    c->comp_size = avctx->width * avctx->height + 1024 +
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
    c->pstride = FFALIGN(avctx->width, 16);
    if (!(c->prev = av_malloc(c->pstride * avctx->height))) {
        av_log(avctx, AV_LOG_ERROR, "Can't allocate picture.\n");
        return AVERROR(ENOMEM);
    }

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
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_PAL8, AV_PIX_FMT_NONE },
};
